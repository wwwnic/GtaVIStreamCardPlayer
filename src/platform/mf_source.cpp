#include <windows.h>

#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>

#include "platform/mf_source.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "core/capture_choice.h"
#include "core/luma_summary.h"
#include "platform/log.h"

using Microsoft::WRL::ComPtr;

namespace gp {

namespace {

const DWORD kVideoStream = static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM);
const DWORD kAllStreams = static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS);

std::string SubtypeName(const GUID& subtype) {
    if (subtype == MFVideoFormat_YUY2) return "YUY2";
    if (subtype == MFVideoFormat_NV12) return "NV12";
    if (subtype == MFVideoFormat_MJPG) return "MJPG";
    if (subtype == MFVideoFormat_RGB24) return "RGB24";
    if (subtype == MFVideoFormat_P010) return "P010";
    const uint32_t fourcc = subtype.Data1;
    char text[5] = {static_cast<char>(fourcc & 255), static_cast<char>((fourcc >> 8) & 255),
                    static_cast<char>((fourcc >> 16) & 255), static_cast<char>((fourcc >> 24) & 255), 0};
    for (char& c : text) {
        if (c != 0 && (c < 32 || c > 126)) c = '?';
    }
    return text;
}

CaptureMode ModeOf(IMFMediaType* type) {
    CaptureMode mode;
    GUID subtype = {};
    type->GetGUID(MF_MT_SUBTYPE, &subtype);
    if (subtype == MFVideoFormat_YUY2) mode.format = PixelFormat::YUY2;
    if (subtype == MFVideoFormat_NV12) mode.format = PixelFormat::NV12;
    mode.motionJpeg = subtype == MFVideoFormat_MJPG;
    MFGetAttributeSize(type, MF_MT_FRAME_SIZE, &mode.width, &mode.height);
    MFGetAttributeRatio(type, MF_MT_FRAME_RATE, &mode.fpsNumerator, &mode.fpsDenominator);
    return mode;
}

// The colour description the Capture Card attached to this mode. Modes on the
// GC553 Pro differ (some are full range BT.601), so this is never assumed.
Colour ColourOf(IMFMediaType* type) {
    Colour colour;
    switch (MFGetAttributeUINT32(type, MF_MT_VIDEO_NOMINAL_RANGE, MFNominalRange_Unknown)) {
    case MFNominalRange_0_255: colour.range = ColourRange::Full; break;
    case MFNominalRange_16_235: colour.range = ColourRange::Limited; break;
    default: break;
    }
    switch (MFGetAttributeUINT32(type, MF_MT_YUV_MATRIX, MFVideoTransferMatrix_Unknown)) {
    case MFVideoTransferMatrix_BT709: colour.matrix = ColourMatrix::BT709; break;
    case MFVideoTransferMatrix_BT601: colour.matrix = ColourMatrix::BT601; break;
    default: break;
    }
    return colour;
}

std::string Describe(IMFMediaType* type) {
    GUID subtype = {};
    type->GetGUID(MF_MT_SUBTYPE, &subtype);
    UINT32 width = 0, height = 0, fpsN = 0, fpsD = 1;
    MFGetAttributeSize(type, MF_MT_FRAME_SIZE, &width, &height);
    MFGetAttributeRatio(type, MF_MT_FRAME_RATE, &fpsN, &fpsD);
    char text[200];
    snprintf(text, sizeof text, "%s %ux%u @ %.3f fps (%u/%u) nominalRange=%u yuvMatrix=%u interlace=%u",
             SubtypeName(subtype).c_str(), width, height, fpsD ? static_cast<double>(fpsN) / fpsD : 0.0, fpsN, fpsD,
             MFGetAttributeUINT32(type, MF_MT_VIDEO_NOMINAL_RANGE, 0), MFGetAttributeUINT32(type, MF_MT_YUV_MATRIX, 0),
             MFGetAttributeUINT32(type, MF_MT_INTERLACE_MODE, 0));
    return text;
}

std::vector<std::wstring> StringsOf(IMFActivate** devices, UINT32 count, REFGUID key) {
    std::vector<std::wstring> strings(count);
    for (UINT32 i = 0; i < count; i++) {
        WCHAR* text = nullptr;
        UINT32 length = 0;
        if (SUCCEEDED(devices[i]->GetAllocatedString(key, &text, &length)) && text) strings[i] = text;
        CoTaskMemFree(text);
    }
    return strings;
}

std::vector<std::wstring> NamesOf(IMFActivate** devices, UINT32 count) {
    return StringsOf(devices, count, MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME);
}

// The symbolic link: the remembered Capture Card's identifier (ticket 09).
std::vector<std::wstring> IdsOf(IMFActivate** devices, UINT32 count) {
    return StringsOf(devices, count, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK);
}

HRESULT EnumerateVideoCaptureDevices(IMFActivate*** devices, UINT32* count) {
    ComPtr<IMFAttributes> query;
    HRESULT hr = MFCreateAttributes(&query, 1);
    if (SUCCEEDED(hr)) hr = query->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    if (SUCCEEDED(hr)) hr = MFEnumDeviceSources(query.Get(), devices, count);
    return hr;
}

// For the log. A value the card gave but the Viewer does not convert is named
// by its number, not called unspecified.
std::string DefaultedText(IMFMediaType* type, REFGUID key, const char* treatedAs) {
    UINT32 raw = 0;
    const bool given = SUCCEEDED(type->GetUINT32(key, &raw)) && raw != 0;  // 0: "unknown" in both lists
    char text[80];
    if (given) {
        snprintf(text, sizeof text, "unsupported value %u (treated as %s)", raw, treatedAs);
    } else {
        snprintf(text, sizeof text, "unspecified (treated as %s)", treatedAs);
    }
    return text;
}

std::string RangeText(IMFMediaType* type, ColourRange range) {
    switch (range) {
    case ColourRange::Full: return "full";
    case ColourRange::Limited: return "limited";
    default: return DefaultedText(type, MF_MT_VIDEO_NOMINAL_RANGE, "limited");
    }
}

std::string MatrixText(IMFMediaType* type, ColourMatrix matrix) {
    switch (matrix) {
    case ColourMatrix::BT601: return "BT.601";
    case ColourMatrix::BT709: return "BT.709";
    default: return DefaultedText(type, MF_MT_YUV_MATRIX, "BT.709");
    }
}

// Receives samples from the Source Reader on a Media Foundation worker thread,
// copies each into a Frame and hands it to the sink.
class ReaderCallback final : public IMFSourceReaderCallback {
public:
    ReaderCallback() : flushed_(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {}

    void Begin(ComPtr<IMFSourceReader> reader, Source::FrameSink sink, Source::EventSink events, PixelFormat format,
               uint32_t width, uint32_t height, Colour colour) {
        std::lock_guard lock(mutex_);
        reader_ = std::move(reader);
        sink_ = std::move(sink);
        events_ = std::move(events);
        format_ = format;
        width_ = width;
        height_ = height;
        colour_ = colour;
    }

    // After this returns the sink is never called again.
    void End() {
        ComPtr<IMFSourceReader> reader;
        {
            std::lock_guard lock(mutex_);  // waits for a sink call in progress
            sink_ = nullptr;
            events_ = nullptr;
            reader = reader_;
        }
        if (reader) {
            // Flush cancels the pending read; OnFlush tells us it is done.
            if (SUCCEEDED(reader->Flush(kAllStreams))) {
                if (WaitForSingleObject(flushed_, 2000) != WAIT_OBJECT_0) Log("Source: flush did not complete in 2 s");
            }
        }
        std::lock_guard lock(mutex_);
        reader_.Reset();  // breaks the reader <-> callback reference cycle
    }

    uint64_t FramesDelivered() const { return sequence_.load(); }

    Source::Probe TakeProbe() {
        Source::Probe probe;
        probe.frames = frames_.exchange(0);
        probe.streamTicks = streamTicks_.exchange(0);
        probe.typeChanges = typeChanges_.exchange(0);
        probe.discontinuities = discontinuities_.exchange(0);
        {
            std::lock_guard lock(lumaMutex_);
            probe.lumaMin = luma_.min;
            probe.lumaMax = luma_.max;
            probe.lumaMean = luma_.mean;
            luma_ = {};
        }
        lumaWanted_ = true;  // summarise the next frame
        return probe;
    }

    STDMETHODIMP QueryInterface(REFIID riid, void** object) override {
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IMFSourceReaderCallback)) {
            *object = static_cast<IMFSourceReaderCallback*>(this);
            AddRef();
            return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return static_cast<ULONG>(InterlockedIncrement(&references_)); }
    STDMETHODIMP_(ULONG) Release() override {
        const LONG remaining = InterlockedDecrement(&references_);
        if (remaining == 0) delete this;
        return static_cast<ULONG>(remaining);
    }

    STDMETHODIMP OnReadSample(HRESULT status, DWORD, DWORD flags, LONGLONG, IMFSample* sample) override {
        LARGE_INTEGER arrival;
        QueryPerformanceCounter(&arrival);

        std::lock_guard lock(mutex_);
        if (!sink_ || !reader_) return S_OK;  // stopping
        // Another program holding the card shows here, on the first read:
        // status 0xC00D3704 with the error flag (GC553 Pro).
        const SourceStage stage = sequence_ == 0 ? SourceStage::FirstRead : SourceStage::Read;
        if (FAILED(status) || (flags & MF_SOURCE_READERF_ERROR)) {
            Fail(stage, FAILED(status) ? status : E_FAIL, "read failed");
            return S_OK;
        }
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
            Fail(stage, MF_E_END_OF_STREAM, "end of stream");
            return S_OK;
        }
        if (flags & MF_SOURCE_READERF_STREAMTICK) streamTicks_++;
        if (flags & (MF_SOURCE_READERF_NATIVEMEDIATYPECHANGED | MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED)) {
            typeChanges_++;
            Log("Source: media type changed mid-stream (flags=0x%lX)", static_cast<unsigned long>(flags));
            if (!AdoptCurrentType()) {
                Fail(SourceStage::Read, MF_E_INVALIDMEDIATYPE, "the new media type is not one the Viewer shows");
                return S_OK;
            }
        }
        if (sample) {
            if (MFGetAttributeUINT32(sample, MFSampleExtension_Discontinuity, FALSE)) discontinuities_++;
            if (FramePtr frame = CopySample(sample, arrival.QuadPart)) {
                frames_++;
                if (lumaWanted_.exchange(false)) {
                    const LumaSummary luma = SummarizeLuma(*frame);
                    std::lock_guard lumaLock(lumaMutex_);
                    luma_ = luma;
                }
                sink_(std::move(frame));
            }
        }
        const HRESULT hr = reader_->ReadSample(kVideoStream, 0, nullptr, nullptr, nullptr, nullptr);
        if (FAILED(hr)) Fail(SourceStage::Read, hr, "ReadSample failed");
        return S_OK;
    }
    STDMETHODIMP OnEvent(DWORD, IMFMediaEvent*) override { return S_OK; }
    STDMETHODIMP OnFlush(DWORD) override {
        SetEvent(flushed_);
        return S_OK;
    }

private:
    ~ReaderCallback() { CloseHandle(flushed_); }

    // Capture ends; the Viewer decides what to do. Called with mutex_ held.
    void Fail(SourceStage stage, HRESULT hr, const char* what) {
        Log("Source: %s hr=0x%08lX (%s) after %llu frames; capture stopped", what, static_cast<unsigned long>(hr),
            NameOf(stage), static_cast<unsigned long long>(sequence_.load()));
        if (events_) events_({SourceEvent::Kind::Failed, stage, static_cast<uint32_t>(hr)});
        events_ = nullptr;  // one failure per start
    }

    // After a type change the frames are described by the reader's current
    // type, not the one read at Start. False: a type the Viewer cannot show
    // (the Feed is then restarted, which sets the chosen mode again). Called
    // with mutex_ held. The mode in About stays the one set at Start.
    bool AdoptCurrentType() {
        ComPtr<IMFMediaType> current;
        const HRESULT hr = reader_->GetCurrentMediaType(kVideoStream, &current);
        if (FAILED(hr)) {
            Log("Source: GetCurrentMediaType failed hr=0x%08lX; frames read as before", static_cast<unsigned long>(hr));
            return true;
        }
        const CaptureMode mode = ModeOf(current.Get());
        if (!mode.format || mode.width == 0 || mode.height == 0) {
            Log("Source: now %s", Describe(current.Get()).c_str());
            return false;
        }
        const Colour colour = ColourOf(current.Get());
        if (*mode.format == format_ && mode.width == width_ && mode.height == height_ && colour == colour_) return true;
        format_ = *mode.format;
        width_ = mode.width;
        height_ = mode.height;
        colour_ = colour;
        warnedShortBuffer_ = warnedLongBuffer_ = false;
        Log("Source: now %s; colour range %s, matrix %s", Describe(current.Get()).c_str(),
            RangeText(current.Get(), colour.range).c_str(), MatrixText(current.Get(), colour.matrix).c_str());
        return true;
    }

    FramePtr CopySample(IMFSample* sample, int64_t arrivalQpc) {
        ComPtr<IMFMediaBuffer> buffer;
        if (FAILED(sample->ConvertToContiguousBuffer(&buffer))) return nullptr;
        const size_t byteSize = FrameByteSize(format_, width_, height_);
        BYTE* data = nullptr;
        DWORD length = 0;
        if (FAILED(buffer->Lock(&data, nullptr, &length))) return nullptr;
        std::shared_ptr<Frame> frame;
        if (length > byteSize && !warnedLongBuffer_) {
            warnedLongBuffer_ = true;  // copied as packed all the same; a sheared picture would be padded rows
            Log("Source: buffer of %lu bytes is longer than a %zu byte frame; its rows may be padded",
                static_cast<unsigned long>(length), byteSize);
        }
        if (length >= byteSize) {
            // With converters disabled the card's buffer is tightly packed
            // (verified by the prototype on the GC553 Pro), which is exactly
            // the layout Frame promises.
            frame = pool_.Acquire(byteSize);
            memcpy(frame->pixels.data(), data, byteSize);
        } else if (!warnedShortBuffer_) {
            warnedShortBuffer_ = true;
            Log("Source: buffer of %lu bytes is shorter than a %zu byte frame; frames skipped",
                static_cast<unsigned long>(length), byteSize);
        }
        buffer->Unlock();
        if (!frame) return nullptr;
        frame->width = width_;
        frame->height = height_;
        frame->format = format_;
        frame->colour = colour_;
        frame->arrivalQpc = arrivalQpc;
        frame->sequence = ++sequence_;
        return frame;
    }

    LONG references_ = 1;
    HANDLE flushed_;
    std::mutex mutex_;  // guards everything below; held while the sink runs
    ComPtr<IMFSourceReader> reader_;
    Source::FrameSink sink_;
    Source::EventSink events_;
    PixelFormat format_ = PixelFormat::YUY2;
    uint32_t width_ = 0, height_ = 0;
    Colour colour_;
    FramePool pool_;
    std::atomic<uint64_t> sequence_{0};
    bool warnedShortBuffer_ = false, warnedLongBuffer_ = false;

    // The probe (no-signal investigation), taken once a second from another thread.
    std::atomic<uint32_t> frames_{0}, streamTicks_{0}, typeChanges_{0}, discontinuities_{0};
    std::atomic<bool> lumaWanted_{true};
    std::mutex lumaMutex_;
    LumaSummary luma_;
};

class MediaFoundationSource final : public Source {
public:
    MediaFoundationSource() {
        const HRESULT hr = MFStartup(MF_VERSION);
        mfStarted_ = SUCCEEDED(hr);
        if (!mfStarted_) Log("Source: MFStartup failed hr=0x%08lX", static_cast<unsigned long>(hr));
    }

    ~MediaFoundationSource() override {
        Stop();
        if (mfStarted_) MFShutdown();
    }

    // Every start chooses the device from scratch: after a replug the card
    // may come back under a new identity.
    bool Start(FrameSink frames, EventSink events) override {
        if (callback_) return false;
        if (!mfStarted_) {
            events({SourceEvent::Kind::Failed, SourceStage::Enumerate, static_cast<uint32_t>(E_FAIL)});
            return false;
        }
        std::optional<SourceEvent> failure = OpenDevice();
        if (!failure) failure = StartReading(std::move(frames), events);
        if (failure) {
            Stop();
            events(*failure);
            return false;
        }
        return true;
    }

    void Stop() override {
        currentMode_.reset();
        if (callback_) {
            callback_->End();
            const uint64_t frames = callback_->FramesDelivered();
            if (frames || detail_) Log("Source: stopped after %llu frames", static_cast<unsigned long long>(frames));
            callback_.Reset();
        }
        if (device_) {
            device_->Shutdown();
            device_.Reset();
        }
    }

    std::wstring DeviceName() const override { return deviceName_; }
    std::wstring DeviceId() const override { return deviceId_; }

    void Choose(const Choice& choice) override {
        choice_ = choice;
        choiceChanged_ = true;
    }
    std::vector<CaptureMode> Modes() const override { return modes_; }
    std::optional<CaptureMode> CurrentMode() const override { return currentMode_; }

    Probe TakeProbe() override { return callback_ ? callback_->TakeProbe() : Probe{}; }

private:
    // Retries happen every second or so; their details are logged only when
    // the device list differs from the previous attempt's.
    template <typename... Args>
    void Detail(const char* format, Args... args) {
        if (detail_) Log(format, args...);
    }

    static SourceEvent Failure(SourceStage stage, HRESULT hr) {
        return {SourceEvent::Kind::Failed, stage, static_cast<uint32_t>(hr)};
    }

    std::optional<SourceEvent> OpenDevice() {
        deviceName_.clear();
        deviceId_.clear();
        modes_.clear();
        IMFActivate** devices = nullptr;
        UINT32 count = 0;
        HRESULT hr = EnumerateVideoCaptureDevices(&devices, &count);
        if (FAILED(hr)) {
            Log("Source: MFEnumDeviceSources failed hr=0x%08lX", static_cast<unsigned long>(hr));
            return Failure(SourceStage::Enumerate, hr);
        }
        const std::vector<std::wstring> names = NamesOf(devices, count);
        const std::vector<std::wstring> ids = IdsOf(devices, count);
        // A new choice logs its details like a changed device list.
        detail_ = !lastNames_ || names != *lastNames_ || choiceChanged_;
        choiceChanged_ = false;
        lastNames_ = names;
        if (detail_) Log("Source: %u capture devices", count);
        for (UINT32 i = 0; i < count; i++) {
            Detail("Source: capture device [%u] \"%s\" id %s", i, ToUtf8(names[i]).c_str(), ToUtf8(ids[i]).c_str());
        }
        std::optional<SourceEvent> failure;
        const std::optional<size_t> chosen = ChooseCaptureDevice(names, ids, choice_.device, choice_.deviceId);
        const bool byId = chosen && !choice_.deviceId.empty() && ids[*chosen] == choice_.deviceId;
        const bool byName = chosen && !choice_.device.empty() && names[*chosen] == choice_.device;
        if (chosen && !choice_.device.empty() && !byId && !byName) {
            Detail("Source: chosen Capture Card \"%s\" is not present; automatic choice", ToUtf8(choice_.device).c_str());
        } else if (byName && !byId && !choice_.deviceId.empty()) {
            Detail("Source: chosen Capture Card found by name; its identifier changed");
        }
        if (chosen) {
            Detail("Source: chosen device [%zu] \"%s\"%s", *chosen, ToUtf8(names[*chosen]).c_str(),
                   byId ? " (by identifier)" : byName ? " (by name)" : " (automatic)");
            deviceName_ = names[*chosen];
            deviceId_ = ids[*chosen];
            hr = devices[*chosen]->ActivateObject(IID_PPV_ARGS(&device_));
            if (FAILED(hr)) {
                Log("Source: could not open the device hr=0x%08lX", static_cast<unsigned long>(hr));
                failure = Failure(SourceStage::Open, hr);
            }
        } else {
            Detail("Source: no video capture device found");
            failure = SourceEvent{SourceEvent::Kind::NoDevice, SourceStage::Enumerate, 0};
        }
        for (UINT32 i = 0; i < count; i++) devices[i]->Release();
        CoTaskMemFree(devices);
        return failure;
    }

    std::optional<SourceEvent> StartReading(FrameSink sink, const EventSink& events) {
        callback_.Attach(new ReaderCallback());

        ComPtr<IMFAttributes> settings;
        HRESULT hr = MFCreateAttributes(&settings, 3);
        if (SUCCEEDED(hr)) hr = settings->SetUnknown(MF_SOURCE_READER_ASYNC_CALLBACK, callback_.Get());
        // No decoder, colour converter or video processor may be inserted:
        // frames must arrive exactly as the Capture Card produced them.
        if (SUCCEEDED(hr)) hr = settings->SetUINT32(MF_READWRITE_DISABLE_CONVERTERS, TRUE);
        if (SUCCEEDED(hr)) hr = settings->SetUINT32(MF_LOW_LATENCY, TRUE);
        ComPtr<IMFSourceReader> reader;
        if (SUCCEEDED(hr)) hr = MFCreateSourceReaderFromMediaSource(device_.Get(), settings.Get(), &reader);
        if (FAILED(hr)) {
            Log("Source: could not create the Source Reader hr=0x%08lX", static_cast<unsigned long>(hr));
            return Failure(SourceStage::Reader, hr);
        }

        std::vector<CaptureMode> modes;
        for (DWORD i = 0;; i++) {
            ComPtr<IMFMediaType> type;
            hr = reader->GetNativeMediaType(kVideoStream, i, &type);
            if (FAILED(hr)) break;  // MF_E_NO_MORE_TYPES ends the list
            modes.push_back(ModeOf(type.Get()));
            Detail("Source: mode #%lu %s", static_cast<unsigned long>(i), Describe(type.Get()).c_str());
        }
        modes_ = modes;
        const std::optional<size_t> chosen = ChooseCaptureMode(modes, choice_.resolution);
        if (chosen && choice_.resolution) {
            const CaptureMode& picked = modes[*chosen];
            if (picked.width != choice_.resolution->width || picked.height != choice_.resolution->height) {
                Log("Source: chosen resolution %ux%u is not offered by this card; default mode", choice_.resolution->width,
                    choice_.resolution->height);
            }
        }
        if (!chosen) {
            Log("Source: the device offers no uncompressed 1920x1080 60 fps mode (YUY2 or NV12) among %zu modes",
                modes.size());
            return Failure(SourceStage::Mode, MF_E_INVALIDMEDIATYPE);
        }

        ComPtr<IMFMediaType> native;
        reader->GetNativeMediaType(kVideoStream, static_cast<DWORD>(*chosen), &native);
        reader->SetStreamSelection(kAllStreams, FALSE);
        reader->SetStreamSelection(kVideoStream, TRUE);
        hr = reader->SetCurrentMediaType(kVideoStream, nullptr, native.Get());
        if (FAILED(hr)) {
            Log("Source: SetCurrentMediaType failed hr=0x%08lX", static_cast<unsigned long>(hr));
            return Failure(SourceStage::Mode, hr);
        }
        ComPtr<IMFMediaType> current;
        hr = reader->GetCurrentMediaType(kVideoStream, &current);
        if (FAILED(hr)) {
            Log("Source: GetCurrentMediaType failed hr=0x%08lX", static_cast<unsigned long>(hr));
            return Failure(SourceStage::Mode, hr);
        }
        const CaptureMode mode = ModeOf(current.Get());
        if (!mode.format || mode.width == 0 || mode.height == 0) {
            Log("Source: the reader is not delivering the chosen format: %s", Describe(current.Get()).c_str());
            return Failure(SourceStage::Mode, MF_E_INVALIDMEDIATYPE);
        }
        const Colour colour = ColourOf(current.Get());
        Detail("Source: chosen mode #%zu %s", *chosen, Describe(current.Get()).c_str());
        Detail("Source: colour from the media type: range %s, matrix %s", RangeText(current.Get(), colour.range).c_str(),
               MatrixText(current.Get(), colour.matrix).c_str());

        // Opened goes out before the first read, whose failure may be reported
        // from the reader's thread at once.
        currentMode_ = mode;
        events({SourceEvent::Kind::Opened, SourceStage::FirstRead, 0});
        callback_->Begin(reader, std::move(sink), events, *mode.format, mode.width, mode.height, colour);
        hr = reader->ReadSample(kVideoStream, 0, nullptr, nullptr, nullptr, nullptr);
        if (FAILED(hr)) {
            Log("Source: first ReadSample failed hr=0x%08lX", static_cast<unsigned long>(hr));
            return Failure(SourceStage::FirstRead, hr);
        }
        Detail("Source: capturing (Source Reader, asynchronous, converters disabled)");
        return std::nullopt;
    }

    bool mfStarted_ = false;
    bool detail_ = true;
    std::optional<std::vector<std::wstring>> lastNames_;
    std::wstring deviceName_;
    std::wstring deviceId_;
    Choice choice_;
    bool choiceChanged_ = false;
    std::vector<CaptureMode> modes_;
    std::optional<CaptureMode> currentMode_;
    ComPtr<IMFMediaSource> device_;
    ComPtr<ReaderCallback> callback_;
};

}  // namespace

std::unique_ptr<Source> MakeMediaFoundationSource() {
    return std::make_unique<MediaFoundationSource>();
}

std::vector<std::wstring> ListCaptureDevices() {
    IMFActivate** devices = nullptr;
    UINT32 count = 0;
    if (FAILED(EnumerateVideoCaptureDevices(&devices, &count))) return {};
    std::vector<std::wstring> names = NamesOf(devices, count);
    for (UINT32 i = 0; i < count; i++) devices[i]->Release();
    CoTaskMemFree(devices);
    return names;
}

}  // namespace gp

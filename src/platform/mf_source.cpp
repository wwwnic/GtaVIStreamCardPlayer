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
#include <string>
#include <vector>

#include "core/capture_choice.h"
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

const char* RangeName(ColourRange range) {
    switch (range) {
    case ColourRange::Full: return "full";
    case ColourRange::Limited: return "limited";
    default: return "unspecified (treated as limited)";
    }
}

const char* MatrixName(ColourMatrix matrix) {
    switch (matrix) {
    case ColourMatrix::BT601: return "BT.601";
    case ColourMatrix::BT709: return "BT.709";
    default: return "unspecified (treated as BT.709)";
    }
}

// Receives samples from the Source Reader on a Media Foundation worker thread,
// copies each into a Frame and hands it to the sink.
class ReaderCallback final : public IMFSourceReaderCallback {
public:
    ReaderCallback() : flushed_(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {}

    void Begin(ComPtr<IMFSourceReader> reader, Source::FrameSink sink, PixelFormat format, uint32_t width,
               uint32_t height, Colour colour) {
        std::lock_guard lock(mutex_);
        reader_ = std::move(reader);
        sink_ = std::move(sink);
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
        if (FAILED(status)) {
            // Recovery is ticket 06; for now capture simply ends.
            Log("Source: read failed hr=0x%08lX; capture stopped", static_cast<unsigned long>(status));
            return S_OK;
        }
        if (flags & MF_SOURCE_READERF_ERROR) {
            Log("Source: stream error; capture stopped");
            return S_OK;
        }
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
            Log("Source: end of stream; capture stopped");
            return S_OK;
        }
        if (flags & (MF_SOURCE_READERF_NATIVEMEDIATYPECHANGED | MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED)) {
            Log("Source: media type changed mid-stream (flags=0x%lX)", static_cast<unsigned long>(flags));
        }
        if (sample) {
            if (FramePtr frame = CopySample(sample, arrival.QuadPart)) sink_(std::move(frame));
        }
        const HRESULT hr = reader_->ReadSample(kVideoStream, 0, nullptr, nullptr, nullptr, nullptr);
        if (FAILED(hr)) Log("Source: ReadSample failed hr=0x%08lX; capture stopped", static_cast<unsigned long>(hr));
        return S_OK;
    }
    STDMETHODIMP OnEvent(DWORD, IMFMediaEvent*) override { return S_OK; }
    STDMETHODIMP OnFlush(DWORD) override {
        SetEvent(flushed_);
        return S_OK;
    }

private:
    ~ReaderCallback() { CloseHandle(flushed_); }

    FramePtr CopySample(IMFSample* sample, int64_t arrivalQpc) {
        ComPtr<IMFMediaBuffer> buffer;
        if (FAILED(sample->ConvertToContiguousBuffer(&buffer))) return nullptr;
        const size_t byteSize = FrameByteSize(format_, width_, height_);
        BYTE* data = nullptr;
        DWORD length = 0;
        if (FAILED(buffer->Lock(&data, nullptr, &length))) return nullptr;
        std::shared_ptr<Frame> frame;
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
    PixelFormat format_ = PixelFormat::YUY2;
    uint32_t width_ = 0, height_ = 0;
    Colour colour_;
    FramePool pool_;
    std::atomic<uint64_t> sequence_{0};
    bool warnedShortBuffer_ = false;
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

    bool Start(FrameSink sink) override {
        if (!mfStarted_ || callback_) return false;
        if (!OpenDevice()) return false;
        if (!StartReading(std::move(sink))) {
            Stop();
            return false;
        }
        return true;
    }

    void Stop() override {
        if (callback_) {
            callback_->End();
            Log("Source: stopped after %llu frames", static_cast<unsigned long long>(callback_->FramesDelivered()));
            callback_.Reset();
        }
        if (device_) {
            device_->Shutdown();
            device_.Reset();
        }
    }

private:
    bool OpenDevice() {
        ComPtr<IMFAttributes> query;
        MFCreateAttributes(&query, 1);
        query->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
        IMFActivate** devices = nullptr;
        UINT32 count = 0;
        HRESULT hr = MFEnumDeviceSources(query.Get(), &devices, &count);
        if (FAILED(hr)) {
            Log("Source: MFEnumDeviceSources failed hr=0x%08lX", static_cast<unsigned long>(hr));
            return false;
        }
        std::vector<std::wstring> names(count);
        for (UINT32 i = 0; i < count; i++) {
            WCHAR* name = nullptr;
            UINT32 length = 0;
            if (SUCCEEDED(devices[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &name, &length)) && name) {
                names[i] = name;
            }
            CoTaskMemFree(name);
            Log("Source: capture device [%u] \"%s\"", i, ToUtf8(names[i]).c_str());
        }
        const std::optional<size_t> chosen = ChooseCaptureDevice(names);
        if (chosen) {
            Log("Source: chosen device [%zu] \"%s\"", *chosen, ToUtf8(names[*chosen]).c_str());
            hr = devices[*chosen]->ActivateObject(IID_PPV_ARGS(&device_));
            if (FAILED(hr)) {
                Log("Source: could not open the device hr=0x%08lX (another program may be using it)",
                    static_cast<unsigned long>(hr));
            }
        } else {
            Log("Source: no video capture device found");
        }
        for (UINT32 i = 0; i < count; i++) devices[i]->Release();
        CoTaskMemFree(devices);
        return device_ != nullptr;
    }

    bool StartReading(FrameSink sink) {
        callback_.Attach(new ReaderCallback());

        ComPtr<IMFAttributes> settings;
        MFCreateAttributes(&settings, 3);
        settings->SetUnknown(MF_SOURCE_READER_ASYNC_CALLBACK, callback_.Get());
        // No decoder, colour converter or video processor may be inserted:
        // frames must arrive exactly as the Capture Card produced them.
        settings->SetUINT32(MF_READWRITE_DISABLE_CONVERTERS, TRUE);
        settings->SetUINT32(MF_LOW_LATENCY, TRUE);
        ComPtr<IMFSourceReader> reader;
        HRESULT hr = MFCreateSourceReaderFromMediaSource(device_.Get(), settings.Get(), &reader);
        if (FAILED(hr)) {
            Log("Source: could not create the Source Reader hr=0x%08lX", static_cast<unsigned long>(hr));
            return false;
        }

        std::vector<CaptureMode> modes;
        for (DWORD i = 0;; i++) {
            ComPtr<IMFMediaType> type;
            hr = reader->GetNativeMediaType(kVideoStream, i, &type);
            if (FAILED(hr)) break;  // MF_E_NO_MORE_TYPES ends the list
            modes.push_back(ModeOf(type.Get()));
            Log("Source: mode #%lu %s", static_cast<unsigned long>(i), Describe(type.Get()).c_str());
        }
        const std::optional<size_t> chosen = ChooseCaptureMode(modes);
        if (!chosen) {
            Log("Source: the device offers no uncompressed 1920x1080 60 fps mode (YUY2 or NV12) among %zu modes",
                modes.size());
            return false;
        }

        ComPtr<IMFMediaType> native;
        reader->GetNativeMediaType(kVideoStream, static_cast<DWORD>(*chosen), &native);
        reader->SetStreamSelection(kAllStreams, FALSE);
        reader->SetStreamSelection(kVideoStream, TRUE);
        hr = reader->SetCurrentMediaType(kVideoStream, nullptr, native.Get());
        if (FAILED(hr)) {
            Log("Source: SetCurrentMediaType failed hr=0x%08lX", static_cast<unsigned long>(hr));
            return false;
        }
        ComPtr<IMFMediaType> current;
        hr = reader->GetCurrentMediaType(kVideoStream, &current);
        if (FAILED(hr)) {
            Log("Source: GetCurrentMediaType failed hr=0x%08lX", static_cast<unsigned long>(hr));
            return false;
        }
        const CaptureMode mode = ModeOf(current.Get());
        if (!mode.format || mode.width == 0 || mode.height == 0) {
            Log("Source: the reader is not delivering the chosen format: %s", Describe(current.Get()).c_str());
            return false;
        }
        const Colour colour = ColourOf(current.Get());
        Log("Source: chosen mode #%zu %s", *chosen, Describe(current.Get()).c_str());
        Log("Source: colour from the media type: range %s, matrix %s", RangeName(colour.range), MatrixName(colour.matrix));

        callback_->Begin(reader, std::move(sink), *mode.format, mode.width, mode.height, colour);
        hr = reader->ReadSample(kVideoStream, 0, nullptr, nullptr, nullptr, nullptr);
        if (FAILED(hr)) {
            Log("Source: first ReadSample failed hr=0x%08lX", static_cast<unsigned long>(hr));
            return false;
        }
        Log("Source: capturing (Source Reader, asynchronous, converters disabled)");
        return true;
    }

    bool mfStarted_ = false;
    ComPtr<IMFMediaSource> device_;
    ComPtr<ReaderCallback> callback_;
};

}  // namespace

std::unique_ptr<Source> MakeMediaFoundationSource() {
    return std::make_unique<MediaFoundationSource>();
}

}  // namespace gp

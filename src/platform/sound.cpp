#include <windows.h>

#include <audioclient.h>
#include <avrt.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>

#include "platform/sound.h"

#include <atomic>
#include <cmath>
#include <optional>
#include <thread>
#include <vector>

#include "core/capture_choice.h"
#include "core/pcm.h"
#include "core/sound_playout.h"
#include "platform/log.h"

using Microsoft::WRL::ComPtr;

namespace gp {

namespace {

// PKEY_Device_FriendlyName, spelled out to avoid needing INITGUID.
const PROPERTYKEY kFriendlyName = {{0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}}, 14};

unsigned long Hex(HRESULT hr) { return static_cast<unsigned long>(hr); }

std::wstring NameOf(IMMDevice* device) {
    std::wstring name;
    ComPtr<IPropertyStore> properties;
    if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &properties))) {
        PROPVARIANT value;
        PropVariantInit(&value);
        if (SUCCEEDED(properties->GetValue(kFriendlyName, &value)) && value.vt == VT_LPWSTR && value.pwszVal) {
            name = value.pwszVal;
        }
        PropVariantClear(&value);
    }
    return name;
}

std::wstring IdOf(IMMDevice* device) {
    std::wstring id;
    LPWSTR text = nullptr;
    if (SUCCEEDED(device->GetId(&text)) && text) id = text;
    CoTaskMemFree(text);
    return id;
}

// The endpoint's shared-mode format in the Viewer's terms; empty when it is
// one the Viewer cannot convert.
std::optional<PcmFormat> PcmFormatOf(const WAVEFORMATEX* wave) {
    WORD tag = wave->wFormatTag;
    if (tag == WAVE_FORMAT_EXTENSIBLE && wave->cbSize >= 22) {
        // The subformat GUID's first field is the plain format tag.
        tag = static_cast<WORD>(reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(wave)->SubFormat.Data1);
    }
    PcmFormat format;
    format.channels = wave->nChannels;
    format.rate = wave->nSamplesPerSec;
    if (tag == WAVE_FORMAT_IEEE_FLOAT && wave->wBitsPerSample == 32) {
        format.type = SampleType::Float32;
    } else if (tag == WAVE_FORMAT_PCM && wave->wBitsPerSample == 16) {
        format.type = SampleType::Int16;
    } else if (tag == WAVE_FORMAT_PCM && wave->wBitsPerSample == 24) {
        format.type = SampleType::Int24;
    } else if (tag == WAVE_FORMAT_PCM && wave->wBitsPerSample == 32) {
        format.type = SampleType::Int32;  // also 24 valid bits in 32
    } else {
        return std::nullopt;
    }
    if (format.channels == 0 || format.rate == 0 || wave->nBlockAlign != format.BytesPerFrame()) return std::nullopt;
    return format;
}

const char* TypeName(SampleType type) {
    switch (type) {
    case SampleType::Float32: return "32-bit float";
    case SampleType::Int16: return "16-bit";
    case SampleType::Int24: return "24-bit";
    case SampleType::Int32: return "32-bit";
    }
    return "?";
}

// One side (capture or render) opened in shared mode, event-driven.
struct Stream {
    ComPtr<IAudioClient> client;
    PcmFormat format;
    UINT32 bufferFrames = 0;
    HANDLE event = nullptr;

    ~Stream() {
        client.Reset();  // before the event it signals
        if (event) CloseHandle(event);
    }
};

// Opens the endpoint in shared mode with its own mix format (the Viewer
// converts), event-driven, using an IAudioClient3 low-latency period when the
// device offers one smaller than its default.
std::unique_ptr<Stream> OpenStream(IMMDevice* device, const char* side) {
    auto stream = std::make_unique<Stream>();
    ComPtr<IAudioClient3> client3;
    HRESULT hr = device->Activate(__uuidof(IAudioClient3), CLSCTX_ALL, nullptr, &client3);
    if (SUCCEEDED(hr)) {
        stream->client = client3;
    } else {
        hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &stream->client);
    }
    if (FAILED(hr)) {
        Log("Sound: %s endpoint could not be activated hr=0x%08lX", side, Hex(hr));
        return nullptr;
    }
    WAVEFORMATEX* mix = nullptr;
    hr = stream->client->GetMixFormat(&mix);
    if (FAILED(hr) || !mix) {
        Log("Sound: %s GetMixFormat failed hr=0x%08lX", side, Hex(hr));
        return nullptr;
    }
    const std::optional<PcmFormat> format = PcmFormatOf(mix);
    Log("Sound: %s format %lu Hz, %u channels, %u bits, tag 0x%04X", side, mix->nSamplesPerSec, mix->nChannels,
        mix->wBitsPerSample, mix->wFormatTag);
    if (!format) {
        Log("Sound: %s format is not one the Viewer can convert", side);
        CoTaskMemFree(mix);
        return nullptr;
    }
    stream->format = *format;

    bool initialized = false;
    if (client3) {
        UINT32 defaultPeriod = 0, fundamental = 0, minPeriod = 0, maxPeriod = 0;
        hr = client3->GetSharedModeEnginePeriod(mix, &defaultPeriod, &fundamental, &minPeriod, &maxPeriod);
        if (SUCCEEDED(hr)) {
            Log("Sound: %s engine periods default %.2f ms, min %.2f ms", side, defaultPeriod * 1000.0 / mix->nSamplesPerSec,
                minPeriod * 1000.0 / mix->nSamplesPerSec);
            if (minPeriod > 0 && minPeriod < defaultPeriod) {
                hr = client3->InitializeSharedAudioStream(AUDCLNT_STREAMFLAGS_EVENTCALLBACK, minPeriod, mix, nullptr);
                if (SUCCEEDED(hr)) {
                    initialized = true;
                    Log("Sound: %s uses the low-latency period", side);
                } else {
                    Log("Sound: %s low-latency period refused hr=0x%08lX; using the default", side, Hex(hr));
                    // A client that failed to initialise is not reused.
                    stream->client.Reset();
                    client3.Reset();
                    hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &stream->client);
                    if (FAILED(hr)) {
                        Log("Sound: %s endpoint could not be re-activated hr=0x%08lX", side, Hex(hr));
                        CoTaskMemFree(mix);
                        return nullptr;
                    }
                }
            }
        }
    }
    if (!initialized) {
        hr = stream->client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, 0, 0, mix, nullptr);
    }
    CoTaskMemFree(mix);
    if (FAILED(hr)) {
        Log("Sound: %s Initialize failed hr=0x%08lX", side, Hex(hr));
        return nullptr;
    }
    stream->client->GetBufferSize(&stream->bufferFrames);
    REFERENCE_TIME latency = 0;
    stream->client->GetStreamLatency(&latency);
    Log("Sound: %s buffer %u frames (%.1f ms), stream latency %.1f ms", side, stream->bufferFrames,
        stream->bufferFrames * 1000.0 / stream->format.rate, latency / 10000.0);
    stream->event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    hr = stream->client->SetEventHandle(stream->event);
    if (FAILED(hr)) {
        Log("Sound: %s SetEventHandle failed hr=0x%08lX", side, Hex(hr));
        return nullptr;
    }
    return stream;
}

// Real-time priority for the two audio threads.
class AudioThreadScope {
public:
    AudioThreadScope() {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        DWORD task = 0;
        mmcss_ = AvSetMmThreadCharacteristicsW(L"Pro Audio", &task);
    }
    ~AudioThreadScope() {
        if (mmcss_) AvRevertMmThreadCharacteristics(mmcss_);
        CoUninitialize();
    }

private:
    HANDLE mmcss_ = nullptr;
};

// Wakes the control thread when the Windows default output changes.
class DefaultOutputWatcher final : public IMMNotificationClient {
public:
    explicit DefaultOutputWatcher(HANDLE wake) : wake_(wake) {}

    STDMETHODIMP OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR) override {
        if (flow == eRender && role == eConsole) SetEvent(wake_);
        return S_OK;
    }
    STDMETHODIMP OnDeviceAdded(LPCWSTR) override { return S_OK; }
    STDMETHODIMP OnDeviceRemoved(LPCWSTR) override { return S_OK; }
    STDMETHODIMP OnDeviceStateChanged(LPCWSTR, DWORD) override { return S_OK; }
    STDMETHODIMP OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) override { return S_OK; }

    STDMETHODIMP QueryInterface(REFIID riid, void** object) override {
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IMMNotificationClient)) {
            *object = static_cast<IMMNotificationClient*>(this);
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

private:
    ~DefaultOutputWatcher() = default;
    LONG references_ = 1;
    HANDLE wake_;
};

}  // namespace

// Threads: the capture thread pushes into the playout; the render thread
// pulls from it; the control thread re-opens the render side when the
// default output changes or fails, and writes the periodic log line. Only
// the control thread (or Start/Stop while it is not running) opens and
// closes the render side.
struct Sound::Impl {
    ComPtr<IMMDeviceEnumerator> enumerator;
    ComPtr<DefaultOutputWatcher> watcher;
    std::unique_ptr<SoundPlayout> playout;

    std::unique_ptr<Stream> capture;
    std::thread captureThread;
    std::atomic<bool> capturing{false};
    std::atomic<uint32_t> discontinuities{0};

    std::unique_ptr<Stream> render;
    std::wstring renderId;
    std::thread renderThread;
    std::atomic<bool> rendering{false};
    std::atomic<bool> renderFailed{false};

    std::thread controlThread;
    HANDLE stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);

    ~Impl() {
        CloseHandle(stop);
        CloseHandle(wake);
    }

    std::optional<std::wstring> ChooseCaptureEndpoint(const std::wstring& cardName, ComPtr<IMMDevice>& chosen) {
        ComPtr<IMMDeviceCollection> endpoints;
        HRESULT hr = enumerator->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &endpoints);
        UINT count = 0;
        if (SUCCEEDED(hr)) endpoints->GetCount(&count);
        Log("Sound: %u audio capture endpoints", count);
        std::vector<std::wstring> names(count);
        for (UINT i = 0; i < count; i++) {
            ComPtr<IMMDevice> device;
            if (SUCCEEDED(endpoints->Item(i, &device))) names[i] = NameOf(device.Get());
            Log("Sound: capture endpoint [%u] \"%s\"", i, ToUtf8(names[i]).c_str());
        }
        const std::optional<SoundEndpointChoice> choice = ChooseSoundEndpoint(names, cardName);
        if (!choice) {
            Log("Sound: no capture endpoint matches the Capture Card \"%s\"; no sound", ToUtf8(cardName).c_str());
            return std::nullopt;
        }
        endpoints->Item(static_cast<UINT>(choice->index), &chosen);
        if (choice->byCardName) {
            Log("Sound: chosen capture endpoint [%zu] \"%s\" (matches the Capture Card)", choice->index,
                ToUtf8(names[choice->index]).c_str());
        } else {
            Log("Sound: no endpoint matches the Capture Card \"%s\"; fallback chose [%zu] \"%s\"",
                ToUtf8(cardName).c_str(), choice->index, ToUtf8(names[choice->index]).c_str());
        }
        return names[choice->index];
    }

    void CaptureLoop() {
        AudioThreadScope scope;
        ComPtr<IAudioCaptureClient> service;
        capture->client->GetService(IID_PPV_ARGS(&service));
        const PcmFormat format = capture->format;
        std::vector<float> stereo(2 * capture->bufferFrames);
        while (capturing) {
            WaitForSingleObject(capture->event, 200);  // a stalled stream just times out
            for (;;) {
                UINT32 packet = 0;
                HRESULT hr = service->GetNextPacketSize(&packet);
                if (FAILED(hr)) {
                    // Losing the card is ticket 06; for now the sound stops.
                    Log("Sound: capture failed hr=0x%08lX; sound stopped", Hex(hr));
                    capturing = false;
                    break;
                }
                if (packet == 0) break;
                BYTE* data = nullptr;
                UINT32 frames = 0;
                DWORD flags = 0;
                if (FAILED(service->GetBuffer(&data, &frames, &flags, nullptr, nullptr))) break;
                if (stereo.size() < 2 * static_cast<size_t>(frames)) stereo.resize(2 * static_cast<size_t>(frames));
                if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
                    std::fill(stereo.begin(), stereo.begin() + 2 * frames, 0.0f);
                } else {
                    ToStereo(data, frames, format, stereo.data());
                }
                if (flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) discontinuities++;
                service->ReleaseBuffer(frames);
                playout->Push(stereo.data(), frames);
            }
        }
    }

    // Writes whatever room the output buffer has. False when the device is gone.
    bool FillRender(IAudioRenderClient* service, std::vector<float>& stereo) {
        UINT32 padding = 0;
        HRESULT hr = render->client->GetCurrentPadding(&padding);
        if (FAILED(hr)) {
            Log("Sound: output failed hr=0x%08lX; re-opening the default output", Hex(hr));
            return false;
        }
        const UINT32 room = render->bufferFrames - padding;
        if (room == 0) return true;
        BYTE* data = nullptr;
        hr = service->GetBuffer(room, &data);
        if (FAILED(hr)) {
            Log("Sound: output GetBuffer failed hr=0x%08lX; re-opening the default output", Hex(hr));
            return false;
        }
        playout->Pull(stereo.data(), room);
        FromStereo(stereo.data(), room, render->format, data);
        service->ReleaseBuffer(room, 0);
        return true;
    }

    void RenderLoop(ComPtr<IAudioRenderClient> service) {
        AudioThreadScope scope;
        std::vector<float> stereo(2 * render->bufferFrames);
        while (rendering) {
            WaitForSingleObject(render->event, 200);
            if (!rendering) break;
            if (!FillRender(service.Get(), stereo)) {
                renderFailed = true;
                SetEvent(wake);
                break;
            }
        }
    }

    // Opens the current Windows default output and starts playing to it.
    void OpenRender() {
        ComPtr<IMMDevice> device;
        HRESULT hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
        if (FAILED(hr)) {
            Log("Sound: no default output device (hr=0x%08lX); waiting for one", Hex(hr));
            return;
        }
        Log("Sound: default output \"%s\"", ToUtf8(NameOf(device.Get())).c_str());
        std::unique_ptr<Stream> stream = OpenStream(device.Get(), "output");
        if (!stream) return;
        ComPtr<IAudioRenderClient> service;
        hr = stream->client->GetService(IID_PPV_ARGS(&service));
        if (FAILED(hr)) {
            Log("Sound: output GetService failed hr=0x%08lX", Hex(hr));
            return;
        }
        render = std::move(stream);
        renderId = IdOf(device.Get());
        renderFailed = false;
        playout->SetOutputRate(render->format.rate);
        std::vector<float> stereo(2 * render->bufferFrames);
        FillRender(service.Get(), stereo);  // silence while priming; avoids a glitch at start
        hr = render->client->Start();
        if (FAILED(hr)) {
            Log("Sound: output Start failed hr=0x%08lX", Hex(hr));
            render.reset();
            renderId.clear();
            return;
        }
        rendering = true;
        renderThread = std::thread([this, service] { RenderLoop(service); });
        Log("Sound: playing to the default output");
    }

    void CloseRender() {
        rendering = false;
        if (render) SetEvent(render->event);
        if (renderThread.joinable()) renderThread.join();
        if (render) render->client->Stop();
        render.reset();
        renderId.clear();
    }

    // The default output changed, or the current one failed.
    void FollowDefaultOutput() {
        if (render && !renderFailed) {
            ComPtr<IMMDevice> device;
            if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device)) &&
                IdOf(device.Get()) == renderId) {
                return;  // still the same device
            }
        }
        Log("Sound: default output changed; switching");
        CloseRender();
        OpenRender();
    }

    void LogStats(double seconds) {
        const SoundPlayout::Stats stats = playout->TakeStats();
        Log("Sound: fill avg %.1f ms (min %.1f, max %.1f), target %.0f ms, rate %+.0f ppm, captured %.0f/s, played %.0f/s, "
            "underruns %u, overruns %u, discontinuities %u%s",
            stats.fillAverageSeconds * 1000, stats.fillMinSeconds * 1000, stats.fillMaxSeconds * 1000,
            playout->TargetFillSeconds() * 1000, (stats.rate - 1.0) * 1e6, stats.framesPushed / seconds,
            stats.framesPulled / seconds, stats.underruns, stats.overruns, discontinuities.exchange(0),
            render ? "" : " (no output device)");
    }

    void ControlLoop() {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        ULONGLONG lastStats = GetTickCount64();
        ULONGLONG lastRetry = lastStats;
        int statsLogged = 0;
        const HANDLE events[] = {stop, wake};
        for (;;) {
            const DWORD woke = WaitForMultipleObjects(2, events, FALSE, 250);
            if (woke == WAIT_OBJECT_0) break;
            const ULONGLONG now = GetTickCount64();
            if (woke == WAIT_OBJECT_0 + 1) {
                FollowDefaultOutput();
            } else if (!render && now - lastRetry >= 2000) {
                lastRetry = now;
                OpenRender();  // the default output was missing; try again
            }
            const ULONGLONG interval = statsLogged < 10 ? 1000 : 60000;
            if (now - lastStats >= interval) {
                LogStats((now - lastStats) / 1000.0);
                lastStats = now;
                statsLogged++;
            }
        }
        CoUninitialize();
    }
};

Sound::Sound() = default;

Sound::~Sound() { Stop(); }

bool Sound::Start(const std::wstring& cardName) {
    if (impl_) return false;
    auto impl = std::make_unique<Impl>();
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&impl->enumerator));
    if (FAILED(hr)) {
        Log("Sound: no device enumerator hr=0x%08lX; no sound", Hex(hr));
        return false;
    }
    ComPtr<IMMDevice> device;
    if (!impl->ChooseCaptureEndpoint(cardName, device) || !device) return false;
    impl->capture = OpenStream(device.Get(), "capture");
    if (!impl->capture) {
        Log("Sound: the capture endpoint could not be opened; no sound");
        return false;
    }
    // The output rate is a placeholder until an output device is open.
    impl->playout = std::make_unique<SoundPlayout>(impl->capture->format.rate, impl->capture->format.rate);
    Log("Sound: buffer target %.0f ms of fill, overrun above %.0f ms more; drift correction up to %.0f ppm",
        SoundPlayout::kTargetFillSeconds * 1000, SoundPlayout::kOverrunMarginSeconds * 1000,
        DriftCorrection::kMaxCorrection * 1e6);
    impl->playout->SetGain(static_cast<float>(GainOf(settings_)));
    impl->playout->SetAudioDelay(settings_.audioDelayMs / 1000.0);

    impl->watcher.Attach(new DefaultOutputWatcher(impl->wake));
    hr = impl->enumerator->RegisterEndpointNotificationCallback(impl->watcher.Get());
    if (FAILED(hr)) {
        Log("Sound: cannot watch for output changes hr=0x%08lX", Hex(hr));
        impl->watcher.Reset();
    }
    impl->OpenRender();

    hr = impl->capture->client->Start();
    if (FAILED(hr)) {
        Log("Sound: capture Start failed hr=0x%08lX; no sound", Hex(hr));
        impl_ = std::move(impl);
        Stop();
        return false;
    }
    impl->capturing = true;
    Impl* raw = impl.get();
    impl->captureThread = std::thread([raw] { raw->CaptureLoop(); });
    impl->controlThread = std::thread([raw] { raw->ControlLoop(); });
    impl_ = std::move(impl);
    return true;
}

void Sound::Stop() {
    if (!impl_) return;
    Impl& impl = *impl_;
    SetEvent(impl.stop);
    if (impl.controlThread.joinable()) impl.controlThread.join();
    if (impl.watcher) impl.enumerator->UnregisterEndpointNotificationCallback(impl.watcher.Get());
    impl.CloseRender();
    impl.capturing = false;
    if (impl.capture) SetEvent(impl.capture->event);
    if (impl.captureThread.joinable()) impl.captureThread.join();
    if (impl.capture) impl.capture->client->Stop();
    Log("Sound: stopped");
    impl_.reset();
}

void Sound::SetVolume(int percent) {
    settings_.volumePercent = ClampVolume(percent);
    ApplySettings();
}

void Sound::SetMuted(bool muted) {
    settings_.muted = muted;
    ApplySettings();
}

void Sound::SetAudioDelayMs(int ms) {
    settings_.audioDelayMs = ClampAudioDelay(ms);
    ApplySettings();
}

void Sound::ApplySettings() {
    if (!impl_ || !impl_->playout) {
        Log("Audio: volume %d%%%s, Audio Delay %d ms (no sound playing)", settings_.volumePercent,
            settings_.muted ? " muted" : "", settings_.audioDelayMs);
        return;
    }
    impl_->playout->SetGain(static_cast<float>(GainOf(settings_)));
    impl_->playout->SetAudioDelay(settings_.audioDelayMs / 1000.0);
    Log("Audio: volume %d%%%s, Audio Delay %d ms, buffer target now %.0f ms", settings_.volumePercent,
        settings_.muted ? " muted" : "", settings_.audioDelayMs, impl_->playout->TargetFillSeconds() * 1000);
}

}  // namespace gp

#include "platform/presenter.h"
#include "platform/sharp_upscale_shader.h"

#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi1_5.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <optional>
#include <thread>

#include "core/colour.h"
#include "core/frame_schedule.h"
#include "core/latest_slot.h"
#include "core/picture_fit.h"
#include "core/shown_texts.h"
#include "core/target_schedule.h"
#include "core/ladder_depth.h"
#include "core/vsync_clock.h"
#include "platform/frame_generator.h"
#include "platform/log.h"
#include "platform/text_overlay.h"

using Microsoft::WRL::ComPtr;

namespace gp {

namespace {

// Two passes. Convert: the captured YUV frame becomes an RGB picture at the
// Feed's own size, pixel for pixel. Show: that picture is drawn into the
// Video Area with smooth scaling.
const char kShaders[] = R"hlsl(
struct VertexOut { float4 position : SV_Position; float2 uv : TEXCOORD0; };

// One triangle covering the whole viewport; no vertex buffer.
VertexOut FullScreen(uint id : SV_VertexID) {
    VertexOut o;
    o.uv = float2((id << 1) & 2, id & 2);
    o.position = float4(o.uv * float2(2, -2) + float2(-1, 1), 0, 1);
    return o;
}

cbuffer Conversion : register(b0) {
    float4 toRed;            // rgb = rows * (y, u, v, 1); built by MakeYuvToRgb
    float4 toGreen;          // from the capture mode's range and matrix
    float4 toBlue;
    uint format;             // 0 = YUY2, 1 = NV12
    int lastChromaColumn;    // YUY2: index of the last U/V pair in a row
    uint2 unused;
};
Texture2D<float4> plane0 : register(t0);   // YUY2: one texel = Y0 U Y1 V.  NV12: Y
Texture2D<float4> plane1 : register(t1);   // NV12: UV at half size

float4 Convert(VertexOut input) : SV_Target {
    int2 p = int2(input.position.xy);
    float y;
    float2 uv;
    if (format == 0) {
        int column = p.x >> 1;
        float4 here = plane0.Load(int3(column, p.y, 0));
        if (p.x & 1) {
            // U and V are sampled at the even pixel; an odd pixel lies halfway to the next pair.
            float4 next = plane0.Load(int3(min(column + 1, lastChromaColumn), p.y, 0));
            y = here.b;
            uv = 0.5 * (here.ga + next.ga);
        } else {
            y = here.r;
            uv = here.ga;
        }
    } else {
        y = plane0.Load(int3(p, 0)).r;
        uv = plane1.Load(int3(p >> 1, 0)).rg;
    }
    float4 yuv1 = float4(y, uv, 1);
    return float4(saturate(float3(dot(toRed, yuv1), dot(toGreen, yuv1), dot(toBlue, yuv1))), 1);
}

Texture2D<float4> picture : register(t2);
SamplerState smooth : register(s0);

float4 Show(VertexOut input) : SV_Target {
    return float4(picture.Sample(smooth, input.uv).rgb, 1);
}
)hlsl";

struct ConversionConstants {
    float rows[3][4];
    uint32_t format;
    int32_t lastChromaColumn;
    uint32_t unused[2];
};

int64_t QpcFrequency() {
    static const int64_t frequency = [] {
        LARGE_INTEGER ticks;
        QueryPerformanceFrequency(&ticks);
        return ticks.QuadPart;
    }();
    return frequency;
}

double QpcToMs(int64_t ticks) { return static_cast<double>(ticks) * 1000.0 / static_cast<double>(QpcFrequency()); }

const ULONGLONG kRebuildEveryMs = 1000;  // device rebuild attempts are at least this far apart

// A timed frame is drawn this long before it is due at most (what the
// previous drawing took), so that its present lands on the due time.
const int kLongestDrawLeadMs = 2;

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION  // Windows SDK before 10.0.17134
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

int64_t QpcNow() {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return now.QuadPart;
}

}  // namespace

class Presenter::Impl {
public:
    ~Impl() { Stop(); }

    bool Start(HWND window) {
        if (thread_.joinable()) return false;
        window_ = window;
        wake_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!wake_) {
            Log("Presenter: no wake event (error %lu)", GetLastError());
            return false;
        }
        // For the due times of a timed sequence. High resolution (Windows 10
        // 1803 on); an ordinary timer otherwise, exact to a millisecond or so.
        timer_ = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
        const bool highResolution = timer_ != nullptr;
        if (!timer_) timer_ = CreateWaitableTimerW(nullptr, FALSE, nullptr);
        Log("Presenter: timer for timed frames: %s", highResolution ? "high resolution" : timer_ ? "ordinary" : "none");
        stop_ = false;
        // No device now (a graphics driver being replaced at launch): the
        // thread keeps trying, exactly as after a device loss.
        deviceLost_ = !CreateDevice();
        if (deviceLost_) {
            Log("Presenter: no device at start; trying again once a second");
            rebuildAt_ = GetTickCount64() + kRebuildEveryMs;
        }
        resized_ = true;  // first pass paints the Video Area black
        SetEvent(wake_);
        thread_ = std::thread([this] { Run(); });
        return true;
    }

    void Stop() {
        if (thread_.joinable()) {
            stop_ = true;
            SetEvent(wake_);
            // DXGI can send messages to the window's thread from inside
            // Present or ResizeBuffers, so keep answering them while waiting.
            // A quit seen here belongs to the caller's message loop: posted again.
            HANDLE thread = thread_.native_handle();
            std::optional<int> quit;
            while (MsgWaitForMultipleObjects(1, &thread, FALSE, INFINITE, QS_ALLINPUT) != WAIT_OBJECT_0) {
                MSG message;
                while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                    if (message.message == WM_QUIT) {
                        quit = static_cast<int>(message.wParam);
                    } else {
                        DispatchMessageW(&message);
                    }
                }
            }
            thread_.join();
            if (quit) PostQuitMessage(*quit);
        }
        slot_.Take();
        lastFrame_.reset();
        ReleaseDevice();
        if (wake_) CloseHandle(wake_);
        wake_ = nullptr;
        if (timer_) CloseHandle(timer_);
        timer_ = nullptr;
    }

    void Submit(TimedFrame frame, uint32_t feedGeneration) {
        if (!wake_ || !frame.frame) return;
        if (slot_.Put({std::move(frame.frame), feedGeneration})) {
            std::lock_guard lock(statsMutex_);
            stats_.replaced++;
        }
        SetEvent(wake_);
    }

    void VideoAreaResized() {
        if (!wake_) return;
        resized_ = true;
        SetEvent(wake_);
    }

    PresentationInfo Presentation() {
        std::lock_guard lock(statsMutex_);
        auto info = presentation_;
        info.shownRate = presentedFrames_.RateAt(GetTickCount64());
        info.realRate = realPresentedFrames_.RateAt(GetTickCount64());
        return info;
    }

    Stats TakeStats() {
        std::lock_guard lock(statsMutex_);
        Stats taken = stats_;
        if (taken.presented) taken.averageLatencyMs = latencySumMs_ / taken.presented;
        stats_ = {};
        latencySumMs_ = 0.0;
        return taken;
    }

    void ShowText(TextPlace place, VideoAreaText text, int durationMs) {
        bool changed = false;
        {
            std::lock_guard lock(textMutex_);
            changed = texts_.Show(place, std::move(text), durationMs, GetTickCount64());
        }
        // The same text again (a held key) only moves its end: the presenting
        // thread wakes at the old end, finds nothing due and waits again.
        if (changed) Repaint();
    }

    void ShowTextWithNextFrame(TextPlace place, VideoAreaText text) {
        std::lock_guard lock(textMutex_);
        texts_.ShowWithNextFrame(place, std::move(text));
    }

    void ShowPicture(bool visible) {
        if (pictureVisible_.exchange(visible) != visible) Repaint();
    }

    // No repaint: an extra present would count as a game frame for Lossless Scaling.
    void SetSharpUpscale(bool on) { sharpUpscale_ = on; }
    void SetVsync(bool on) { vsync_ = on; }

    void SetFrameGenerator(std::shared_ptr<FrameGenerator> generator, bool target) {
        {
            std::lock_guard lock(generatorMutex_);
            wantedGenerator_ = std::move(generator);
            wantedTarget_ = target && wantedGenerator_;
        }
        generatorChanged_ = true;
        if (wake_) SetEvent(wake_);
    }

    TimedStats TakeTimedStats() {
        std::lock_guard lock(statsMutex_);
        TimedStats taken;
        taken.frames = timedStats_;
        if (taken.frames.timed) taken.latenessAverageMs = QpcToMs(taken.frames.latenessSumTicks) / taken.frames.timed;
        taken.latenessWorstMs = QpcToMs(taken.frames.latenessWorstTicks);
        taken.presentCallWorstMs = QpcToMs(presentCallWorstTicks_);
        timedStats_ = {};
        presentCallWorstTicks_ = 0;
        return taken;
    }

private:
    void Repaint() {
        repaint_ = true;
        if (wake_) SetEvent(wake_);
    }

    // ---- setup (window's thread at Start; presenting thread after a device loss) ----

    bool CreateDevice() {
        if (!CreateDeviceAndSwapChain() || !CreatePipeline()) {
            ReleaseDevice();
            return false;
        }
        overlay_.Attach(device_.Get());  // without it the picture still shows
        return true;
    }

    bool CreateDeviceAndSwapChain() {
        D3D_FEATURE_LEVEL level = {};
        HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                       nullptr, 0, D3D11_SDK_VERSION, &device_, &level, &context_);
        if (FAILED(hr)) {
            Log("Presenter: D3D11CreateDevice failed hr=0x%08lX", static_cast<unsigned long>(hr));
            return false;
        }
        ComPtr<IDXGIDevice> dxgiDevice;
        ComPtr<IDXGIAdapter> adapter;
        ComPtr<IDXGIFactory2> factory;
        device_.As(&dxgiDevice);
        dxgiDevice->GetAdapter(&adapter);
        hr = adapter->GetParent(IID_PPV_ARGS(&factory));
        if (FAILED(hr)) {
            Log("Presenter: no DXGI factory hr=0x%08lX", static_cast<unsigned long>(hr));
            return false;
        }
        DXGI_ADAPTER_DESC adapterInfo = {};
        adapter->GetDesc(&adapterInfo);
        Log("Presenter: Direct3D 11 on \"%s\", feature level 0x%X", ToUtf8(adapterInfo.Description).c_str(),
            static_cast<unsigned>(level));

        BOOL tearing = FALSE;
        ComPtr<IDXGIFactory5> factory5;
        if (SUCCEEDED(factory.As(&factory5))) {
            if (FAILED(factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tearing, sizeof tearing))) {
                tearing = FALSE;
            }
        }
        tearing_ = tearing != FALSE;

        swapChainFlags_ = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
        if (tearing_) swapChainFlags_ |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
        DXGI_SWAP_CHAIN_DESC1 description = {};  // width and height 0: the window's client size
        description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        description.SampleDesc.Count = 1;
        description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        description.BufferCount = 2;
        description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        // While a resize is catching up, keep the old picture unscaled rather than stretched.
        description.Scaling = DXGI_SCALING_NONE;
        description.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
        description.Flags = swapChainFlags_;
        ComPtr<IDXGISwapChain1> swapChain;
        hr = factory->CreateSwapChainForHwnd(device_.Get(), window_, &description, nullptr, nullptr, &swapChain);
        if (FAILED(hr)) {
            Log("Presenter: CreateSwapChainForHwnd failed hr=0x%08lX", static_cast<unsigned long>(hr));
            return false;
        }
        factory->MakeWindowAssociation(window_, DXGI_MWA_NO_ALT_ENTER);  // never exclusive fullscreen
        if (FAILED(swapChain.As(&swapChain_))) {
            Log("Presenter: IDXGISwapChain2 not available");
            return false;
        }
        swapChain_->SetMaximumFrameLatency(1);
        latencyWaitable_ = swapChain_->GetFrameLatencyWaitableObject();
        if (!CreateBackBufferView()) return false;
        Log("Presenter: swap chain FLIP_DISCARD, 2 buffers, %ux%u, waitable object, max frame latency 1, tearing %s",
            backWidth_, backHeight_, tearing_ ? "allowed" : "NOT supported");
        return true;
    }

    bool CreatePipeline() {
        ComPtr<ID3DBlob> vertexCode = Compile("FullScreen", "vs_5_0");
        ComPtr<ID3DBlob> convertCode = Compile("Convert", "ps_5_0");
        ComPtr<ID3DBlob> showCode = Compile("Show", "ps_5_0");
        if (!vertexCode || !convertCode || !showCode) return false;
        device_->CreateVertexShader(vertexCode->GetBufferPointer(), vertexCode->GetBufferSize(), nullptr, &vertexShader_);
        device_->CreatePixelShader(convertCode->GetBufferPointer(), convertCode->GetBufferSize(), nullptr, &convertShader_);
        device_->CreatePixelShader(showCode->GetBufferPointer(), showCode->GetBufferSize(), nullptr, &showShader_);

        D3D11_BUFFER_DESC constants = {};
        constants.ByteWidth = sizeof(ConversionConstants);
        constants.Usage = D3D11_USAGE_DEFAULT;
        constants.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        device_->CreateBuffer(&constants, nullptr, &constants_);

        D3D11_SAMPLER_DESC sampler = {};
        sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler.MaxLOD = D3D11_FLOAT32_MAX;
        device_->CreateSamplerState(&sampler, &sampler_);
        return vertexShader_ && convertShader_ && showShader_ && constants_ && sampler_;
    }

    ComPtr<ID3DBlob> Compile(const char* entry, const char* target,
                             const char* source = kShaders, size_t size = sizeof kShaders - 1) {
        ComPtr<ID3DBlob> code, errors;
        const HRESULT hr = D3DCompile(source, size, "presenter", nullptr, nullptr, entry, target,
                                      D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
        if (FAILED(hr)) {
            Log("Presenter: shader %s failed to compile hr=0x%08lX: %s", entry, static_cast<unsigned long>(hr),
                errors ? static_cast<const char*>(errors->GetBufferPointer()) : "");
            return nullptr;
        }
        return code;
    }

    bool CreateBackBufferView() {
        ComPtr<ID3D11Texture2D> backBuffer;
        HRESULT hr = swapChain_->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
        if (SUCCEEDED(hr)) hr = device_->CreateRenderTargetView(backBuffer.Get(), nullptr, &backBufferView_);
        if (FAILED(hr)) {
            Log("Presenter: no back buffer view hr=0x%08lX", static_cast<unsigned long>(hr));
            return false;
        }
        D3D11_TEXTURE2D_DESC description = {};
        backBuffer->GetDesc(&description);
        backWidth_ = description.Width;
        backHeight_ = description.Height;
        return true;
    }

    void ReleaseDevice() {
        // What the generator made is on this device, and the Unique Frame
        // before a rebuild is not one to generate from.
        if (generator_) generator_->ReleaseDevice();
        SetRunningFactor(0);
        schedule_.Forget();
        targetSchedule_.Forget();
        vsyncClock_.Reset();  // the statistics belong to the swap chain going away
        vsyncReady_ = false;
        overlay_.Detach();
        if (latencyWaitable_) CloseHandle(latencyWaitable_);
        latencyWaitable_ = nullptr;
        if (context_) {
            context_->ClearState();
            context_->Flush();
        }
        plane0_.Reset(); plane0View_.Reset(); plane1_.Reset(); plane1View_.Reset();
        picture_.Reset(); pictureTarget_.Reset(); pictureView_.Reset();
        havePicture_ = false;
        UpscaleUsage(false, scaledWidth_, scaledHeight_);
        scaledTarget_.Reset(); scaledView_.Reset();
        scaledWidth_ = scaledHeight_ = 0;
        easuShader_.Reset(); rcasShader_.Reset();
        triedUpscaleShaders_ = false;
        vertexShader_.Reset(); convertShader_.Reset(); showShader_.Reset();
        constants_.Reset(); sampler_.Reset();
        backBufferView_.Reset();
        swapChain_.Reset();
        if (context_) {
            context_->ClearState();
            context_->Flush();
            context_.Reset();
        }
        if (device_) {
            const unsigned long remaining = device_.Reset();
            Log("Presenter: device released (references still held elsewhere: %lu)", remaining);
        }
    }

    // ---- presenting thread ----

    void Run() {
        Log("Presenter: thread started");
        while (true) {
            Wait();
            if (stop_) break;
            if (deviceLost_) {
                // Graphics driver reset or GPU removed (it can happen around
                // sleep): build everything again, once a second until it
                // works. By the clock: frames and texts keep waking this
                // thread meanwhile, and the newest frame waits in the slot.
                if (RebuildWaitMs() > 0) continue;
                rebuildAt_ = GetTickCount64() + kRebuildEveryMs;
                ReleaseDevice();
                if (!CreateDevice()) continue;
                Log("Presenter: device recreated");
                deviceLost_ = false;
                resized_ = true;
                // On a still picture no new frame is coming: show the last one again.
                if (lastFrame_) ConvertToPicture(*lastFrame_);
            }

            AdoptGenerator();
            if (generator_ && target_) {  // Target: its own pass, nothing below runs
                TargetPass();
                continue;
            }

            // Both, always: a repaint must not draw a text whose time is up.
            const bool repaint = repaint_.exchange(false);
            const bool ended = EndExpiredTexts();
            if (repaint || ended) drawWaiting_ = true;
            if (resized_.exchange(false)) resizeWaiting_ = true;
            // Without a frame generator the schedule never holds anything:
            // these are drawn and presented on this very pass. With one, they
            // wait for the next frame of the schedule, which is never far,
            // so no wake-up of this thread adds a present between two frames.
            bool draw = false;
            if (!schedule_.HoldsRedraw(ScheduleNow())) {
                draw = drawWaiting_;
                drawWaiting_ = false;
                if (resizeWaiting_) {
                    if (!ApplyResize()) continue;
                    draw = true;  // also repaints when no new frame is coming
                }
            }
            auto next = slot_.Take();
            const bool converted = next && ConvertToPicture(*next->frame);
            if (converted) {
                lastFrame_ = next->frame;
                if (generator_) BeginSequence(*next->frame, next->feedGeneration);
                if (!generator_) draw = true;
            }
            // The frame of the timed sequence that is due, if any. Other
            // wake-ups find none due and change nothing: the wait starts
            // again for the same due time, read from the clock.
            const std::optional<int> due = generator_ ? schedule_.DueFrame(ScheduleNow()) : std::nullopt;
            ID3D11ShaderResourceView* picture = pictureView_.Get();
            bool uniqueFrame = converted;  // this present shows a Unique Frame for the first time
            if (due) {
                if (resizeWaiting_ && !ApplyResize()) continue;
                draw = true;
                drawWaiting_ = false;
                uniqueFrame = *due == schedule_.Generated();
                if (!uniqueFrame) {
                    ID3D11ShaderResourceView* generated = generator_->Generated(*due);
                    if (generated) picture = generated;
                }
            } else if (generator_) {
                uniqueFrame = false;
            }
            if (!draw || !backBufferView_) continue;

            const int64_t drawBegan = QpcNow();
            const bool presented = ShowAndPresent(picture);
            if (presented) {
                std::lock_guard lock(statsMutex_);
                presentedFrames_.Presented(GetTickCount64());
            }
            if (presented && generator_) generator_->NotePresented();
            if (due) {
                schedule_.Presented(presentBegan_, presented);
                drawLead_ = std::min(presentBegan_ - drawBegan, QpcFrequency() * kLongestDrawLeadMs / 1000);
                std::lock_guard lock(statsMutex_);
                timedStats_.Add(schedule_.TakeStats());
                presentCallWorstTicks_ = std::max(presentCallWorstTicks_, presentTook_);
            }
            // Counted as shown only if it was converted and the present went through.
            if (presented && uniqueFrame && lastFrame_) {
                const double latencyMs = QpcToMs(QpcNow() - lastFrame_->arrivalQpc);
                std::lock_guard lock(statsMutex_);
                realPresentedFrames_.Presented(GetTickCount64());
                stats_.presented++;
                latencySumMs_ += latencyMs;
                if (latencyMs > stats_.worstLatencyMs) stats_.worstLatencyMs = latencyMs;
            }
        }
        Log("Presenter: thread stopped");
    }

    // Sleeps until there may be something to do: a wake-up (a frame, a
    // resize, a text, stop), the end of a brief text, or the due time of the
    // next frame of a timed sequence. That due time is exact (the timer) and
    // set again from the clock before every sleep, so being woken for
    // anything else neither ends the wait for it nor moves it.
    void Wait() {
        if (deviceLost_) {
            WaitForSingleObject(wake_, RebuildWaitMs());
            return;
        }
        if (generator_ && target_) {
            WaitForVsync();
            return;
        }
        const DWORD textMs = NextTextEndMs();
        std::optional<int64_t> ticks;
        if (generator_) ticks = schedule_.TicksUntilWake(ScheduleNow(), drawWaiting_ || resizeWaiting_);
        if (!ticks) {
            WaitForSingleObject(wake_, textMs);
            return;
        }
        if (*ticks <= 0) return;
        LARGE_INTEGER due;
        due.QuadPart = -std::max<LONGLONG>(*ticks * 10'000'000 / QpcFrequency(), 1);  // relative, 100 ns units
        if (!timer_ || !SetWaitableTimer(timer_, &due, 0, nullptr, nullptr, FALSE)) {
            WaitForSingleObject(wake_, std::min(textMs, static_cast<DWORD>(QpcToMs(*ticks))));
            return;
        }
        const HANDLE handles[2] = {wake_, timer_};
        WaitForMultipleObjects(2, handles, FALSE, textMs);
    }

    // The clock as the schedule is asked: a frame is drawn a little before it
    // is due, by what the previous drawing took.
    int64_t ScheduleNow() const { return QpcNow() + drawLead_; }

    // The generator asked for by SetFrameGenerator, taken up here.
    void AdoptGenerator() {
        if (!generatorChanged_.exchange(false)) return;
        std::shared_ptr<FrameGenerator> wanted;
        bool wantedTarget = false;
        {
            std::lock_guard lock(generatorMutex_);
            wanted = wantedGenerator_;
            wantedTarget = wantedTarget_;
        }
        if (wanted == generator_ && wantedTarget == target_) return;
        DropGenerator();
        generator_ = std::move(wanted);
        if (generator_ && wantedTarget) {
            target_ = true;
            targetRate_ = 0;  // read on the first pass
            targetSecond_ = GetTickCount64();
            Log("Presenter: frame generator set: Target, one present per vsync (sync interval 1, no tearing), "
                "the nearest frame each time");
            return;
        }
        Log("Presenter: frame generator %s", generator_ ? "set: each Unique Frame is a timed sequence, V-sync ignored"
                                                        : "removed: each Unique Frame is shown at once, alone");
    }

    void DropGenerator() {
        if (!generator_) return;
        // Its Generated Frames were on screen but the Unique Frame was not yet: show it.
        if (schedule_.InProgress()) drawWaiting_ = true;
        generator_->ReleaseDevice();
        generator_.reset();
        SetRunningFactor(0);
        schedule_.Forget();
        drawLead_ = 0;
        if (target_) {
            // The user's own V-sync applies again from the next present.
            target_ = false;
            if (targetSchedule_.Shown() < targetSchedule_.Generated()) drawWaiting_ = true;
            targetSchedule_.Forget();
            ladderDepth_.Reset();
            loggedDepth_ = 4;
            vsyncClock_.Reset();
            vsyncReady_ = false;
            targetRate_ = 0;
            std::lock_guard lock(statsMutex_);
            presentation_.targetRate = 0;
        }
    }

    // A Unique Frame is in picture_: ask the schedule whether to generate,
    // the generator for the Generated Frames, and start the sequence.
    void BeginSequence(const Frame& frame, uint32_t feedGeneration) {
        const FrameSchedule::Arrival arrival =
            schedule_.UniqueFrameArrived(frame.arrivalQpc, frame.sequence, frame.width, frame.height, feedGeneration);
        FrameGenerator::Input input;
        input.device = device_.Get();
        input.context = context_.Get();
        input.picture = picture_.Get();
        input.pictureView = pictureView_.Get();
        input.width = frame.width;
        input.height = frame.height;
        input.sequence = frame.sequence;
        input.forgetEarlier = arrival.forgetEarlier;
        input.mayGenerate = arrival.mayGenerate;
        const int generated = generator_->Generate(input);
        if (generated == FrameGenerator::kCannotBeUsed) {
            Log("Presenter: the frame generator cannot be used; each Unique Frame is shown at once, alone");
            DropGenerator();
            return;
        }
        SetRunningFactor(generator_->RunningFactor());
        schedule_.Begin(generated);
    }

    // A new factor is a new present rate: the Shown rate starts measuring
    // again rather than mixing two seconds of the old rate with the new.
    void SetRunningFactor(int factor) {
        std::lock_guard lock(statsMutex_);
        if (presentation_.runningFactor == factor) return;
        presentation_.runningFactor = factor;
        presentedFrames_.Reset(GetTickCount64());
    }

    // ---- Target (ADR 0004): one present per vsync, the nearest frame ----

    // Sleeps until the swap chain can take the next frame (the waitable
    // object: once per vsync, max frame latency 1) or a wake-up (a frame, a
    // text, stop). Only the first makes the next pass present.
    void WaitForVsync() {
        if (vsyncReady_) return;
        const DWORD refreshMs = static_cast<DWORD>(std::max(1, 1000 / std::max(targetRate_, 1)));
        if (occluded_ || !latencyWaitable_) {
            // Minimised or covered: presents are not paced by the monitor
            // then; at most one per refresh, by the clock.
            if (WaitForSingleObject(wake_, refreshMs) == WAIT_TIMEOUT) vsyncReady_ = true;
            return;
        }
        const HANDLE handles[2] = {latencyWaitable_, wake_};
        const DWORD woke = WaitForMultipleObjects(2, handles, FALSE, 100);
        if (woke == WAIT_OBJECT_0 || woke == WAIT_TIMEOUT) vsyncReady_ = true;
    }

    // The rate in use: the refresh of the monitor the window is on, read
    // again once a second so it follows the window to another monitor.
    void CheckTargetRate() {
        const ULONGLONG now = GetTickCount64();
        if (targetRate_ > 0 && now < rateCheckedAt_ + 1000) return;
        rateCheckedAt_ = now;
        const int rate = MonitorRefreshRate(window_);
        if (rate == targetRate_) return;
        Log("Target: rate in use %d (was %d), the refresh of the monitor the window is on", rate, targetRate_);
        targetRate_ = rate;
        refreshTicks_ = QpcFrequency() / rate;
        vsyncClock_.Reset();
        std::lock_guard lock(statsMutex_);
        presentation_.targetRate = rate;
        presentedFrames_.Reset(now);  // a new present rate: Shown measures again
    }

    // takenAt: when the pass took the frame from the slot (before converting it).
    void BeginTargetSequence(const Frame& frame, uint32_t feedGeneration, int64_t takenAt) {
        const FrameSchedule::Arrival arrival = targetSchedule_.UniqueFrameArrived(
            frame.arrivalQpc, frame.sequence, frame.width, frame.height, feedGeneration);
        FrameGenerator::Input input;
        input.device = device_.Get();
        input.context = context_.Get();
        input.picture = picture_.Get();
        input.pictureView = pictureView_.Get();
        input.width = frame.width;
        input.height = frame.height;
        input.sequence = frame.sequence;
        input.forgetEarlier = arrival.forgetEarlier;
        input.mayGenerate = arrival.mayGenerate;
        const int64_t period = vsyncClock_.Period(refreshTicks_);
        // x4 or x8 from the recent Game Rate (core/ladder_depth.h). A change
        // takes effect with this Unique Frame; the generator restarts the
        // third stage's history from it.
        const int depth = ladderDepth_.Arrived(frame.arrivalQpc, arrival.forgetEarlier, period);
        input.eighths = depth == 8;
        if (depth != loggedDepth_) {
            Log("Target: ladder depth x%d (Game Rate %.1f, %.2f refreshes per Unique Frame)", depth,
                ladderDepth_.GameRate(), ladderDepth_.GameRate() > 0
                    ? static_cast<double>(QpcFrequency()) / ladderDepth_.GameRate() / static_cast<double>(period) : 0.0);
            loggedDepth_ = depth;
        }
        const int64_t began = QpcNow();
        const int generated = generator_->Generate(input);
        targetGenerateWorst_ = std::max(targetGenerateWorst_, QpcNow() - began);
        if (generated == FrameGenerator::kCannotBeUsed) {
            Log("Presenter: the frame generator cannot be used; each Unique Frame is shown at once, alone");
            DropGenerator();
            return;
        }
        SetRunningFactor(generator_->RunningFactor());
        // FrameFor is asked with display times (a vblank after the present).
        // A present made now is shown a refresh later at the earliest: that
        // is when the first ladder frame is due on screen, as at 4x. Not so
        // late that the Unique Frame itself misses its vsync (x8's seven FRC
        // calls take longer than an eighth; core/ladder_depth.h).
        targetSchedule_.Begin(generated, period,
                              LadderStartTicks(takenAt, QpcNow() + period, arrival.intervalTicks, generated));
    }

    // One pass of the Target loop. A new Unique Frame is taken (and its
    // Generated Frames made) whenever it arrives; a frame is presented only
    // when the swap chain is ready for the next vsync, and then always: the
    // nearest ladder frame, or the previous one again.
    void TargetPass() {
        repaint_ = false;   // every vsync presents: texts and resizes go with the next
        EndExpiredTexts();
        drawWaiting_ = false;
        if (resized_.exchange(false)) resizeWaiting_ = true;
        CheckTargetRate();
        auto next = slot_.Take();
        const int64_t takenAt = QpcNow();
        if (next && ConvertToPicture(*next->frame)) {
            lastFrame_ = next->frame;
            BeginTargetSequence(*next->frame, next->feedGeneration, takenAt);
            if (!generator_) {  // dropped: the ordinary path shows it
                drawWaiting_ = true;
                return;
            }
        }
        if (!vsyncReady_) return;
        vsyncReady_ = false;
        targetVsyncs_++;
        if (resizeWaiting_ && !ApplyResize()) return;
        if (!backBufferView_) return;

        const int64_t vsyncAt = vsyncClock_.NextVsync(QpcNow(), refreshTicks_);
        const std::optional<int> pick = targetSchedule_.FrameFor(vsyncAt);
        const int shown = targetSchedule_.Shown();
        ID3D11ShaderResourceView* picture = pictureView_.Get();
        if (shown >= 0 && shown < targetSchedule_.Generated()) {
            if (ID3D11ShaderResourceView* generated = generator_->Generated(shown)) picture = generated;
        }
        const bool presented = ShowAndPresent(picture, true);
        if (presented) {
            targetPresents_++;
            {
                std::lock_guard lock(statsMutex_);
                presentedFrames_.Presented(GetTickCount64());
            }
            generator_->NotePresented();
            if (pick && *pick == targetSchedule_.Generated() && lastFrame_) {
                const double latencyMs = QpcToMs(QpcNow() - lastFrame_->arrivalQpc);
                std::lock_guard lock(statsMutex_);
                realPresentedFrames_.Presented(GetTickCount64());
                stats_.presented++;
                latencySumMs_ += latencyMs;
                if (latencyMs > stats_.worstLatencyMs) stats_.worstLatencyMs = latencyMs;
            }
            SampleVsyncs();
        }
        LogTargetSecond();
    }

    // The swap chain's frame statistics after a present: vblank times for
    // VsyncClock, and vsyncs that kept the previous frame (missed).
    void SampleVsyncs() {
        DXGI_FRAME_STATISTICS statistics = {};
        const HRESULT hr = swapChain_->GetFrameStatistics(&statistics);
        if (FAILED(hr)) {
            targetStatsFailures_++;
            targetStatsError_ = hr;
            return;
        }
        targetMissed_ += vsyncClock_.Sample({statistics.PresentCount, statistics.PresentRefreshCount,
                                             statistics.SyncQPCTime.QuadPart, statistics.SyncRefreshCount});
        targetStatsSamples_++;
    }

    void LogTargetSecond() {
        const ULONGLONG now = GetTickCount64();
        if (now < targetSecond_ + 1000) return;
        targetSecond_ = now;
        const TargetStats s = targetSchedule_.TakeStats();
        const double refreshMs = QpcToMs(vsyncClock_.Period(refreshTicks_));
        char source[96];
        if (targetStatsSamples_ > 0) {
            snprintf(source, sizeof source, "frame statistics (%u samples, %u failed)", targetStatsSamples_,
                     targetStatsFailures_);
        } else {
            snprintf(source, sizeof source, "wake-up time (frame statistics failed hr=0x%08lX)",
                     static_cast<unsigned long>(targetStatsError_));
        }
        char ladder[24];
        if (s.ladder > 1) {
            snprintf(ladder, sizeof ladder, "x%d", s.ladder);
        } else {
            snprintf(ladder, sizeof ladder, "real frames only");
        }
        Log("Target: rate %d, vsyncs %u, presents %u, Generated Frames shown %u, Unique Frames shown %u, repeats %u, "
            "missed vsyncs %u, ladder %s (depth x%d, Game Rate %.1f); Unique Frames %u (not shown %u); refresh %.3f ms%s; "
            "generate worst %.2f ms; occluded %s; vsync times from %s",
            targetRate_, targetVsyncs_, targetPresents_, s.generatedShown, s.uniqueShown, s.repeats, targetMissed_,
            ladder, ladderDepth_.Depth(), ladderDepth_.GameRate(), s.uniqueFrames, s.notShown, refreshMs,
            vsyncClock_.Period(refreshTicks_) != refreshTicks_ ? " measured" : " nominal",
            QpcToMs(targetGenerateWorst_), occluded_ ? "yes" : "no", source);
        targetVsyncs_ = targetPresents_ = targetMissed_ = 0;
        targetStatsSamples_ = targetStatsFailures_ = 0;
        targetGenerateWorst_ = 0;
    }

    // False: no back buffer to draw into, and Present, which reports a lost
    // device, would never run: rebuild as after a loss.
    bool ApplyResize() {
        resizeWaiting_ = false;
        if (ResizeBackBuffer()) return true;
        deviceLost_ = true;
        return false;
    }

    // Milliseconds until the device may be built again; 0 when it may now.
    DWORD RebuildWaitMs() const {
        const ULONGLONG now = GetTickCount64();
        return rebuildAt_ > now ? static_cast<DWORD>(rebuildAt_ - now) : 0;
    }

    // Milliseconds until the next brief text ends; INFINITE when none.
    DWORD NextTextEndMs() {
        std::lock_guard lock(textMutex_);
        const std::optional<uint64_t> wait = texts_.MsUntilNextEnd(GetTickCount64());
        if (!wait) return INFINITE;
        return static_cast<DWORD>(std::min<uint64_t>(*wait, INFINITE - 1));
    }

    bool EndExpiredTexts() {
        std::lock_guard lock(textMutex_);
        return texts_.EndExpired(GetTickCount64());
    }

    // False: the swap chain has no usable back buffer (a lost device shows
    // here too, not only in Present); the caller rebuilds the device.
    bool ResizeBackBuffer() {
        RECT client = {};
        GetClientRect(window_, &client);
        const uint32_t width = static_cast<uint32_t>(client.right - client.left);
        const uint32_t height = static_cast<uint32_t>(client.bottom - client.top);
        if (width == 0 || height == 0) return true;  // minimised: keep the old buffers
        if (width == backWidth_ && height == backHeight_ && backBufferView_) return true;
        context_->OMSetRenderTargets(0, nullptr, nullptr);
        backBufferView_.Reset();
        overlay_.ReleaseTarget();
        context_->Flush();
        const HRESULT hr = swapChain_->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, swapChainFlags_);
        if (FAILED(hr)) {
            Log("Presenter: ResizeBuffers(%ux%u) failed hr=0x%08lX (device removed reason 0x%08lX); recreating", width,
                height, static_cast<unsigned long>(hr), static_cast<unsigned long>(device_->GetDeviceRemovedReason()));
            return false;
        }
        if (!CreateBackBufferView()) {
            Log("Presenter: no back buffer after a resize; recreating");
            return false;
        }
        return true;
    }

    // Pass 1: upload the frame and convert it into the RGB picture.
    bool ConvertToPicture(const Frame& frame) {
        if (!PreparePictureFor(frame)) return false;
        const uint8_t* pixels = frame.pixels.data();
        if (frame.format == PixelFormat::YUY2) {
            context_->UpdateSubresource(plane0_.Get(), 0, nullptr, pixels, frame.width * 2, 0);
        } else {
            context_->UpdateSubresource(plane0_.Get(), 0, nullptr, pixels, frame.width, 0);
            context_->UpdateSubresource(plane1_.Get(), 0, nullptr, pixels + static_cast<size_t>(frame.width) * frame.height,
                                        frame.width, 0);
        }
        const D3D11_VIEWPORT viewport = {0, 0, static_cast<float>(frame.width), static_cast<float>(frame.height), 0, 1};
        ID3D11RenderTargetView* target = pictureTarget_.Get();
        ID3D11ShaderResourceView* planes[2] = {plane0View_.Get(), plane1View_.Get()};
        ID3D11Buffer* constants = constants_.Get();
        context_->OMSetRenderTargets(1, &target, nullptr);
        context_->RSSetViewports(1, &viewport);
        context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context_->VSSetShader(vertexShader_.Get(), nullptr, 0);
        context_->PSSetShader(convertShader_.Get(), nullptr, 0);
        context_->PSSetConstantBuffers(0, 1, &constants);
        context_->PSSetShaderResources(0, 2, planes);
        context_->Draw(3, 0);
        context_->OMSetRenderTargets(0, nullptr, nullptr);  // the picture is read in pass 2
        havePicture_ = true;
        return true;
    }

    // (Re)creates the textures and conversion constants when the Feed's size,
    // format or colour description differs from the previous frame's.
    bool PreparePictureFor(const Frame& frame) {
        if (frame.width == 0 || frame.height == 0 ||
            frame.pixels.size() < FrameByteSize(frame.format, frame.width, frame.height)) {
            return false;
        }
        const bool sameShape = picture_ && frame.width == pictureWidth_ && frame.height == pictureHeight_ &&
                               frame.format == pictureFormat_;
        if (sameShape && frame.colour == pictureColour_) return true;

        if (!sameShape) {
            plane0_.Reset(); plane0View_.Reset(); plane1_.Reset(); plane1View_.Reset();
            picture_.Reset(); pictureTarget_.Reset(); pictureView_.Reset();
            havePicture_ = false;
            bool ok;
            if (frame.format == PixelFormat::YUY2) {
                ok = CreateTexture(frame.width / 2, frame.height, DXGI_FORMAT_R8G8B8A8_UNORM, false, plane0_, plane0View_);
            } else {
                ok = CreateTexture(frame.width, frame.height, DXGI_FORMAT_R8_UNORM, false, plane0_, plane0View_) &&
                     CreateTexture(frame.width / 2, frame.height / 2, DXGI_FORMAT_R8G8_UNORM, false, plane1_, plane1View_);
            }
            ok = ok && CreateTexture(frame.width, frame.height, DXGI_FORMAT_B8G8R8A8_UNORM, true, picture_, pictureView_) &&
                 SUCCEEDED(device_->CreateRenderTargetView(picture_.Get(), nullptr, &pictureTarget_));
            if (!ok) {
                Log("Presenter: could not create textures for a %ux%u frame", frame.width, frame.height);
                picture_.Reset();
                return false;
            }
            pictureWidth_ = frame.width;
            pictureHeight_ = frame.height;
            pictureFormat_ = frame.format;
        }

        const YuvToRgb conversion = MakeYuvToRgb(frame.colour);
        ConversionConstants constants = {};
        memcpy(constants.rows, conversion.rows.data(), sizeof constants.rows);
        constants.format = frame.format == PixelFormat::YUY2 ? 0u : 1u;
        constants.lastChromaColumn = static_cast<int32_t>(frame.width / 2) - 1;
        context_->UpdateSubresource(constants_.Get(), 0, nullptr, &constants, 0, 0);
        pictureColour_ = frame.colour;
        Log("Presenter: picture %ux%u %s, shader conversion set from the frame's colour (range %d, matrix %d)", frame.width,
            frame.height, frame.format == PixelFormat::YUY2 ? "YUY2" : "NV12", static_cast<int>(frame.colour.range),
            static_cast<int>(frame.colour.matrix));
        return true;
    }

    bool CreateTexture(uint32_t width, uint32_t height, DXGI_FORMAT format, bool renderTarget,
                       ComPtr<ID3D11Texture2D>& texture, ComPtr<ID3D11ShaderResourceView>& view) {
        D3D11_TEXTURE2D_DESC description = {};
        description.Width = width;
        description.Height = height;
        description.MipLevels = 1;
        description.ArraySize = 1;
        description.Format = format;
        description.SampleDesc.Count = 1;
        description.Usage = D3D11_USAGE_DEFAULT;
        description.BindFlags = D3D11_BIND_SHADER_RESOURCE | (renderTarget ? D3D11_BIND_RENDER_TARGET : 0u);
        return SUCCEEDED(device_->CreateTexture2D(&description, nullptr, &texture)) &&
               SUCCEEDED(device_->CreateShaderResourceView(texture.Get(), nullptr, &view));
    }

    // Pass 2: black Video Area, the picture fitted inside it in the capture
    // mode's shape (16:9 for every 16:9 mode), present now. False: not presented.
    // `shown`: the picture, or a Generated Frame of the same size. The texts
    // are drawn over whichever it is, never into it.
    bool CreateUpscaleShader(const char* entry, ComPtr<ID3D11PixelShader>& shader) {
        auto code = Compile(entry, "ps_5_0", kSharpUpscaleShaders, sizeof kSharpUpscaleShaders - 1);
        if (!code) return false;
        HRESULT hr = device_->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &shader);
        if (FAILED(hr)) Log("Upscale: %s shader creation failed hr=0x%08lX", entry, static_cast<unsigned long>(hr));
        return SUCCEEDED(hr);
    }

    bool PrepareUpscale(PictureRect fit) {
        if (!triedUpscaleShaders_) {
            triedUpscaleShaders_ = true;
            if (CreateUpscaleShader("Easu", easuShader_)) CreateUpscaleShader("Rcas", rcasShader_);
        }
        if (!rcasShader_) return false;
        if (scaledWidth_ == fit.width && scaledHeight_ == fit.height) return scaledTarget_ != nullptr;
        scaledTarget_.Reset();
        scaledView_.Reset();
        scaledWidth_ = fit.width;
        scaledHeight_ = fit.height;
        ComPtr<ID3D11Texture2D> texture;
        if (!CreateTexture(fit.width, fit.height, DXGI_FORMAT_B8G8R8A8_UNORM, true, texture, scaledView_) ||
            FAILED(device_->CreateRenderTargetView(texture.Get(), nullptr, &scaledTarget_))) {
            Log("Upscale: cannot create %dx%d intermediate texture or views; using smooth stretch", fit.width, fit.height);
            scaledView_.Reset();
            return false;
        }
        return true;
    }

    void UpscaleUsage(bool active, int width, int height) {
        {
            std::lock_guard lock(statsMutex_);
            presentation_.sharpUpscale = active;
            presentation_.feedWidth = pictureWidth_;
            presentation_.feedHeight = pictureHeight_;
            presentation_.shownWidth = width;
            presentation_.shownHeight = height;
        }
        if (active == usingUpscale_) return;
        usingUpscale_ = active;
        Log("Upscale: FSR 1 %s (picture %ux%u shown at %dx%d)", active ? "on" : "off",
            pictureWidth_, pictureHeight_, width, height);
    }

    // paced (Target): WaitForVsync has waited already; sync interval 1, no tearing.
    bool ShowAndPresent(ID3D11ShaderResourceView* shown, bool paced = false) {
        // Signalled when the swap chain can take a frame; keeps at most one in flight.
        if (latencyWaitable_ && !paced) WaitForSingleObjectEx(latencyWaitable_, 100, FALSE);

        const float black[4] = {0, 0, 0, 1};
        ID3D11RenderTargetView* target = backBufferView_.Get();
        context_->OMSetRenderTargets(1, &target, nullptr);
        context_->ClearRenderTargetView(target, black);
        if (havePicture_ && pictureVisible_) {
            const PictureRect fit = FitPicture(static_cast<int>(backWidth_), static_cast<int>(backHeight_),
                                               static_cast<int>(pictureWidth_), static_cast<int>(pictureHeight_));
            const D3D11_VIEWPORT viewport = {static_cast<float>(fit.x), static_cast<float>(fit.y),
                                             static_cast<float>(fit.width), static_cast<float>(fit.height), 0, 1};
            ID3D11ShaderResourceView* picture = shown;
            ID3D11SamplerState* sampler = sampler_.Get();
            context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            context_->VSSetShader(vertexShader_.Get(), nullptr, 0);
            const bool sharp = UseSharpUpscale(sharpUpscale_, pictureWidth_, pictureHeight_, fit) && PrepareUpscale(fit);
            UpscaleUsage(sharp, fit.width, fit.height);
            if (sharp) {
                const D3D11_VIEWPORT whole = {0, 0, viewport.Width, viewport.Height, 0, 1};
                ID3D11RenderTargetView* scaled = scaledTarget_.Get();
                context_->OMSetRenderTargets(1, &scaled, nullptr);
                context_->RSSetViewports(1, &whole);
                context_->PSSetShader(easuShader_.Get(), nullptr, 0);
                context_->PSSetShaderResources(2, 1, &shown);
                context_->Draw(3, 0);
                context_->OMSetRenderTargets(1, &target, nullptr);
                picture = scaledView_.Get();
            }
            context_->RSSetViewports(1, &viewport);
            context_->PSSetShader(sharp ? rcasShader_.Get() : showShader_.Get(), nullptr, 0);
            context_->PSSetSamplers(0, 1, &sampler);
            context_->PSSetShaderResources(2, 1, &picture);
            context_->Draw(3, 0);
            ID3D11ShaderResourceView* none = nullptr;
            context_->PSSetShaderResources(2, 1, &none);  // free the picture for pass 1
        } else {
            UpscaleUsage(false, scaledWidth_, scaledHeight_);
        }
        context_->OMSetRenderTargets(0, nullptr, nullptr);
        TextOverlay::Texts texts;
        {
            std::lock_guard lock(textMutex_);
            texts = texts_.All();
        }
        overlay_.Draw(swapChain_.Get(), texts, backWidth_, backHeight_);
        // V-sync off (default): interval 0 = do not wait for the monitor; with
        // tearing allowed the frame goes out immediately and the FreeSync
        // display follows. On: wait for the refresh, no tearing flag. Ignored
        // with a frame generator: waiting for the monitor would break the
        // timing of its frames (ADR 0003).
        // Target always waits for the refresh (ADR 0004).
        const bool vsync = paced || (vsync_.load() && !generator_);
        presentBegan_ = QpcNow();
        const HRESULT hr = swapChain_->Present(vsync ? 1 : 0, !vsync && tearing_ ? DXGI_PRESENT_ALLOW_TEARING : 0);
        presentTook_ = QpcNow() - presentBegan_;
        if (paced) occluded_ = hr == DXGI_STATUS_OCCLUDED;
        if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
            Log("Presenter: device lost hr=0x%08lX (reason 0x%08lX); recreating", static_cast<unsigned long>(hr),
                static_cast<unsigned long>(device_->GetDeviceRemovedReason()));
            deviceLost_ = true;
        } else if (FAILED(hr) && !warnedPresent_) {
            warnedPresent_ = true;
            Log("Presenter: Present failed hr=0x%08lX", static_cast<unsigned long>(hr));
        }
        return SUCCEEDED(hr);
    }

    HWND window_ = nullptr;
    HANDLE wake_ = nullptr;  // auto-reset: a frame, a resize or stop is waiting
    std::thread thread_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> resized_{false};
    std::atomic<bool> repaint_{false};
    std::atomic<bool> pictureVisible_{true};
    ComPtr<ID3D11PixelShader> easuShader_, rcasShader_;
    ComPtr<ID3D11RenderTargetView> scaledTarget_;
    ComPtr<ID3D11ShaderResourceView> scaledView_;
    int scaledWidth_ = 0, scaledHeight_ = 0;
    bool triedUpscaleShaders_ = false, usingUpscale_ = false;
    std::atomic<bool> sharpUpscale_{false};
    std::atomic<bool> vsync_{false};
    struct SubmittedFrame {
        FramePtr frame;
        uint32_t feedGeneration;
    };
    LatestSlot<SubmittedFrame> slot_;

    std::mutex textMutex_;
    ShownTexts texts_;  // times from GetTickCount64
    TextOverlay overlay_;  // presenting thread (and Start/Stop)

    std::mutex statsMutex_;
    PresentationInfo presentation_;
    PresentedFrames presentedFrames_;
    PresentedFrames realPresentedFrames_;
    Stats stats_;
    double latencySumMs_ = 0.0;
    TimedFrameStats timedStats_;
    int64_t presentCallWorstTicks_ = 0;

    std::mutex generatorMutex_;
    std::shared_ptr<FrameGenerator> wantedGenerator_;  // what SetFrameGenerator asked for
    bool wantedTarget_ = false;                        // with it: Target
    std::atomic<bool> generatorChanged_{false};
    HANDLE timer_ = nullptr;  // waitable timer: the due time of the next timed frame

    // Everything below is touched only by the presenting thread while it runs.
    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<IDXGISwapChain2> swapChain_;
    ComPtr<ID3D11RenderTargetView> backBufferView_;
    HANDLE latencyWaitable_ = nullptr;
    UINT swapChainFlags_ = 0;
    bool tearing_ = false;
    bool deviceLost_ = false;   // lost, or never created: Run builds it again
    ULONGLONG rebuildAt_ = 0;   // GetTickCount64: no rebuild attempt before this
    bool warnedPresent_ = false;
    uint32_t backWidth_ = 0, backHeight_ = 0;

    // Timed sequences. Without a generator the schedule is never started and
    // nothing below changes what is presented.
    std::shared_ptr<FrameGenerator> generator_;
    FrameSchedule schedule_{QpcFrequency()};
    bool drawWaiting_ = false;    // a text appeared or ended: to draw, once the schedule lets it
    bool resizeWaiting_ = false;  // the same for a new Video Area size
    int64_t drawLead_ = 0;        // QPC ticks: see ScheduleNow
    int64_t presentBegan_ = 0;    // QPC: when the latest Present was called
    int64_t presentTook_ = 0;     // QPC ticks: how long it took to return

    // Target (ADR 0004). Without it nothing below changes what is presented.
    bool target_ = false;
    TargetSchedule targetSchedule_{QpcFrequency()};
    LadderDepth ladderDepth_{QpcFrequency()};
    int loggedDepth_ = 4;  // the depth the last `Target: ladder depth` line said
    VsyncClock vsyncClock_;
    bool vsyncReady_ = false;     // the swap chain can take the frame for the next vsync
    bool occluded_ = false;       // the last Target present said the window cannot be seen
    int targetRate_ = 0;          // the rate in use; 0: not read yet
    int64_t refreshTicks_ = 0;    // QPC ticks per refresh at that rate
    ULONGLONG rateCheckedAt_ = 0;
    ULONGLONG targetSecond_ = 0;  // GetTickCount64 of the last Target: line
    uint32_t targetVsyncs_ = 0, targetPresents_ = 0, targetMissed_ = 0;
    uint32_t targetStatsSamples_ = 0, targetStatsFailures_ = 0;
    HRESULT targetStatsError_ = S_OK;
    int64_t targetGenerateWorst_ = 0;

    ComPtr<ID3D11VertexShader> vertexShader_;
    ComPtr<ID3D11PixelShader> convertShader_, showShader_;
    ComPtr<ID3D11Buffer> constants_;
    ComPtr<ID3D11SamplerState> sampler_;

    ComPtr<ID3D11Texture2D> plane0_, plane1_;  // the captured frame as uploaded
    ComPtr<ID3D11ShaderResourceView> plane0View_, plane1View_;
    ComPtr<ID3D11Texture2D> picture_;          // the frame converted to RGB
    ComPtr<ID3D11RenderTargetView> pictureTarget_;
    ComPtr<ID3D11ShaderResourceView> pictureView_;
    bool havePicture_ = false;
    FramePtr lastFrame_;  // the frame in picture_, kept to rebuild it after a device loss
    uint32_t pictureWidth_ = 0, pictureHeight_ = 0;
    PixelFormat pictureFormat_ = PixelFormat::YUY2;
    Colour pictureColour_;
};

Presenter::Presenter() : impl_(std::make_unique<Impl>()) {}
Presenter::~Presenter() = default;
bool Presenter::Start(HWND window) { return impl_->Start(window); }
void Presenter::Stop() { impl_->Stop(); }
void Presenter::Submit(TimedFrame frame, uint32_t feedGeneration) { impl_->Submit(std::move(frame), feedGeneration); }
void Presenter::VideoAreaResized() { impl_->VideoAreaResized(); }
void Presenter::ShowText(TextPlace place, VideoAreaText text, int durationMs) {
    impl_->ShowText(place, std::move(text), durationMs);
}
void Presenter::ShowTextWithNextFrame(TextPlace place, VideoAreaText text) {
    impl_->ShowTextWithNextFrame(place, std::move(text));
}
void Presenter::ShowPicture(bool visible) { impl_->ShowPicture(visible); }
void Presenter::SetSharpUpscale(bool on) { impl_->SetSharpUpscale(on); }
void Presenter::SetVsync(bool on) { impl_->SetVsync(on); }
void Presenter::SetFrameGenerator(std::shared_ptr<FrameGenerator> generator, bool target) {
    impl_->SetFrameGenerator(std::move(generator), target);
}

int MonitorRefreshRate(HWND window) {
    MONITORINFOEXW info = {};
    info.cbSize = sizeof info;
    if (!GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &info)) return 60;
    DEVMODEW mode = {};
    mode.dmSize = sizeof mode;
    // 0 and 1 mean "the hardware's default".
    if (!EnumDisplaySettingsW(info.szDevice, ENUM_CURRENT_SETTINGS, &mode) || mode.dmDisplayFrequency <= 1) return 60;
    return static_cast<int>(mode.dmDisplayFrequency);
}
PresentationInfo Presenter::Presentation() { return impl_->Presentation(); }
Presenter::Stats Presenter::TakeStats() { return impl_->TakeStats(); }
Presenter::TimedStats Presenter::TakeTimedStats() { return impl_->TakeTimedStats(); }

}  // namespace gp

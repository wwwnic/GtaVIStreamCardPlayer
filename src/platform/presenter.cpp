#include "platform/presenter.h"

#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi1_5.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <mutex>
#include <optional>
#include <thread>

#include "core/colour.h"
#include "core/latest_slot.h"
#include "core/picture_fit.h"
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

double QpcToMs(int64_t ticks) {
    static const double msPerTick = [] {
        LARGE_INTEGER frequency;
        QueryPerformanceFrequency(&frequency);
        return 1000.0 / static_cast<double>(frequency.QuadPart);
    }();
    return static_cast<double>(ticks) * msPerTick;
}

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
        if (!CreateDevice()) return false;
        wake_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        stop_ = false;
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
            HANDLE thread = thread_.native_handle();
            while (MsgWaitForMultipleObjects(1, &thread, FALSE, INFINITE, QS_ALLINPUT) != WAIT_OBJECT_0) {
                MSG message;
                while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&message);
            }
            thread_.join();
        }
        slot_.Take();
        lastFrame_.reset();
        ReleaseDevice();
        if (wake_) CloseHandle(wake_);
        wake_ = nullptr;
    }

    void Submit(TimedFrame frame) {
        if (!wake_ || !frame.frame) return;
        if (slot_.Put(std::move(frame))) {
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

    Stats TakeStats() {
        std::lock_guard lock(statsMutex_);
        Stats taken = stats_;
        if (taken.presented) taken.averageLatencyMs = latencySumMs_ / taken.presented;
        stats_ = {};
        latencySumMs_ = 0.0;
        return taken;
    }

    void ShowText(TextPlace place, VideoAreaText text, int durationMs) {
        const size_t index = static_cast<size_t>(place);
        if (index >= TextOverlay::kPlaces) return;
        {
            std::lock_guard lock(textMutex_);
            if (texts_[index] == text && durationMs <= 0 && textEndsAt_[index] == 0) return;  // nothing new
            texts_[index] = std::move(text);
            textEndsAt_[index] = durationMs > 0 ? GetTickCount64() + static_cast<ULONGLONG>(durationMs) : 0;
        }
        Repaint();
    }

    void ShowTextWithNextFrame(TextPlace place, VideoAreaText text) {
        const size_t index = static_cast<size_t>(place);
        if (index >= TextOverlay::kPlaces) return;
        std::lock_guard lock(textMutex_);
        texts_[index] = std::move(text);
        textEndsAt_[index] = 0;
    }

    void ShowPicture(bool visible) {
        if (pictureVisible_.exchange(visible) != visible) Repaint();
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

    ComPtr<ID3DBlob> Compile(const char* entry, const char* target) {
        ComPtr<ID3DBlob> code, errors;
        const HRESULT hr = D3DCompile(kShaders, sizeof kShaders - 1, "presenter", nullptr, nullptr, entry, target,
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
            WaitForSingleObject(wake_, deviceLost_ ? 1000 : NextTextEndMs());
            if (stop_) break;
            if (deviceLost_) {
                // Graphics driver reset or GPU removed (it can happen around
                // sleep): build everything again, once a second until it works.
                ReleaseDevice();
                if (!CreateDevice()) continue;
                Log("Presenter: device recreated after a loss");
                deviceLost_ = false;
                resized_ = true;
                // On a still picture no new frame is coming: show the last one again.
                if (lastFrame_) ConvertToPicture(*lastFrame_);
            }

            bool draw = repaint_.exchange(false) || EndExpiredTexts();
            if (resized_.exchange(false)) {
                ResizeBackBuffer();
                draw = true;  // also repaints when no new frame is coming
            }
            std::optional<TimedFrame> next = slot_.Take();
            if (next && ConvertToPicture(*next->frame)) {
                draw = true;
                lastFrame_ = next->frame;
            }
            if (!draw || !backBufferView_) continue;

            ShowAndPresent();
            if (next) {
                const double latencyMs = QpcToMs(QpcNow() - next->frame->arrivalQpc);
                std::lock_guard lock(statsMutex_);
                stats_.presented++;
                latencySumMs_ += latencyMs;
                if (latencyMs > stats_.worstLatencyMs) stats_.worstLatencyMs = latencyMs;
            }
        }
        Log("Presenter: thread stopped");
    }

    // Milliseconds until the next brief text ends; INFINITE when none.
    DWORD NextTextEndMs() {
        std::lock_guard lock(textMutex_);
        const ULONGLONG now = GetTickCount64();
        DWORD wait = INFINITE;
        for (ULONGLONG endsAt : textEndsAt_) {
            if (endsAt) wait = std::min<DWORD>(wait, endsAt > now ? static_cast<DWORD>(endsAt - now) : 0);
        }
        return wait;
    }

    bool EndExpiredTexts() {
        std::lock_guard lock(textMutex_);
        const ULONGLONG now = GetTickCount64();
        bool ended = false;
        for (size_t i = 0; i < TextOverlay::kPlaces; i++) {
            if (textEndsAt_[i] && textEndsAt_[i] <= now) {
                texts_[i] = {};
                textEndsAt_[i] = 0;
                ended = true;
            }
        }
        return ended;
    }

    void ResizeBackBuffer() {
        RECT client = {};
        GetClientRect(window_, &client);
        const uint32_t width = static_cast<uint32_t>(client.right - client.left);
        const uint32_t height = static_cast<uint32_t>(client.bottom - client.top);
        if (width == 0 || height == 0) return;  // minimised: keep the old buffers
        if (width == backWidth_ && height == backHeight_ && backBufferView_) return;
        context_->OMSetRenderTargets(0, nullptr, nullptr);
        backBufferView_.Reset();
        overlay_.ReleaseTarget();
        context_->Flush();
        const HRESULT hr = swapChain_->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, swapChainFlags_);
        if (FAILED(hr)) Log("Presenter: ResizeBuffers(%ux%u) failed hr=0x%08lX", width, height, static_cast<unsigned long>(hr));
        CreateBackBufferView();
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

    // Pass 2: black Video Area, the picture fitted 16:9 inside it, present now.
    void ShowAndPresent() {
        // Signalled when the swap chain can take a frame; keeps at most one in flight.
        if (latencyWaitable_) WaitForSingleObjectEx(latencyWaitable_, 100, FALSE);

        const float black[4] = {0, 0, 0, 1};
        ID3D11RenderTargetView* target = backBufferView_.Get();
        context_->OMSetRenderTargets(1, &target, nullptr);
        context_->ClearRenderTargetView(target, black);
        if (havePicture_ && pictureVisible_) {
            const PictureRect fit = FitPicture(static_cast<int>(backWidth_), static_cast<int>(backHeight_),
                                               static_cast<int>(pictureWidth_), static_cast<int>(pictureHeight_));
            const D3D11_VIEWPORT viewport = {static_cast<float>(fit.x), static_cast<float>(fit.y),
                                             static_cast<float>(fit.width), static_cast<float>(fit.height), 0, 1};
            ID3D11ShaderResourceView* picture = pictureView_.Get();
            ID3D11SamplerState* sampler = sampler_.Get();
            context_->RSSetViewports(1, &viewport);
            context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            context_->VSSetShader(vertexShader_.Get(), nullptr, 0);
            context_->PSSetShader(showShader_.Get(), nullptr, 0);
            context_->PSSetSamplers(0, 1, &sampler);
            context_->PSSetShaderResources(2, 1, &picture);
            context_->Draw(3, 0);
            ID3D11ShaderResourceView* none = nullptr;
            context_->PSSetShaderResources(2, 1, &none);  // free the picture for pass 1
        }
        context_->OMSetRenderTargets(0, nullptr, nullptr);
        TextOverlay::Texts texts;
        {
            std::lock_guard lock(textMutex_);
            texts = texts_;
        }
        overlay_.Draw(swapChain_.Get(), texts, backWidth_, backHeight_);
        // Interval 0 = do not wait for the monitor; with tearing allowed the
        // frame goes out immediately and the FreeSync display follows.
        const HRESULT hr = swapChain_->Present(0, tearing_ ? DXGI_PRESENT_ALLOW_TEARING : 0);
        if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
            Log("Presenter: device lost hr=0x%08lX (reason 0x%08lX); recreating", static_cast<unsigned long>(hr),
                static_cast<unsigned long>(device_->GetDeviceRemovedReason()));
            deviceLost_ = true;
            SetEvent(wake_);
        } else if (FAILED(hr) && !warnedPresent_) {
            warnedPresent_ = true;
            Log("Presenter: Present failed hr=0x%08lX", static_cast<unsigned long>(hr));
        }
    }

    HWND window_ = nullptr;
    HANDLE wake_ = nullptr;  // auto-reset: a frame, a resize or stop is waiting
    std::thread thread_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> resized_{false};
    std::atomic<bool> repaint_{false};
    std::atomic<bool> pictureVisible_{true};
    LatestSlot<TimedFrame> slot_;

    std::mutex textMutex_;
    TextOverlay::Texts texts_;
    std::array<ULONGLONG, TextOverlay::kPlaces> textEndsAt_ = {};  // GetTickCount64; 0 = stays
    TextOverlay overlay_;  // presenting thread (and Start/Stop)

    std::mutex statsMutex_;
    Stats stats_;
    double latencySumMs_ = 0.0;

    // Everything below is touched only by the presenting thread while it runs.
    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<IDXGISwapChain2> swapChain_;
    ComPtr<ID3D11RenderTargetView> backBufferView_;
    HANDLE latencyWaitable_ = nullptr;
    UINT swapChainFlags_ = 0;
    bool tearing_ = false;
    bool deviceLost_ = false;
    bool warnedPresent_ = false;
    uint32_t backWidth_ = 0, backHeight_ = 0;

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
void Presenter::Submit(TimedFrame frame) { impl_->Submit(std::move(frame)); }
void Presenter::VideoAreaResized() { impl_->VideoAreaResized(); }
void Presenter::ShowText(TextPlace place, VideoAreaText text, int durationMs) {
    impl_->ShowText(place, std::move(text), durationMs);
}
void Presenter::ShowTextWithNextFrame(TextPlace place, VideoAreaText text) {
    impl_->ShowTextWithNextFrame(place, std::move(text));
}
void Presenter::ShowPicture(bool visible) { impl_->ShowPicture(visible); }
Presenter::Stats Presenter::TakeStats() { return impl_->TakeStats(); }

}  // namespace gp

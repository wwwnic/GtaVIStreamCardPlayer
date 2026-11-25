#include "platform/amf_upscaler.h"

#include <windows.h>
#include <d3d11_4.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <system_error>
#include <thread>

#include "components/Component.h"
#include "core/Factory.h"
#include "platform/log.h"

namespace gp {
namespace {
using Microsoft::WRL::ComPtr;
using Clock = std::chrono::steady_clock;
double SinceMs(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

// The AMF SDK's HQScaler component (components/HQScaler.h, AMF SDK, MIT).
// Its header is not vendored here; these are its documented names and values.
const wchar_t kHqScaler[] = L"AMFHQScaler";
const wchar_t kAlgorithm[] = L"HQScalerAlgorithm";
const wchar_t kEngineType[] = L"HQScalerEngineType";
const wchar_t kOutputSize[] = L"HQSOutputSize";
const wchar_t kKeepAspectRatio[] = L"KeepAspectRatio";
const wchar_t kFill[] = L"Fill";
const amf_int64 kVideoSr10 = 2;  // AMF_HQ_SCALER_ALGORITHM_VIDEOSR1_0, based on FSR 1
const amf_int64 kVideoSr11 = 4;  // AMF_HQ_SCALER_ALGORITHM_VIDEOSR1_1, DX11/DX12 only
}  // namespace

struct AmfUpscaler::Session {
    ComPtr<ID3D11Device> device;
    UINT inWidth, inHeight, outWidth, outHeight;
    HMODULE runtime = nullptr;
    amf::AMFContextPtr context;
    amf::AMFComponentPtr scaler;
    std::string algorithm, failure;
    std::atomic<bool> started{false};
    Clock::time_point initStart = Clock::now();

    Session(ID3D11Device* d, UINT iw, UINT ih, UINT ow, UINT oh)
        : device(d), inWidth(iw), inHeight(ih), outWidth(ow), outHeight(oh) {}
    ~Session() {
        if (scaler) scaler->Terminate();
        scaler = nullptr;
        if (context) context->Terminate();
        context = nullptr;
        if (runtime) FreeLibrary(runtime);
    }

    bool Check(const char* call, AMF_RESULT result) {
        if (result == AMF_OK) return true;
        failure = std::string(call) + " AMF_RESULT " + std::to_string(result);
        return false;
    }

    void Start() {
        runtime = LoadLibraryExW(AMF_DLL_NAME, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!runtime) { failure = "cannot load the AMD driver runtime"; return; }
        auto init = reinterpret_cast<AMFInit_Fn>(GetProcAddress(runtime, AMF_INIT_FUNCTION_NAME));
        auto query = reinterpret_cast<AMFQueryVersion_Fn>(GetProcAddress(runtime, AMF_QUERY_VERSION_FUNCTION_NAME));
        if (!init || !query) { failure = "AMF runtime entry points missing"; return; }
        amf_uint64 version = 0;
        amf::AMFFactory* factory = nullptr;
        if (!Check("AMFQueryVersion", query(&version)) ||
            !Check("AMFInit", init(std::min<amf_uint64>(version, AMF_FULL_VERSION), &factory))) return;
        if (!factory) { failure = "no AMF factory"; return; }
        if (!Check("CreateContext", factory->CreateContext(&context))) return;
        if (!context) { failure = "no AMF context"; return; }
        if (!Check("InitDX11", context->InitDX11(device.Get()))) return;
        // 1.1 first (newer); a driver that does not keep it or cannot start it gets 1.0.
        if (TryAlgorithm(factory, kVideoSr11, "VideoSR 1.1")) return;
        const std::string first = failure;
        failure.clear();
        if (!TryAlgorithm(factory, kVideoSr10, "VideoSR 1.0")) failure = first + "; then " + failure;
        else Log("Upscale: VideoSR 1.1 not available (%s), using 1.0", first.c_str());
    }

    bool TryAlgorithm(amf::AMFFactory* factory, amf_int64 wanted, const char* name) {
        if (scaler) scaler->Terminate();
        scaler = nullptr;
        if (!Check("CreateComponent", factory->CreateComponent(context, kHqScaler, &scaler))) return false;
        if (!scaler) { failure = "no HQScaler component"; return false; }
        if (!Check("engine", scaler->SetProperty(kEngineType, amf_int64(amf::AMF_MEMORY_DX11))) ||
            !Check("algorithm", scaler->SetProperty(kAlgorithm, wanted)) ||
            !Check("output size", scaler->SetProperty(kOutputSize, AMFConstructSize(outWidth, outHeight))) ||
            !Check("keep aspect ratio", scaler->SetProperty(kKeepAspectRatio, false)) ||
            !Check("fill", scaler->SetProperty(kFill, false)))
            return false;
        amf_int64 kept = -1;
        scaler->GetProperty(kAlgorithm, &kept);
        if (kept != wanted) {
            failure = "the driver did not keep the algorithm (" + std::to_string(kept) + ")";
            return false;
        }
        if (!Check("HQScaler Init", scaler->Init(amf::AMF_SURFACE_BGRA, inWidth, inHeight))) return false;
        algorithm = name;
        return true;
    }
};

AmfUpscaler::AmfUpscaler() = default;
AmfUpscaler::~AmfUpscaler() = default;

void AmfUpscaler::Release() {
    session_.reset();
    algorithm_.clear();
    pendingWidth_ = pendingHeight_ = 0;
}

void AmfUpscaler::Fail(const std::string& reason) {
    Log("Upscale: AMD VideoSR not available (%s); FSR 1 for this run", reason.c_str());
    failed_ = true;
    Release();
}

bool AmfUpscaler::Scale(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11Texture2D* source,
                        ID3D11Texture2D* dest) {
    if (failed_ || !device || !context || !source || !dest) return false;
    D3D11_TEXTURE2D_DESC in{}, out{};
    source->GetDesc(&in);
    dest->GetDesc(&out);
    const bool matches = session_ && session_->device.Get() == device && session_->inWidth == in.Width &&
                         session_->inHeight == in.Height && session_->outWidth == out.Width &&
                         session_->outHeight == out.Height;
    if (!matches) {
        // One session starting at a time; a new size must hold before it starts.
        if (session_ && !session_->started.load(std::memory_order_acquire)) return false;
        const auto now = GetTickCount64();
        if (pendingWidth_ != out.Width || pendingHeight_ != out.Height) {
            pendingWidth_ = out.Width;
            pendingHeight_ = out.Height;
            pendingSince_ = now;
        }
        if (now - pendingSince_ < kSettleMs) return false;
        ComPtr<ID3D11Multithread> protection;
        if (FAILED(context->QueryInterface(IID_PPV_ARGS(&protection)))) {
            Fail("D3D11 context lacks multithread protection");
            return false;
        }
        protection->SetMultithreadProtected(TRUE);
        algorithm_.clear();
        session_ = std::make_shared<Session>(device, in.Width, in.Height, out.Width, out.Height);
        try {
            std::thread([session = session_] {
                session->Start();
                session->started.store(true, std::memory_order_release);
            }).detach();
        } catch (const std::system_error&) {
            Fail("could not start the AMF worker");
            return false;
        }
        Log("Upscale: starting AMD VideoSR %ux%u -> %ux%u on its own thread", in.Width, in.Height, out.Width,
            out.Height);
        return false;
    }
    auto& s = *session_;
    if (!s.started.load(std::memory_order_acquire)) return false;
    if (!s.failure.empty()) {
        Fail(s.failure);
        return false;
    }
    if (algorithm_.empty()) {
        algorithm_ = s.algorithm;
        Log("Upscale: AMD %s ready in %.1f ms (%ux%u -> %ux%u)", algorithm_.c_str(), SinceMs(s.initStart), s.inWidth,
            s.inHeight, s.outWidth, s.outHeight);
    }

    const auto start = Clock::now();
    amf::AMFSurfacePtr input;
    if (!s.Check("CreateSurfaceFromDX11Native", s.context->CreateSurfaceFromDX11Native(source, &input, nullptr)) ||
        !input) {
        Fail(s.failure.empty() ? "empty input surface" : s.failure);
        return false;
    }
    const amf_pts pts = static_cast<amf_pts>(++sequence_);
    input->SetPts(pts);
    AMF_RESULT result = s.scaler->SubmitInput(input);
    if (result == AMF_INPUT_FULL) {
        amf::AMFDataPtr stale;
        s.scaler->QueryOutput(&stale);
        result = s.scaler->SubmitInput(input);
    }
    if (!s.Check("SubmitInput", result)) {
        Fail(s.failure);
        return false;
    }
    bool done = false;
    do {
        amf::AMFDataPtr output;
        result = s.scaler->QueryOutput(&output);
        if (result == AMF_REPEAT || (result == AMF_OK && !output) || result == AMF_NEED_MORE_INPUT) {
            SwitchToThread();
            continue;
        }
        if (!s.Check("QueryOutput", result)) {
            Fail(s.failure);
            return false;
        }
        if (output->GetPts() != pts) continue;  // an older picture's output
        amf::AMFSurfacePtr surface(output);
        auto* plane = surface && surface->GetMemoryType() == amf::AMF_MEMORY_DX11 ? surface->GetPlaneAt(0) : nullptr;
        auto* texture = plane ? static_cast<ID3D11Texture2D*>(plane->GetNative()) : nullptr;
        D3D11_TEXTURE2D_DESC got{};
        if (texture) texture->GetDesc(&got);
        if (got.Width != out.Width || got.Height != out.Height || got.Format != out.Format) {
            Fail("unexpected output texture");
            return false;
        }
        context->CopyResource(dest, texture);
        done = true;
    } while (!done && SinceMs(start) < 4.0);
    context->ClearState();  // AMF shares the immediate context
    const double ms = SinceMs(start);
    if (done) {
        ++scaled_;
        sumMs_ += ms;
        worstMs_ = std::max(worstMs_, ms);
    } else {
        ++missed_;
    }
    const auto now = GetTickCount64();
    if (now - lastLog_ >= 1000) {
        if (lastLog_ && (scaled_ || missed_))
            Log("Upscale: %s scaled %llu, not ready %llu (FSR 1 used), CPU %.2f ms average %.2f ms worst",
                algorithm_.c_str(), scaled_, missed_, scaled_ ? sumMs_ / scaled_ : 0.0, worstMs_);
        lastLog_ = now;
        scaled_ = missed_ = 0;
        sumMs_ = worstMs_ = 0;
    }
    return done;
}

}  // namespace gp

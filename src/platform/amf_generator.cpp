#include "platform/amf_generator.h"

#include <windows.h>
#include <d3d11_4.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <string>
#include <thread>

#include "components/Component.h"
#include "components/FRC.h"
#include "core/Factory.h"
#include "core/frame_schedule.h"
#include "platform/frame_generator.h"
#include "platform/log.h"

namespace gp {
namespace {
using Microsoft::WRL::ComPtr;
using Clock = std::chrono::steady_clock;
double SinceMs(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

struct Stage {
    amf::AMFComponentPtr frc;
    std::array<ComPtr<ID3D11Texture2D>, 4> inputs;
    unsigned next = 0;
    bool havePrevious = false;

    void Close() {
        if (frc) frc->Terminate();
        frc = nullptr;
    }
};

struct Picture {
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11ShaderResourceView> view;
};

// One device and Feed size. During startup the worker owns this entire
// session even if the Presenter drops its reference. It never uses the window.
struct Session {
    ComPtr<ID3D11Device> device;
    UINT width, height;
    HMODULE runtime = nullptr;
    amf::AMFContextPtr context;
    Stage first, second;
    Picture middle, firstQuarter, lastQuarter;
    std::string quarterFailure;
    std::string failure;
    std::atomic<bool> started{false};
    Clock::time_point initStart = Clock::now();

    Session(const FrameGenerator::Input& in) : device(in.device), width(in.width), height(in.height) {}
    ~Session() {
        second.Close();
        first.Close();
        if (context) context->Terminate();
        context = nullptr;
        if (runtime) FreeLibrary(runtime);
    }

    bool Check(const char* call, AMF_RESULT result) {
        if (result == AMF_OK) return true;
        failure = std::string(call) + " AMF_RESULT " + std::to_string(result);
        return false;
    }

    void Start(bool four) {
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
        StartStage(factory, first);
        if (!failure.empty() || !MakePicture(middle) || !four) return;
        StartStage(factory, second);
        if (failure.empty() && MakePicture(firstQuarter)) MakePicture(lastQuarter);
        quarterFailure.swap(failure);
    }

    D3D11_TEXTURE2D_DESC TextureDesc() const {
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        return desc;
    }

    bool MakePicture(Picture& picture) {
        auto desc = TextureDesc();
        if (SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &picture.texture)) &&
            SUCCEEDED(device->CreateShaderResourceView(picture.texture.Get(), nullptr, &picture.view))) return true;
        failure = "could not allocate the Generated Frame";
        return false;
    }

    void StartStage(amf::AMFFactory* factory, Stage& stage) {
        auto& frc = stage.frc;
        if (!Check("CreateComponent", factory->CreateComponent(context, AMFFRC, &frc))) return;
        if (!frc) { failure = "no FRC component"; return; }
        if (!Check("engine", frc->SetProperty(AMF_FRC_ENGINE_TYPE, amf_int64(FRC_ENGINE_DX11))) ||
            !Check("mode", frc->SetProperty(AMF_FRC_MODE, amf_int64(FRC_ONLY_INTERPOLATED))) ||
            !Check("profile", frc->SetProperty(AMF_FRC_PROFILE, amf_int64(FRC_PROFILE_HIGH))) ||
            !Check("fallback", frc->SetProperty(AMF_FRC_ENABLE_FALLBACK, true)) ||
            !Check("future frame", frc->SetProperty(AMF_FRC_USE_FUTURE_FRAME, false)) ||
            !Check("indicator", frc->SetProperty(AMF_FRC_INDICATOR, false)) ||
            !Check("FRC Init", frc->Init(amf::AMF_SURFACE_BGRA, width, height))) return;

        auto desc = TextureDesc();
        for (auto& input : stage.inputs) {
            if (FAILED(device->CreateTexture2D(&desc, nullptr, &input))) {
                failure = "could not allocate FRC input textures";
                return;
            }
        }
    }

    bool Submit(Stage& stage, const FrameGenerator::Input& frame, unsigned& late) {
        auto& frc = stage.frc;
        auto* copy = stage.inputs[stage.next].Get();
        stage.next = (stage.next + 1) % stage.inputs.size();
        frame.context->CopyResource(copy, frame.picture);
        amf::AMFSurfacePtr input;
        if (!Check("CreateSurfaceFromDX11Native", context->CreateSurfaceFromDX11Native(copy, &input, nullptr)))
            return false;
        if (!input) { failure = "empty FRC input surface"; return false; }
        input->SetPts(static_cast<amf_pts>(frame.sequence));
        auto result = frc->SubmitInput(input);
        if (result == AMF_INPUT_FULL) {
            amf::AMFDataPtr dropped;
            auto drain = frc->QueryOutput(&dropped);
            if (drain != AMF_REPEAT && drain != AMF_NEED_MORE_INPUT && !Check("QueryOutput", drain)) return false;
            if (dropped) ++late;
            result = frc->SubmitInput(input);
        }
        return Check("SubmitInput", result);
    }

    bool Receive(Stage& stage, const FrameGenerator::Input& frame, Picture& dest, unsigned& late) {
        const auto start = Clock::now();
        do {
            amf::AMFDataPtr output;
            auto result = stage.frc->QueryOutput(&output);
            if (result == AMF_NEED_MORE_INPUT) return false;
            if (result == AMF_REPEAT || (result == AMF_OK && !output)) {
                SwitchToThread();
                continue;
            }
            if (!Check("QueryOutput", result)) return false;
            if (!CurrentGeneratedOutput(frame.sequence, static_cast<uint64_t>(output->GetPts()))) {
                ++late;
                continue;
            }
            amf::AMFSurfacePtr surface(output);
            auto* plane = surface && surface->GetMemoryType() == amf::AMF_MEMORY_DX11 ? surface->GetPlaneAt(0) : nullptr;
            auto* texture = plane ? static_cast<ID3D11Texture2D*>(plane->GetNative()) : nullptr;
            D3D11_TEXTURE2D_DESC desc{};
            if (texture) texture->GetDesc(&desc);
            if (desc.Width != width || desc.Height != height || desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM ||
                desc.ArraySize != 1 || desc.MipLevels != 1 || desc.SampleDesc.Count != 1) {
                failure = "unexpected FRC output texture";
                return false;
            }
            frame.context->CopyResource(dest.texture.Get(), texture);
            return true;
        } while (SinceMs(start) < 2.0);
        return false;
    }
};

class AmfGenerator final : public FrameGenerator {
public:
    AmfGenerator(bool four, std::function<void(bool)> failed) : four_(four), failed_(std::move(failed)) {}

    int Generate(const Input& frame) override {
        if (!session_ || session_->device.Get() != frame.device ||
            session_->width != frame.width || session_->height != frame.height) {
            ComPtr<ID3D11Multithread> protection;
            if (FAILED(frame.context->QueryInterface(IID_PPV_ARGS(&protection))))
                return Fail("D3D11 context lacks multithread protection");
            protection->SetMultithreadProtected(TRUE);
            session_ = std::make_shared<Session>(frame);
            announced_ = false;
            try {
                std::thread([session = session_, four = four_] {
                    session->Start(four);
                    session->started.store(true, std::memory_order_release);
                }).detach();
            } catch (const std::system_error&) { return Fail("could not start AMF worker"); }
            Log("FrameGen: starting FRC on its own thread");
            return 0;
        }
        auto& s = *session_;
        if (!s.started.load(std::memory_order_acquire)) return 0;
        if (!s.failure.empty()) return Fail(s.failure.c_str());
        if (!s.quarterFailure.empty()) FallBack(s.quarterFailure);
        if (!announced_) {
            Log("FrameGen: FRC started in %.1f ms, DX11 BGRA %ux%u", SinceMs(s.initStart), s.width, s.height);
            announced_ = true;
        }
        if (s.first.havePrevious && frame.forgetEarlier) {
            if (!s.Check("Flush", s.first.frc->Flush())) return Fail(s.failure.c_str());
            s.first.havePrevious = false;
        }
        const auto start = Clock::now();
        const bool attempt = s.first.havePrevious && frame.mayGenerate;
        const bool made = s.Submit(s.first, frame, late_) && attempt && s.Receive(s.first, frame, s.middle, late_);
        generated_ = made ? 1 : 0;
        if (four_ && s.failure.empty()) generated_ = GenerateQuarters(s, frame, made);
        // FRC shares the immediate context. Both Presenter passes rebind all
        // their shader inputs, target and viewport starting from these defaults.
        frame.context->ClearState();
        if (!s.failure.empty()) return Fail(s.failure.c_str());
        s.first.havePrevious = true;
        if (attempt) {
            ++attempts_;
            made_ += generated_;
            if (generated_ == 3) quarters_ += 2;
            const double ms = SinceMs(start);
            sum_ += ms;
            worst_ = std::max(worst_, ms);
        }
        return generated_;
    }

    ID3D11ShaderResourceView* Generated(int index) override {
        if (generated_ == 3 && index == 0) return session_->firstQuarter.view.Get();
        if (generated_ == 3 && index == 2) return session_->lastQuarter.view.Get();
        return session_->middle.view.Get();
    }
    int RunningFactor() const override { return announced_ ? (four_ ? 4 : 2) : 0; }
    void ReleaseDevice() override { session_.reset(); announced_ = false; }

    void NotePresented() override {
        ++presented_;
        if (SinceMs(second_) < 1000) return;
        Log("FrameGen: %dx, generated %u from %u attempts, quarter frames %u, presented %u, late outputs dropped %u, "
            "generation CPU %.2f ms average %.2f ms worst", four_ ? 4 : 2, made_, attempts_, quarters_, presented_, late_,
            attempts_ ? sum_ / attempts_ : 0, worst_);
        second_ = Clock::now();
        made_ = quarters_ = attempts_ = presented_ = late_ = 0;
        sum_ = worst_ = 0;
    }

private:
    int GenerateQuarters(Session& s, const Input& frame, bool middleReady) {
        auto& stage = s.second;
        Input real = frame;
        real.sequence *= 2;
        bool q1 = false, q3 = false;
        if (CanGenerateQuarters(stage.havePrevious, middleReady, frame.forgetEarlier)) {
            Input middle = real;
            --middle.sequence;
            middle.picture = s.middle.texture.Get();
            q1 = s.Submit(stage, middle, late_) && s.Receive(stage, middle, s.firstQuarter, late_);
            if (s.failure.empty())
                q3 = s.Submit(stage, real, late_) && s.Receive(stage, real, s.lastQuarter, late_);
        } else {
            if (!stage.havePrevious || s.Check("quarter FRC Flush", stage.frc->Flush()))
                stage.havePrevious = s.Submit(stage, real, late_);
        }
        if (!s.failure.empty()) FallBack(s.failure);
        return ReadyGeneratedFrames(middleReady, q1, q3);
    }

    void FallBack(std::string& reason) {
        Log("FrameGen: second FRC unavailable (%s), using 2x", reason.c_str());
        reason.clear();
        four_ = false;
        failed_(true);
    }

    int Fail(const char* reason) {
        Log("FrameGen: not available: %s", reason);
        failed_(false);
        return kCannotBeUsed;
    }
    bool four_;
    int generated_ = 0;
    unsigned quarters_ = 0;
    std::function<void(bool)> failed_;
    std::shared_ptr<Session> session_;
    bool announced_ = false;
    Clock::time_point second_ = Clock::now();
    unsigned made_ = 0, attempts_ = 0, presented_ = 0, late_ = 0;
    double sum_ = 0, worst_ = 0;
};
}  // namespace

bool AmfRuntimeInstalled() {
    wchar_t directory[MAX_PATH]{};
    const auto length = GetSystemDirectoryW(directory, MAX_PATH);
    return length && length < MAX_PATH &&
           GetFileAttributesW((std::wstring(directory) + L"\\" + AMF_DLL_NAME).c_str()) != INVALID_FILE_ATTRIBUTES;
}

std::shared_ptr<FrameGenerator> MakeAmfGenerator(bool four, std::function<void(bool)> failed) {
    return std::make_shared<AmfGenerator>(four, std::move(failed));
}

}  // namespace gp

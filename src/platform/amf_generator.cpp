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
#include <vector>

#include "components/Component.h"
#include "components/FRC.h"
#include "core/Factory.h"
#include "core/frame_schedule.h"
#include "core/ladder_depth.h"
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
    // Copies of the inputs: two Unique Frames' worth (the third stage takes
    // four inputs per Unique Frame, so it has eight).
    std::vector<ComPtr<ID3D11Texture2D>> inputs;
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
    // Target's x8 ladder only: the third FRC, fed the quarter stream, and the
    // eighths it makes (1/8, 3/8, 5/8, 7/8).
    Stage third;
    std::array<Picture, 4> eighths;
    std::string eighthFailure;
    std::string failure;
    std::atomic<bool> started{false};
    Clock::time_point initStart = Clock::now();

    Session(const FrameGenerator::Input& in) : device(in.device), width(in.width), height(in.height) {}
    ~Session() {
        third.Close();
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

    void Start(bool four, bool withEighths) {
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
        if (!quarterFailure.empty() || !withEighths) return;
        StartStage(factory, third, 8);
        for (auto& eighth : eighths) {
            if (!failure.empty() || !MakePicture(eighth)) break;
        }
        eighthFailure.swap(failure);
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

    void StartStage(amf::AMFFactory* factory, Stage& stage, size_t inputCopies = 4) {
        stage.inputs.resize(inputCopies);
        auto& frc = stage.frc;
        if (!Check("CreateComponent", factory->CreateComponent(context, AMFFRC, &frc))) return;
        if (!frc) { failure = "no FRC component"; return; }
        if (!Check("engine", frc->SetProperty(AMF_FRC_ENGINE_TYPE, amf_int64(FRC_ENGINE_DX11))) ||
            !Check("mode", frc->SetProperty(AMF_FRC_MODE, amf_int64(FRC_ONLY_INTERPOLATED))) ||
            // SUPER, not HIGH: on 1080p console capture HIGH loses fast camera
            // pans and falls back to a blend (double image). Costs a little GPU.
            !Check("profile", frc->SetProperty(AMF_FRC_PROFILE, amf_int64(FRC_PROFILE_SUPER))) ||
            !Check("motion search", frc->SetProperty(AMF_FRC_MV_SEARCH_MODE, amf_int64(FRC_MV_SEARCH_NATIVE))) ||
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
    AmfGenerator(bool four, bool eighths, std::function<void(GenerationNotice)> failed)
        : four_(four), eighths_(four && eighths), failed_(std::move(failed)) {}

    int Generate(const Input& frame) override {
        if (!session_ || session_->device.Get() != frame.device ||
            session_->width != frame.width || session_->height != frame.height) {
            ComPtr<ID3D11Multithread> protection;
            if (FAILED(frame.context->QueryInterface(IID_PPV_ARGS(&protection))))
                return Fail("D3D11 context lacks multithread protection");
            protection->SetMultithreadProtected(TRUE);
            session_ = std::make_shared<Session>(frame);
            announced_ = false;
            startupFrames_ = 1;
            readyInputs_ = 0;
            reportedRecovery_ = false;
            warmup_.Reset();
            auto desc = session_->TextureDesc();
            if (FAILED(frame.device->CreateTexture2D(&desc, nullptr, &warmup_)))
                Log("FrameGen: cannot retain a warm-up picture; history starts after setup");
            RememberWarmup(frame);
            try {
                std::thread([session = session_, four = four_, eighths = eighths_] {
                    session->Start(four, eighths);
                    session->started.store(true, std::memory_order_release);
                }).detach();
            } catch (const std::system_error&) { return Fail("could not start AMF worker"); }
            Log("FrameGen: starting FRC on its own thread");
            return 0;
        }
        auto& s = *session_;
        if (!s.started.load(std::memory_order_acquire)) {
            ++startupFrames_;
            RememberWarmup(frame);
            return 0;
        }
        if (!s.failure.empty()) return Fail(s.failure.c_str());
        if (!s.quarterFailure.empty()) FallBack(s.quarterFailure);
        if (eighths_ && !s.eighthFailure.empty()) EighthsFallBack(s.eighthFailure);
        const bool becameReady = !announced_;
        if (becameReady) {
            Log("FrameGen: FRC started in %.1f ms, DX11 BGRA %ux%u, %u real frames during setup",
                SinceMs(s.initStart), s.width, s.height, startupFrames_);
            announced_ = true;
        }
        if (s.first.havePrevious && frame.forgetEarlier) {
            if (!s.Check("Flush", s.first.frc->Flush())) return Fail(s.failure.c_str());
            s.first.havePrevious = false;
        }
        // Driver setup is asynchronous and has no bounded duration. Retain the
        // latest real picture while it runs, then prime both stages here without
        // asking the worker to touch the Presenter's immediate context. This
        // avoids discarding the first new-size picture and another warm-up frame.
        if (!s.first.havePrevious && warmup_ && !frame.forgetEarlier) {
            auto previous = frame;
            previous.picture = warmup_.Get();
            previous.sequence = warmupSequence_;
            s.first.havePrevious = s.Submit(s.first, previous, late_);
            if (four_ && s.failure.empty()) {
                previous.sequence *= 2;
                s.second.havePrevious = s.Submit(s.second, previous, late_);
            }
        }
        warmup_.Reset();
        const auto start = Clock::now();
        const bool attempt = s.first.havePrevious && frame.mayGenerate;
        const bool made = s.Submit(s.first, frame, late_) && attempt && s.Receive(s.first, frame, s.middle, late_);
        generated_ = made ? 1 : 0;
        if (four_ && s.failure.empty()) generated_ = GenerateQuarters(s, frame, made);
        if (eighths_ && s.failure.empty()) generated_ = GenerateEighths(s, frame, generated_);
        // FRC shares the immediate context. Both Presenter passes rebind all
        // their shader inputs, target and viewport starting from these defaults.
        frame.context->ClearState();
        if (!s.failure.empty()) return Fail(s.failure.c_str());
        s.first.havePrevious = true;
        if (attempt) {
            ++attempts_;
            made_ += generated_;
            if (generated_ >= 3) quarters_ += 2;
            const double ms = SinceMs(start);
            sum_ += ms;
            worst_ = std::max(worst_, ms);
            if (frame.eighths) ++eighthAttempts_;
            if (generated_ == 7) {
                eighthsMade_ += 4;
                ++eighthLadders_;
                eighthSum_ += ms;
                eighthWorst_ = std::max(eighthWorst_, ms);
            }
        }
        ++readyInputs_;
        if (four_ && generated_ >= 3 && !reportedRecovery_) {
            reportedRecovery_ = true;
            Log("FrameGen: full 4x on real input %u of this session (%u startup, %u ready)",
                startupFrames_ + readyInputs_, startupFrames_, readyInputs_);
        }
        if (becameReady)
            Log("FrameGen: first ready output %d Generated Frames after %u startup frames", generated_, startupFrames_);
        return generated_;
    }

    ID3D11ShaderResourceView* Generated(int index) override {
        if (generated_ == 7) {
            // e1, Q1, e3, M, e5, Q3, e7: the eighths at even places.
            if (index % 2 == 0) return session_->eighths[index / 2].view.Get();
            if (index == 1) return session_->firstQuarter.view.Get();
            if (index == 5) return session_->lastQuarter.view.Get();
            return session_->middle.view.Get();
        }
        if (generated_ == 3 && index == 0) return session_->firstQuarter.view.Get();
        if (generated_ == 3 && index == 2) return session_->lastQuarter.view.Get();
        return session_->middle.view.Get();
    }
    int RunningFactor() const override { return announced_ ? (four_ ? 4 : 2) : 0; }
    bool MakesEighths() const override { return eighths_; }
    void ReleaseDevice() override { session_.reset(); warmup_.Reset(); announced_ = false; }

    void NotePresented() override {
        ++presented_;
        if (SinceMs(second_) < 1000) return;
        // Target with a third FRC only: the eighths, and the time of the
        // Unique Frames that got the whole x8 ladder.
        char eighths[160] = "";
        if (eighths_ || eighthAttempts_)
            snprintf(eighths, sizeof eighths, ", eighth frames %u (x8 wanted %u, made %u, x8 CPU %.2f ms average %.2f ms worst)",
                     eighthsMade_, eighthAttempts_, eighthLadders_, eighthLadders_ ? eighthSum_ / eighthLadders_ : 0,
                     eighthWorst_);
        Log("FrameGen: %dx, generated %u from %u attempts, quarter frames %u%s, presented %u, late outputs dropped %u, "
            "generation CPU %.2f ms average %.2f ms worst", four_ ? 4 : 2, made_, attempts_, quarters_, eighths,
            presented_, late_, attempts_ ? sum_ / attempts_ : 0, worst_);
        second_ = Clock::now();
        made_ = quarters_ = attempts_ = presented_ = late_ = eighthsMade_ = eighthAttempts_ = eighthLadders_ = 0;
        sum_ = worst_ = eighthSum_ = eighthWorst_ = 0;
    }

private:
    void RememberWarmup(const Input& frame) {
        if (!warmup_) return;
        frame.context->CopyResource(warmup_.Get(), frame.picture);
        warmupSequence_ = frame.sequence;
    }

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

    // Target's x8 ladder: the third FRC, fed the quarter stream N-1, Q1, M,
    // Q3, N (sequence x4: each input its own number), makes the eighths. Only
    // a ladder with all four eighths is x8 (core/ladder_depth.h).
    int GenerateEighths(Session& s, const Input& frame, int quarters) {
        auto& stage = s.third;
        Input real = frame;
        real.sequence *= 4;
        int ready = 0;
        switch (ThirdStageStep(frame.eighths, stage.havePrevious, quarters, frame.forgetEarlier)) {
        case EighthStep::Idle:
            stage.havePrevious = false;  // not fed: its history is lost
            return quarters;
        case EighthStep::Restart:
            // Its history may hold older frames even when havePrevious is false.
            if (s.Check("eighth FRC Flush", stage.frc->Flush())) stage.havePrevious = s.Submit(stage, real, late_);
            break;
        case EighthStep::Generate: {
            ID3D11Texture2D* const stream[4] = {s.firstQuarter.texture.Get(), s.middle.texture.Get(),
                                                s.lastQuarter.texture.Get(), frame.picture};
            for (int i = 0; i < 4 && s.failure.empty(); i++) {
                Input input = real;
                input.sequence = real.sequence - 3 + i;
                input.picture = stream[i];
                // Every input is submitted even when an output is missing, so
                // the stream stays whole for the next Unique Frame.
                if (s.Submit(stage, input, late_) && s.Receive(stage, input, s.eighths[i], late_)) ++ready;
            }
            break;
        }
        }
        if (!s.failure.empty()) {
            EighthsFallBack(s.failure);
            return quarters;
        }
        return ReadyLadderFrames(quarters, ready);
    }

    void EighthsFallBack(std::string& reason) {
        Log("FrameGen: third FRC unavailable (%s), Target uses the x4 ladder", reason.c_str());
        eighths_ = false;
        failed_({GenerationOutcome::FallbackToQuadruple, std::exchange(reason, {})});
    }

    void FallBack(std::string& reason) {
        Log("FrameGen: second FRC unavailable (%s), using 2x", reason.c_str());
        four_ = false;
        eighths_ = false;  // the third FRC is fed by the second
        failed_({GenerationOutcome::FallbackToDouble, std::exchange(reason, {})});
    }

    int Fail(const char* reason) {
        Log("FrameGen: not available: %s", reason);
        failed_({GenerationOutcome::Failed, reason});
        return kCannotBeUsed;
    }
    bool four_;
    bool eighths_;
    int generated_ = 0;
    unsigned quarters_ = 0;
    unsigned eighthsMade_ = 0, eighthAttempts_ = 0, eighthLadders_ = 0;
    double eighthSum_ = 0, eighthWorst_ = 0;
    ComPtr<ID3D11Texture2D> warmup_;
    uint64_t warmupSequence_ = 0;
    unsigned startupFrames_ = 0, readyInputs_ = 0;
    bool reportedRecovery_ = false;
    std::function<void(GenerationNotice)> failed_;
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

std::shared_ptr<FrameGenerator> MakeAmfGenerator(bool four, std::function<void(GenerationNotice)> failed,
                                                 bool eighths) {
    return std::make_shared<AmfGenerator>(four, eighths, std::move(failed));
}

}  // namespace gp

#pragma once

#include <memory>
#include <string>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11Texture2D;

namespace gp {

// Sharp Upscale with AMD VideoSR (AMF HQScaler, in the AMD driver) instead of
// FSR 1 EASU. Presenting thread only. Each driver session is for one device,
// one input size and one output size; it starts on its own worker thread
// (setup time is unbounded), and until it is ready Scale returns false so the
// Presenter uses FSR 1 for that frame. A size held for kSettleMs only starts
// a session, so dragging a window edge does not start one per step.
// A failure turns VideoSR off for the run (logged); FSR 1 stays.
class AmfUpscaler {
public:
    static constexpr unsigned kSettleMs = 300;

    AmfUpscaler();
    ~AmfUpscaler();

    // source: BGRA at the Feed size. dest: BGRA at the shown size. True: dest
    // holds the scaled picture. Either way the context's state may be cleared:
    // the caller rebinds after every call.
    bool Scale(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11Texture2D* source,
               ID3D11Texture2D* dest);
    // The device is going away.
    void Release();
    bool Failed() const { return failed_; }
    // "VideoSR 1.1" or "VideoSR 1.0" once a session is ready, else empty.
    const std::string& Algorithm() const { return algorithm_; }

private:
    struct Session;
    void Fail(const std::string& reason);
    std::shared_ptr<Session> session_;
    bool failed_ = false;
    std::string algorithm_;
    unsigned pendingWidth_ = 0, pendingHeight_ = 0;
    unsigned long long pendingSince_ = 0;
    unsigned long long sequence_ = 0;
    unsigned long long scaled_ = 0, missed_ = 0, lastLog_ = 0;
    double sumMs_ = 0, worstMs_ = 0;
};

}  // namespace gp

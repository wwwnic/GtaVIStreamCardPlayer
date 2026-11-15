// Source: the first stage of the pipeline (Source -> Processor -> Presenter).
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "core/capture_choice.h"
#include "core/frame.h"

namespace gp {

// Where in opening and reading the Capture Card something happened.
enum class SourceStage {
    Enumerate,  // listing capture devices
    Open,       // activating the chosen device
    Reader,     // creating the Source Reader
    Mode,       // choosing and setting the capture mode
    FirstRead,  // the first frame request after opening (another program holding the card fails here)
    Read,       // a later frame request: the stream was lost
};

inline const char* NameOf(SourceStage stage) {
    switch (stage) {
    case SourceStage::Enumerate: return "listing devices";
    case SourceStage::Open: return "opening the device";
    case SourceStage::Reader: return "creating the reader";
    case SourceStage::Mode: return "setting the mode";
    case SourceStage::FirstRead: return "first read";
    case SourceStage::Read: return "reading";
    }
    return "?";
}

// What the Source tells the rest of the Viewer. It never draws anything.
struct SourceEvent {
    enum class Kind {
        Opened,    // device open, mode set, first frame requested
        NoDevice,  // no video capture device present
        Failed,    // `code` (an HRESULT) at `stage`
    };
    Kind kind = Kind::Failed;
    SourceStage stage = SourceStage::Open;
    uint32_t code = 0;
    bool operator==(const SourceEvent&) const = default;
};

// Produces the Feed picture as frames stamped with their arrival time.
// The rest of the Viewer only knows this interface, so the Media Foundation
// implementation could be replaced (ADR 0001 keeps DirectShow as the fallback).
class Source {
public:
    // Called once per captured frame, on a thread the Source owns, one call at
    // a time and in arrival order. It must return quickly and must not call
    // back into the Source.
    using FrameSink = std::function<void(FramePtr)>;
    // Called during Start on the calling thread (NoDevice, or Failed, or
    // Opened), and after Opened at most one Failed, from either thread, if
    // reading fails. Must return quickly; may run before Start returns.
    using EventSink = std::function<void(const SourceEvent&)>;

    // Values the Viewer may use to tell "no signal", collected since the
    // previous TakeProbe (logged once a second).
    struct Probe {
        uint32_t frames = 0;             // samples with a picture
        uint32_t streamTicks = 0;        // gaps reported by the reader (MF_SOURCE_READERF_STREAMTICK)
        uint32_t typeChanges = 0;        // native or current media type changed
        uint32_t discontinuities = 0;    // samples flagged as discontinuous
        // Luma of the latest frame on a coarse grid (0-255), -1 when none.
        int lumaMin = -1, lumaMax = -1, lumaMean = -1;
    };

    virtual ~Source() = default;

    // Finds the Capture Card, picks its mode and starts delivering frames.
    // Returns false when a failure was reported during Start.
    virtual bool Start(FrameSink frames, EventSink events) = 0;

    // Stops capture and releases the device. Neither sink is called after
    // Stop returns. Safe to call when not started; Start may follow.
    virtual void Stop() = 0;

    // Name of the Capture Card chosen by Start, even if it could not be
    // opened; empty when there is none. The sound is found by this name.
    virtual std::wstring DeviceName() const = 0;
    // That device's stable identifier (its symbolic link); empty when none.
    virtual std::wstring DeviceId() const = 0;

    // The menu's overrides, used from the next Start: a Capture Card by
    // identifier or name and a resolution (empty: the automatic choices,
    // capture_choice.h).
    struct Choice {
        std::wstring device;
        std::wstring deviceId;  // tried before the name; empty: by name only
        std::optional<Resolution> resolution;
    };
    virtual void Choose(const Choice& choice) = 0;
    // The chosen device's mode list as of the last Start (empty if none was read).
    virtual std::vector<CaptureMode> Modes() const = 0;
    // The mode capturing now; empty when not capturing.
    virtual std::optional<CaptureMode> CurrentMode() const = 0;

    // Like every call here: from the thread that calls Start and Stop.
    virtual Probe TakeProbe() = 0;
};

}  // namespace gp

// Source: the first stage of the pipeline (Source -> Processor -> Presenter).
#pragma once

#include <functional>

#include "core/frame.h"

namespace gp {

// Produces the Feed picture as frames stamped with their arrival time.
// The rest of the Viewer only knows this interface, so the Media Foundation
// implementation could be replaced (ADR 0001 keeps DirectShow as the fallback).
class Source {
public:
    // Called once per captured frame, on a thread the Source owns, one call at
    // a time and in arrival order. It must return quickly and must not call
    // back into the Source.
    using FrameSink = std::function<void(FramePtr)>;

    virtual ~Source() = default;

    // Finds the Capture Card, picks its mode and starts delivering frames.
    // Returns false if no Feed can be captured (no device, device busy, no
    // usable mode); the reason goes to the debug log.
    virtual bool Start(FrameSink sink) = 0;

    // Stops capture and releases the device. The sink is not called after
    // Stop returns. Safe to call when not started.
    virtual void Stop() = 0;
};

}  // namespace gp

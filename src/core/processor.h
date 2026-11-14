// Processor: the middle stage of the pipeline (Source -> Processor -> Presenter).
#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "core/frame.h"

namespace gp {

// A frame the Presenter should show, and when.
struct TimedFrame {
    FramePtr frame;
    // QueryPerformanceCounter time to show it at. A time not in the future
    // means "as soon as possible".
    int64_t showAtQpc = 0;
};

// Takes each captured frame and emits zero or more frames to show.
// Zero: the frame is dropped (ticket 04 drops Repeated Frames here).
// More than one: extra frames were made (in-app Frame Generation would live here).
// An implementation that needs earlier Unique Frames keeps their FramePtr;
// frames are immutable and shared, so that costs no copy.
// Process is called on the Source's thread, one frame at a time, in arrival order.
class Processor {
public:
    virtual ~Processor() = default;
    // Appends the frames to show to `out` (which arrives empty).
    virtual void Process(FramePtr captured, std::vector<TimedFrame>& out) = 0;
};

// Shows every captured frame unchanged, as soon as possible.
std::unique_ptr<Processor> MakePassThroughProcessor();

}  // namespace gp

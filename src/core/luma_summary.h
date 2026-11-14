// Brightness of a frame on a coarse grid. Logged once a second to learn what
// the Capture Card sends with no HDMI signal (a uniform picture has min == max).
// Never used to decide "no signal": a dark or paused game can look the same.
#pragma once

#include "core/frame.h"

namespace gp {

struct LumaSummary {
    int min = -1, max = -1, mean = -1;  // 0-255; -1 when the frame has no picture
};

LumaSummary SummarizeLuma(const Frame& frame);

}  // namespace gp

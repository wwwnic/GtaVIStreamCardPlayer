// YUV to RGB conversion numbers for the Presenter's pixel shader.
#pragma once

#include <array>

#include "core/frame.h"

namespace gp {

// rgb = rows * (y, u, v, 1), with y, u, v as the 0..1 values a UNORM texture
// returns for the stored bytes. Results outside 0..1 are for the caller to clamp.
struct YuvToRgb {
    std::array<std::array<float, 4>, 3> rows{};

    std::array<float, 3> Apply(float y, float u, float v) const;
};

// Conversion for a frame's colour description. Anything the Capture Card
// left unspecified is taken as limited range and BT.709 (HD video defaults).
YuvToRgb MakeYuvToRgb(Colour colour);

}  // namespace gp

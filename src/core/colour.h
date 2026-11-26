// YUV to RGB conversion numbers for the Presenter's pixel shader.
#pragma once

#include <array>
#include <string_view>

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

// The R key: the user's answer when the Capture Card states the range wrongly.
// Auto: as the card says (limited when it says nothing). A washed-out
// picture wants Full -> Limited; crushed blacks want Limited -> Full.
enum class RangeOverride { Auto, Limited, Full };

inline Colour WithRange(Colour colour, RangeOverride range) {
    if (range == RangeOverride::Limited) colour.range = ColourRange::Limited;
    if (range == RangeOverride::Full) colour.range = ColourRange::Full;
    return colour;
}

inline RangeOverride NextRange(RangeOverride range) {
    switch (range) {
    case RangeOverride::Auto: return RangeOverride::Limited;
    case RangeOverride::Limited: return RangeOverride::Full;
    case RangeOverride::Full: break;
    }
    return RangeOverride::Auto;
}

// Its word in the settings file and the log: auto, limited, full.
inline const char* RangeWord(RangeOverride range) {
    return range == RangeOverride::Limited ? "limited" : range == RangeOverride::Full ? "full" : "auto";
}

// The confirmation line for R.
inline const char* RangeLine(RangeOverride range) {
    return range == RangeOverride::Limited ? "Colour range limited"
           : range == RangeOverride::Full  ? "Colour range full"
                                           : "Colour range auto";
}

// Anything but limited or full: Auto.
inline RangeOverride RangeOf(std::string_view word) {
    if (word == "limited") return RangeOverride::Limited;
    if (word == "full") return RangeOverride::Full;
    return RangeOverride::Auto;
}

}  // namespace gp

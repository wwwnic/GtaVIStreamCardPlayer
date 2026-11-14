// Text drawn by the Viewer inside the Video Area: what it says and how big.
// The drawing itself is the Presenter's (Presenter::ShowText).
#pragma once

#include <string>

namespace gp {

// Where a text sits. Each place holds one text at a time.
enum class TextPlace {
    Centre,  // the Video Area state message (ticket 06); hides nothing by itself
    Top,     // brief feedback, e.g. volume or Audio Delay (ticket 07)
    Bottom,  // a small lasting note, e.g. "no sound"
};

// One line saying what is wrong, optionally a smaller line saying what to do.
// Light text, centred. Empty headline and detail: nothing shown.
struct VideoAreaText {
    std::wstring headline;
    std::wstring detail;
    bool Empty() const { return headline.empty() && detail.empty(); }
    bool operator==(const VideoAreaText&) const = default;
};

// Font sizes and margins, in pixels, for a Video Area of this size.
struct TextSizes {
    static constexpr float kMinHeadline = 14.0f;
    static constexpr float kMinDetail = 11.0f;
    float headline = 0;
    float detail = 0;
    float margin = 0;    // from the Video Area's edges
    float maxWidth = 0;  // lines wrap at this width
};

TextSizes SizeText(TextPlace place, int areaWidth, int areaHeight);

}  // namespace gp

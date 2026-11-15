#include "core/window_shape.h"

#include <algorithm>

namespace gp {

namespace {

// One unit is a 16 x 9 pixel block: a Video Area of n units is 16n x 9n.
int NearestUnits(int pixels, int unitPixels) { return (std::max(pixels, 0) + unitPixels / 2) / unitPixels; }

bool MovesLeft(SizingEdge e) {
    return e == SizingEdge::Left || e == SizingEdge::TopLeft || e == SizingEdge::BottomLeft;
}
bool MovesTop(SizingEdge e) { return e == SizingEdge::Top || e == SizingEdge::TopLeft || e == SizingEdge::TopRight; }

}  // namespace

WindowRect KeepVideoArea16x9(WindowRect proposed, SizingEdge edge, int frameWidth, int frameHeight,
                             int minVideoAreaWidth) {
    const int byWidth = NearestUnits(proposed.right - proposed.left - frameWidth, 16);
    const int byHeight = NearestUnits(proposed.bottom - proposed.top - frameHeight, 9);
    int units;
    switch (edge) {
    case SizingEdge::Left:
    case SizingEdge::Right: units = byWidth; break;
    case SizingEdge::Top:
    case SizingEdge::Bottom: units = byHeight; break;
    default: units = std::max(byWidth, byHeight); break;
    }
    const int minUnits = (std::max(minVideoAreaWidth, 16) + 15) / 16;
    units = std::max(units, minUnits);

    const int width = units * 16 + frameWidth;
    const int height = units * 9 + frameHeight;
    WindowRect r = proposed;
    if (MovesLeft(edge)) r.left = r.right - width;
    else r.right = r.left + width;
    if (MovesTop(edge)) r.top = r.bottom - height;
    else r.bottom = r.top + height;
    return r;
}

std::optional<WindowRect> RestoredWindowRect(WindowRect saved, std::span<const WindowRect> monitors, int frameWidth,
                                             int frameHeight, int minVideoAreaWidth, int slack) {
    const int minWidth = std::max(minVideoAreaWidth, 16);
    const bool tooSmall = saved.right - saved.left - frameWidth < minWidth ||
                          saved.bottom - saved.top - frameHeight < minWidth * 9 / 16;
    const WindowRect r =
        tooSmall ? KeepVideoArea16x9(saved, SizingEdge::Right, frameWidth, frameHeight, minVideoAreaWidth) : saved;
    for (const WindowRect& m : monitors) {
        if (r.left >= m.left - slack && r.top >= m.top - slack && r.right <= m.right + slack &&
            r.bottom <= m.bottom + slack) {
            return r;
        }
    }
    return std::nullopt;
}

}  // namespace gp

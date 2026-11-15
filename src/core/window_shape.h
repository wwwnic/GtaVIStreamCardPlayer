// The windowed Viewer keeps its Video Area exactly 16:9 while an edge or
// corner is dragged (WM_SIZING). Maximised and fullscreen are not shaped
// here; there, and after snaps or programmatic resizes, the picture keeps
// its shape with black bars (core/picture_fit.h).
#pragma once

namespace gp {

// Which edge or corner is being dragged (WMSZ_*).
enum class SizingEdge { Left, Right, Top, Bottom, TopLeft, TopRight, BottomLeft, BottomRight };

// A window rectangle in screen pixels, frame included.
struct WindowRect {
    int left = 0, top = 0, right = 0, bottom = 0;
    bool operator==(const WindowRect&) const = default;
};

// The window rectangle to use instead of `proposed` so the Video Area (the
// window minus its frame: borders, title bar, menu bar if any) is exactly
// 16:9 in whole pixels (its width a multiple of 16) and at least
// `minVideoAreaWidth` wide. A side edge leads with the width, a top or bottom
// edge with the height, a corner with whichever went further. Only the
// dragged edges move; the opposite edges stay put.
WindowRect KeepVideoArea16x9(WindowRect proposed, SizingEdge edge, int frameWidth, int frameHeight,
                             int minVideoAreaWidth);

}  // namespace gp

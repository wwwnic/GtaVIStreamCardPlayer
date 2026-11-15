// The windowed Viewer keeps its Video Area exactly 16:9 while an edge or
// corner is dragged (WM_SIZING). Maximised and fullscreen are not shaped
// here; there, and after snaps or programmatic resizes, the picture keeps
// its shape with black bars (core/picture_fit.h).
#pragma once

#include <optional>
#include <span>

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

// Start-up (ticket 09): the window rectangle to reopen at from a remembered
// one. Shaped like a right-edge drag (16:9 from the width, top-left kept),
// then kept only if it lies fully on one of `monitors` (their full
// rectangles), give or take `slack` pixels on each side. Empty: the monitor
// is gone or the window would be off-screen; use the default instead.
std::optional<WindowRect> RestoredWindowRect(WindowRect saved, std::span<const WindowRect> monitors, int frameWidth,
                                             int frameHeight, int minVideoAreaWidth, int slack);

}  // namespace gp

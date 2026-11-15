// When the mouse cursor is hidden, VLC-style: after 2 seconds without mouse
// movement. In fullscreen, anywhere. Windowed, only while it rests over the
// Video Area of the active window with no menu open and no move or resize
// going on; anywhere else it shows at once. Times are in milliseconds.
#pragma once

#include <cstdint>
#include <optional>

namespace gp {

// Where the cursor is and what the window is doing (windowed only).
struct CursorPlace {
    bool overVideoArea = false;   // over the client area, not the title bar, menu bar or borders, nor another window
    bool windowActive = false;    // the Viewer is the foreground window
    bool menuOpen = false;
    bool movingOrSizing = false;  // a title-bar drag or a border drag
};

class CursorIdle {
public:
    static constexpr int64_t kHideAfterMs = 2000;

    void SetFullscreen(bool fullscreen, int64_t nowMs);
    // A mouse-move report, in screen coordinates (client coordinates shift
    // when the window moves). The same position as last time is not movement.
    void MouseAt(int x, int y, int64_t nowMs);
    // Call regularly; hides the cursor once it has been still long enough,
    // shows it when `place` no longer allows hiding (windowed).
    void Tick(int64_t nowMs, const CursorPlace& place = {});
    bool Hidden() const { return hidden_; }

private:
    struct Point {
        int x, y;
    };
    bool fullscreen_ = false;
    bool hidden_ = false;
    int64_t lastMoveMs_ = 0;
    std::optional<Point> last_;
};

}  // namespace gp

// When the mouse cursor is hidden: in fullscreen, after 2 seconds without
// mouse movement; never in windowed mode. Times are in milliseconds.
#pragma once

#include <cstdint>
#include <optional>

namespace gp {

class CursorIdle {
public:
    static constexpr int64_t kHideAfterMs = 2000;

    void SetFullscreen(bool fullscreen, int64_t nowMs);
    // A mouse-move report, in screen coordinates (client coordinates shift
    // when the window moves). The same position as last time is not movement.
    void MouseAt(int x, int y, int64_t nowMs);
    // Call regularly; hides the cursor once it has been still long enough.
    void Tick(int64_t nowMs);
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

#include "core/cursor_idle.h"

namespace gp {

void CursorIdle::SetFullscreen(bool fullscreen, int64_t nowMs) {
    fullscreen_ = fullscreen;
    hidden_ = false;
    lastMoveMs_ = nowMs;
}

void CursorIdle::MouseAt(int x, int y, int64_t nowMs) {
    if (last_ && last_->x == x && last_->y == y) return;
    last_ = Point{x, y};
    lastMoveMs_ = nowMs;
    hidden_ = false;
}

void CursorIdle::Tick(int64_t nowMs, const CursorPlace& place) {
    const bool mayHide =
        fullscreen_ || (place.overVideoArea && place.windowActive && !place.menuOpen && !place.movingOrSizing);
    if (!mayHide) {  // shown, and the 2 seconds start again once hiding is allowed
        hidden_ = false;
        lastMoveMs_ = nowMs;
        return;
    }
    if (nowMs - lastMoveMs_ >= kHideAfterMs) hidden_ = true;
}

}  // namespace gp

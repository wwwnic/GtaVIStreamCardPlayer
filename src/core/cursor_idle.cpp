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

void CursorIdle::Tick(int64_t nowMs) {
    if (fullscreen_ && nowMs - lastMoveMs_ >= kHideAfterMs) hidden_ = true;
}

}  // namespace gp

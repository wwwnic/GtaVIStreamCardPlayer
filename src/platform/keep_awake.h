// Keeps the PC, the display and the screensaver off while a Feed plays
// (controller input on the Console is not PC activity). Use from the
// window's thread only: the request belongs to the calling thread.
#pragma once

#include <windows.h>

#include "core/stay_awake.h"

namespace gp {

class KeepAwake {
public:
    ~KeepAwake() { Release("exit"); }

    // Call once a second: did any frame arrive during that second?
    void Second(bool framesArrived);
    // Back to normal sleep behaviour now (on exit).
    void Release(const char* why);
    // WM_SYSCOMMAND: while held, the screensaver is refused too (it is only
    // asked when GamePlayer is in front). True: swallow the command.
    bool RefuseSysCommand(WPARAM command) const;

private:
    StayAwake state_;
    bool requested_ = false;
};

}  // namespace gp

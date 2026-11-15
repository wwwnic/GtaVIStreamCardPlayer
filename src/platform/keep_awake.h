// Keeps the PC, the display and the screensaver off while a Feed plays
// (controller input on the Console is not PC activity). Use from the
// window's thread only: SetThreadExecutionState belongs to the calling thread.
//
// Two holds, so either alone is enough: a power request (PowerSetRequest,
// shown by `powercfg /requests` as "GamePlayer: a Feed is playing") and the
// thread's execution state. While held, the idle timers are also reset each
// second. Display on / off / dimmed changes are logged, so a display that
// turns off anyway is on record.
#pragma once

#include <windows.h>

#include "core/stay_awake.h"

namespace gp {

class KeepAwake {
public:
    ~KeepAwake();

    // Listen for display on / off / dimmed (logged). Optional.
    void Start(HWND window);
    // Call once a second: did any frame arrive during that second?
    void Second(bool framesArrived);
    // Back to normal sleep behaviour now (on exit).
    void Release(const char* why);
    // WM_SYSCOMMAND: while held, the screensaver is refused too (it is only
    // asked when GamePlayer is in front). True: swallow the command.
    bool RefuseSysCommand(WPARAM command) const;
    // WM_POWERBROADCAST, observed only (never consumes it).
    void PowerBroadcast(WPARAM wParam, LPARAM lParam);

private:
    void Take();

    StayAwake state_;
    bool requested_ = false;
    HANDLE request_ = nullptr;
    HPOWERNOTIFY displayNotification_ = nullptr;
    int heldSeconds_ = 0;
};

}  // namespace gp

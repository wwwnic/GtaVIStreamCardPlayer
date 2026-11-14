#include "platform/single_instance.h"

namespace gp {

bool ClaimSingleInstance(const wchar_t* windowClass) {
    // Never closed: released by Windows when the process ends.
    HANDLE claim = CreateMutexW(nullptr, FALSE, L"Local\\GamePlayer.Viewer");
    if (!claim || GetLastError() != ERROR_ALREADY_EXISTS) return true;

    // The first copy may still be creating its window; wait briefly.
    HWND running = nullptr;
    for (int i = 0; i < 30 && !running; i++) {
        running = FindWindowW(windowClass, nullptr);
        if (!running) Sleep(100);
    }
    if (running) {
        // This process was just started by the user, so it may give focus away.
        if (IsIconic(running)) ShowWindow(running, SW_RESTORE);
        SetForegroundWindow(running);
        PostMessageW(running, AnotherLaunchMessage(), 0, 0);
    }
    return false;
}

UINT AnotherLaunchMessage() {
    static const UINT message = RegisterWindowMessageW(L"GamePlayer.AnotherLaunch");
    return message;
}

}  // namespace gp

#include "platform/single_instance.h"

namespace gp {

namespace {

// How long another launch waits for the running copy to show its window
// (a slow start) or to end (closing: its window already hidden).
const int kWaitSteps = 100;  // of 100 ms: 10 s
const DWORD kStepMs = 100;

}  // namespace

bool ClaimSingleInstance(const wchar_t* windowClass) {
    // Owned by this thread until the process ends (never closed), so another
    // launch can tell a closing copy from a running one.
    HANDLE claim = CreateMutexW(nullptr, TRUE, L"Local\\GamePlayer.Viewer");
    const DWORD error = GetLastError();
    // Denied: it exists but was made by a copy with other rights (run as
    // administrator), so a Viewer is running. Any other failure: start anyway.
    if (!claim && error != ERROR_ACCESS_DENIED) return true;
    if (claim && error != ERROR_ALREADY_EXISTS) return true;

    for (int i = 0; i < kWaitSteps; i++) {
        if (claim) {
            const DWORD ended = WaitForSingleObject(claim, 0);
            // WAIT_ABANDONED: the running copy ended; this one takes over.
            if (ended == WAIT_OBJECT_0 || ended == WAIT_ABANDONED) return true;
        }
        // Hidden: still starting (shown once ready) or closing (ends soon).
        HWND running = FindWindowW(windowClass, nullptr);
        if (running && IsWindowVisible(running)) {
            // This process was just started by the user, so it may give focus away.
            if (IsIconic(running)) ShowWindow(running, SW_RESTORE);
            SetForegroundWindow(running);
            PostMessageW(running, AnotherLaunchMessage(), 0, 0);
            return false;
        }
        Sleep(kStepMs);
    }
    return false;  // a copy runs but never showed its window: still no second copy
}

UINT AnotherLaunchMessage() {
    static const UINT message = RegisterWindowMessageW(L"GamePlayer.AnotherLaunch");
    return message;
}

}  // namespace gp

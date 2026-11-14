// Borderless fullscreen for the Viewer window, and the mouse cursor that
// hides in it. Never exclusive fullscreen: Lossless Scaling cannot work on
// it (ADR 0002), and a borderless flip-model window gets the same low-delay
// presentation path on Windows 11.
#pragma once

#include <windows.h>

#include <string>

#include "core/cursor_idle.h"
#include "core/monitor_choice.h"

namespace gp {

class Fullscreen {
public:
    explicit Fullscreen(HWND window) : window_(window) {}

    // F11 toggles, Esc leaves, the cursor hides after 2 s still in fullscreen,
    // WM_DISPLAYCHANGE keeps the monitor covered. Returns true when the
    // message was handled and *result is the answer.
    bool HandleMessage(UINT message, WPARAM wParam, LPARAM lParam, LRESULT* result);

    bool IsOn() const { return on_; }
    void Toggle();
    void Leave();

private:
    void Enter();
    void DisplayChanged();
    void ShowChrome(bool shown);
    void Cover(const ScreenRect& bounds);
    void SetCursorShown(bool shown);
    void LogClientSize(const char* what);

    HWND window_;
    bool on_ = false;
    WINDOWPLACEMENT saved_ = {sizeof saved_};  // position, size and maximised state to return to
    LONG_PTR savedStyle_ = 0;
    std::wstring monitorName_;  // the monitor being covered
    CursorIdle cursor_;
};

}  // namespace gp

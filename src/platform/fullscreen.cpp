#include "platform/fullscreen.h"

#include <vector>

#include "platform/log.h"

namespace gp {

namespace {

// Clear of main.cpp's timer ids.
const UINT_PTR kCursorTimer = 0x0501;
const UINT kCursorTimerMs = 250;

int64_t NowMs() { return static_cast<int64_t>(GetTickCount64()); }

ScreenRect ToScreenRect(const RECT& r) { return {r.left, r.top, r.right, r.bottom}; }

BOOL CALLBACK AddMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM list) {
    MONITORINFOEXW info = {};
    info.cbSize = sizeof info;
    if (GetMonitorInfoW(monitor, &info)) {
        reinterpret_cast<std::vector<Monitor>*>(list)->push_back({info.szDevice, ToScreenRect(info.rcMonitor)});
    }
    return TRUE;
}

std::vector<Monitor> ListMonitors() {
    std::vector<Monitor> monitors;
    EnumDisplayMonitors(nullptr, nullptr, AddMonitor, reinterpret_cast<LPARAM>(&monitors));
    return monitors;
}

}  // namespace

Fullscreen::Fullscreen(HWND window) : window_(window) {
    // The cursor hides windowed too (over a still Video Area), so this always runs.
    SetTimer(window_, kCursorTimer, kCursorTimerMs, nullptr);
}

CursorPlace Fullscreen::PlaceNow() const {
    CursorPlace place;
    POINT at;
    GetCursorPos(&at);
    if (WindowFromPoint(at) == window_) {
        POINT inside = at;
        ScreenToClient(window_, &inside);
        RECT client;
        GetClientRect(window_, &client);
        place.overVideoArea = PtInRect(&client, inside) != FALSE;
    }
    place.windowActive = GetForegroundWindow() == window_;
    place.menuOpen = menuOpen_;
    place.movingOrSizing = movingOrSizing_;
    return place;
}

bool Fullscreen::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam, LRESULT* result) {
    switch (message) {
    case WM_ENTERMENULOOP:
    case WM_EXITMENULOOP:
        menuOpen_ = message == WM_ENTERMENULOOP;
        if (menuOpen_) ShowCursorAgain();
        return false;  // main.cpp logs these
    case WM_ENTERSIZEMOVE:
    case WM_EXITSIZEMOVE:
        movingOrSizing_ = message == WM_ENTERSIZEMOVE;
        if (movingOrSizing_) ShowCursorAgain();
        return false;
    case WM_NCMOUSEMOVE: {  // over the title bar, menu bar or a border
        POINT at;
        GetCursorPos(&at);
        cursor_.MouseAt(at.x, at.y, NowMs());
        return false;
    }
    case WM_KEYDOWN: {
        const bool repeat = (lParam & (1 << 30)) != 0;  // key held down
        if (wParam == VK_F11) {
            if (!repeat) Toggle();
        } else if (wParam == VK_ESCAPE) {
            if (on_) Leave();  // does nothing when windowed
        } else {
            return false;
        }
        *result = 0;
        return true;
    }
    case WM_MOUSEMOVE: {
        POINT at;
        GetCursorPos(&at);
        const bool wasHidden = cursor_.Hidden();
        cursor_.MouseAt(at.x, at.y, NowMs());
        if (wasHidden && !cursor_.Hidden()) SetCursorShown(true);
        return false;
    }
    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT && cursor_.Hidden()) {
            SetCursor(nullptr);
            *result = TRUE;
            return true;
        }
        return false;
    case WM_TIMER:
    {
        if (wParam != kCursorTimer) return false;
        const bool wasHidden = cursor_.Hidden();
        cursor_.Tick(NowMs(), PlaceNow());
        if (cursor_.Hidden() != wasHidden) SetCursorShown(!cursor_.Hidden());
        *result = 0;
        return true;
    }
    case WM_DISPLAYCHANGE:
        DisplayChanged();
        return false;
    }
    return false;
}

void Fullscreen::Toggle() {
    if (on_) {
        Leave();
    } else {
        Enter();
    }
}

void Fullscreen::Enter() {
    if (on_ || IsIconic(window_)) return;
    RECT windowRect;
    GetWindowRect(window_, &windowRect);
    const std::vector<Monitor> monitors = ListMonitors();
    const std::optional<size_t> target = MonitorForWindow(ToScreenRect(windowRect), monitors);
    if (!target) {
        Log("Fullscreen: no monitor found; staying windowed");
        return;
    }
    saved_.length = sizeof saved_;
    GetWindowPlacement(window_, &saved_);
    const Monitor& monitor = monitors[*target];
    monitorName_ = monitor.name;
    on_ = true;
    ShowChrome(false);
    Cover(monitor.bounds);
    cursor_.SetFullscreen(true, NowMs());
    Log("Fullscreen: on, monitor %s (%d of %zu) at %d,%d %dx%d%s", ToUtf8(monitor.name).c_str(), static_cast<int>(*target) + 1,
        monitors.size(), monitor.bounds.left, monitor.bounds.top, monitor.bounds.right - monitor.bounds.left,
        monitor.bounds.bottom - monitor.bounds.top, saved_.showCmd == SW_SHOWMAXIMIZED ? ", was maximised" : "");
    LogClientSize("on");
}

void Fullscreen::Leave() {
    if (!on_) return;
    on_ = false;
    cursor_.SetFullscreen(false, NowMs());
    SetCursorShown(true);
    ShowChrome(true);
    // Puts the window back where, how big and how maximised it was. If that
    // place is now off every monitor, Windows moves it onto one.
    SetWindowPlacement(window_, &saved_);
    SetWindowPos(window_, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    const RECT& r = saved_.rcNormalPosition;
    Log("Fullscreen: off, window back at %d,%d %dx%d%s", r.left, r.top, r.right - r.left, r.bottom - r.top,
        saved_.showCmd == SW_SHOWMAXIMIZED ? ", maximised" : "");
    LogClientSize("off");
}

// Monitor unplugged or resolution changed: keep covering the same monitor at
// its new size, or go back to windowed if it is gone.
void Fullscreen::DisplayChanged() {
    if (!on_) return;
    const std::vector<Monitor> monitors = ListMonitors();
    const std::optional<size_t> same = FindMonitor(monitorName_, monitors);
    if (!same) {
        Log("Fullscreen: display change, monitor %s gone; back to windowed", ToUtf8(monitorName_).c_str());
        Leave();
        return;
    }
    const ScreenRect& bounds = monitors[*same].bounds;
    Log("Fullscreen: display change, still covering %s at %d,%d %dx%d", ToUtf8(monitorName_).c_str(), bounds.left,
        bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top);
    Cover(bounds);
}

// The one place window chrome is hidden and restored: borders, title bar,
// the menu bar and the topmost flag.
void Fullscreen::ShowChrome(bool shown) {
    if (shown) {
        SetWindowLongPtrW(window_, GWL_STYLE, savedStyle_);
        if (menu_) SetMenu(window_, menu_);
        if (alwaysOnTop_) ApplyTopmost(true);
    } else {
        if (alwaysOnTop_) ApplyTopmost(false);
        if (menu_) SetMenu(window_, nullptr);
        savedStyle_ = GetWindowLongPtrW(window_, GWL_STYLE);
        SetWindowLongPtrW(window_, GWL_STYLE, (savedStyle_ & ~static_cast<LONG_PTR>(WS_OVERLAPPEDWINDOW)) | WS_POPUP);
    }
}

void Fullscreen::SetAlwaysOnTop(bool on) {
    alwaysOnTop_ = on;
    if (!on_) ApplyTopmost(on);  // in fullscreen it applies on leaving
    Log("Window: always on top %s%s", on ? "on" : "off", on_ ? " (from leaving fullscreen)" : "");
}

void Fullscreen::ApplyTopmost(bool topmost) {
    SetWindowPos(window_, topmost ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
}

void Fullscreen::Cover(const ScreenRect& bounds) {
    SetWindowPos(window_, HWND_TOP, bounds.left, bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top,
                 SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
}

// WM_SETCURSOR keeps the cursor as chosen; this changes it at once when the
// mouse is over the window, instead of at the next mouse message.
// Only over the Video Area: elsewhere (borders, title bar, other windows)
// Windows picks the right cursor itself.
void Fullscreen::SetCursorShown(bool shown) {
    if (!PlaceNow().overVideoArea) return;
    SetCursor(shown ? LoadCursorW(nullptr, IDC_ARROW) : nullptr);
}

// A menu opened or a drag began: shown at once, not at the next tick.
void Fullscreen::ShowCursorAgain() {
    if (!cursor_.Hidden() || on_) return;
    cursor_.Tick(NowMs(), PlaceNow());
    if (!cursor_.Hidden()) SetCursorShown(true);
}

void Fullscreen::LogClientSize(const char* what) {
    RECT client;
    GetClientRect(window_, &client);
    Log("Fullscreen: %s, Video Area now %ldx%ld", what, client.right, client.bottom);
}

}  // namespace gp

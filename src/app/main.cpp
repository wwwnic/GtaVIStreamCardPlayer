// GamePlayer, the Viewer: shows the Console's Feed from the Capture Card.
// This file is the window; FeedKeeper wires Source -> Processor -> Presenter.
//
// Arguments: --run N   close by itself after N seconds (unattended smoke runs).
// Keys: F11 toggles borderless fullscreen, Esc leaves it. Up / Down volume,
// M mute, J / K Audio Delay (sound earlier / later).
#include <windows.h>

#include <objbase.h>
#include <shellapi.h>

#include <algorithm>
#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "core/audio_controls.h"
#include "core/window_shape.h"
#include "platform/feed_keeper.h"
#include "platform/feedback.h"
#include "platform/fullscreen.h"
#include "platform/keep_awake.h"
#include "platform/log.h"
#include "platform/presenter.h"
#include "platform/single_instance.h"

namespace {

// Lossless Scaling matches its profile on this exact title; do not change it.
const wchar_t kWindowTitle[] = L"GamePlayer";
const wchar_t kWindowClass[] = L"GamePlayerWindow";
const int kDefaultVideoAreaWidth = 1280;  // 16:9 (720 high), shrunk if the monitor is smaller
const int kMinVideoAreaWidthAt96Dpi = 480;  // 270 high
const UINT_PTR kStatsTimer = 1;
const UINT_PTR kRunTimer = 2;

struct App {
    explicit App(HWND window) : feedback(presenter), fullscreen(window), feed(window, presenter, sound) {}
    gp::Presenter presenter;
    gp::Sound sound;  // started and stopped by the FeedKeeper; its settings persist
    gp::Feedback feedback;
    int statsLogged = 0;
    int secondsSinceStats = 0;
    gp::Fullscreen fullscreen;
    gp::KeepAwake keepAwake;
    gp::FeedKeeper feed;  // after the Presenter it feeds
};

// One line per second for the first ten seconds, then one per minute.
void LogStats(App& app) {
    app.secondsSinceStats++;
    if (app.statsLogged >= 10 && app.secondsSinceStats < 60) return;
    const double seconds = app.secondsSinceStats;
    const uint32_t arrived = app.feed.TakeFramesArrived();
    const gp::Presenter::Stats shown = app.presenter.TakeStats();
    gp::Log("Stats: arrived %.1f/s, presented %.1f/s, replaced before shown %u, arrival-to-present avg %.2f ms worst %.2f ms",
            arrived / seconds, shown.presented / seconds, shown.replaced, shown.averageLatencyMs, shown.worstLatencyMs);
    app.statsLogged++;
    app.secondsSinceStats = 0;
}

// Up, Down, M, J, K. Only arrive while the window has focus (both modes).
bool HandleAudioKey(App& app, WPARAM key, LPARAM flags) {
    gp::AudioKey audioKey;
    switch (key) {
    case VK_UP: audioKey = gp::AudioKey::VolumeUp; break;
    case VK_DOWN: audioKey = gp::AudioKey::VolumeDown; break;
    case 'M': audioKey = gp::AudioKey::ToggleMute; break;
    case 'J': audioKey = gp::AudioKey::DelayEarlier; break;
    case 'K': audioKey = gp::AudioKey::DelayLater; break;
    default: return false;
    }
    const bool repeat = (flags & (1 << 30)) != 0;
    if (repeat && audioKey == gp::AudioKey::ToggleMute) return true;  // held M toggles once
    gp::AudioSettings now;
    now.volumePercent = app.sound.Volume();
    now.muted = app.sound.Muted();
    now.audioDelayMs = app.sound.AudioDelayMs();
    const gp::AudioKeyResult pressed = gp::PressAudioKey(now, audioKey);
    if (pressed.settings.muted != now.muted) app.sound.SetMuted(pressed.settings.muted);
    if (pressed.settings.volumePercent != now.volumePercent) app.sound.SetVolume(pressed.settings.volumePercent);
    if (pressed.settings.audioDelayMs != now.audioDelayMs) app.sound.SetAudioDelayMs(pressed.settings.audioDelayMs);
    gp::Log("Key: %s", pressed.feedback.c_str());
    app.feedback.Show(pressed.feedback);
    return true;
}

// The window's frame: its size minus the Video Area (the client area). Includes
// the borders, the title bar and a menu bar if there is one; measured, not assumed.
SIZE FrameSize(HWND window) {
    RECT outer = {}, client = {};
    GetWindowRect(window, &outer);
    GetClientRect(window, &client);
    return {(outer.right - outer.left) - client.right, (outer.bottom - outer.top) - client.bottom};
}

int MinVideoAreaWidth(HWND window) {
    const int width = MulDiv(kMinVideoAreaWidthAt96Dpi, static_cast<int>(GetDpiForWindow(window)), 96);
    return (width + 15) / 16 * 16;  // whole 16:9 blocks
}

// WM_SIZING: while an edge or corner is dragged, keep the Video Area 16:9.
void KeepShapeWhileSizing(HWND window, WPARAM edge, RECT* proposed) {
    gp::SizingEdge sizing;
    switch (edge) {
    case WMSZ_LEFT: sizing = gp::SizingEdge::Left; break;
    case WMSZ_RIGHT: sizing = gp::SizingEdge::Right; break;
    case WMSZ_TOP: sizing = gp::SizingEdge::Top; break;
    case WMSZ_BOTTOM: sizing = gp::SizingEdge::Bottom; break;
    case WMSZ_TOPLEFT: sizing = gp::SizingEdge::TopLeft; break;
    case WMSZ_TOPRIGHT: sizing = gp::SizingEdge::TopRight; break;
    case WMSZ_BOTTOMLEFT: sizing = gp::SizingEdge::BottomLeft; break;
    case WMSZ_BOTTOMRIGHT: sizing = gp::SizingEdge::BottomRight; break;
    default: return;
    }
    const SIZE frame = FrameSize(window);
    const gp::WindowRect shaped = gp::KeepVideoArea16x9({proposed->left, proposed->top, proposed->right, proposed->bottom},
                                                        sizing, frame.cx, frame.cy, MinVideoAreaWidth(window));
    *proposed = {shaped.left, shaped.top, shaped.right, shaped.bottom};
}

// Opening size: a 16:9 Video Area, kDefaultVideoAreaWidth wide or as much as
// fits in 90% of the monitor's work area.
void SetDefaultSize(HWND window) {
    const SIZE frame = FrameSize(window);
    const int frameW = static_cast<int>(frame.cx), frameH = static_cast<int>(frame.cy);
    MONITORINFO monitor = {sizeof monitor};
    GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTOPRIMARY), &monitor);
    const int workWidth = monitor.rcWork.right - monitor.rcWork.left;
    const int workHeight = monitor.rcWork.bottom - monitor.rcWork.top;
    int units = kDefaultVideoAreaWidth / 16;
    units = std::min(units, (workWidth * 9 / 10 - frameW) / 16);
    units = std::min(units, (workHeight * 9 / 10 - frameH) / 9);
    units = std::max(units, MinVideoAreaWidth(window) / 16);
    SetWindowPos(window, nullptr, 0, 0, units * 16 + frameW, units * 9 + frameH,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    RECT client = {};
    GetClientRect(window, &client);
    gp::Log("Window: opening Video Area %ldx%ld (frame %dx%d, minimum width %d)", client.right, client.bottom, frameW,
            frameH, MinVideoAreaWidth(window));
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    App* app = reinterpret_cast<App*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    LRESULT handled = 0;
    if (app && app->fullscreen.HandleMessage(message, wParam, lParam, &handled)) return handled;
    if (app && app->feed.HandleMessage(message, wParam, lParam, &handled)) return handled;
    if (message == gp::AnotherLaunchMessage()) {
        gp::Log("Another launch: brought this window forward, no second copy");
        return 0;
    }
    switch (message) {
    case WM_KEYDOWN:
        if (app && HandleAudioKey(*app, wParam, lParam)) return 0;
        break;
    case WM_SIZING:
        // Windowed drags only: maximised and fullscreen have no sizing border.
        KeepShapeWhileSizing(window, wParam, reinterpret_cast<RECT*>(lParam));
        return TRUE;
    case WM_GETMINMAXINFO:
        if (app) {
            const SIZE frame = FrameSize(window);
            const int width = MinVideoAreaWidth(window);
            auto* limits = reinterpret_cast<MINMAXINFO*>(lParam);
            limits->ptMinTrackSize = {width + frame.cx, width / 16 * 9 + frame.cy};
            return 0;
        }
        break;
    case WM_SYSCOMMAND:
        if (app && app->keepAwake.RefuseSysCommand(wParam)) return 0;
        break;
    case WM_SIZE:
        // Also arrives continuously while an edge is dragged; the Presenter
        // repaints on its own thread, so the picture keeps its shape during the drag.
        // Snaps and programmatic sizes may give other shapes: black bars then.
        if (app && wParam != SIZE_MINIMIZED) app->presenter.VideoAreaResized();
        return 0;
    case WM_TIMER:
        if (wParam == kStatsTimer && app) {
            LogStats(*app);
            app->keepAwake.Second(app->feed.TakePlayingSecond());  // not while a state message shows
        }
        if (wParam == kRunTimer) PostMessageW(window, WM_CLOSE, 0, 0);
        return 0;
    case WM_CLOSE:
        // Leave the message loop first; the window is destroyed after the
        // pipeline has stopped, because the swap chain belongs to it.
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

int RunSecondsFromCommandLine() {
    int seconds = 0;
    int count = 0;
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!arguments) return 0;
    for (int i = 1; i + 1 < count; i++) {
        if (wcscmp(arguments[i], L"--run") == 0) seconds = _wtoi(arguments[i + 1]);
    }
    LocalFree(arguments);
    return seconds > 0 ? seconds : 0;
}

std::wstring LogPathBesideExecutable() {
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring folder = path;
    folder.resize(folder.find_last_of(L'\\') + 1);
    return folder + L"GamePlayer.log";
}

HWND CreateViewerWindow(HINSTANCE instance) {
    WNDCLASSEXW windowClass = {sizeof windowClass};
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));  // black until the first frame
    windowClass.lpszClassName = kWindowClass;
    RegisterClassExW(&windowClass);

    const DWORD style = WS_OVERLAPPEDWINDOW;
    RECT bounds = {0, 0, kDefaultVideoAreaWidth, kDefaultVideoAreaWidth / 16 * 9};
    AdjustWindowRectEx(&bounds, style, FALSE, 0);  // a first guess; SetDefaultSize measures the real frame
    HWND window = CreateWindowExW(0, kWindowClass, kWindowTitle, style, CW_USEDEFAULT, CW_USEDEFAULT,
                                  bounds.right - bounds.left, bounds.bottom - bounds.top, nullptr, nullptr, instance,
                                  nullptr);
    if (window) SetDefaultSize(window);
    return window;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    // Before the log opens, so a second launch leaves the running copy's log alone.
    if (!gp::ClaimSingleInstance(kWindowClass)) return 0;
    gp::LogOpen(LogPathBesideExecutable());
    const int runSeconds = RunSecondsFromCommandLine();
    gp::Log("GamePlayer starting%s", runSeconds ? " (--run: will close by itself)" : "");

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(comResult)) gp::Log("CoInitializeEx failed hr=0x%08lX", static_cast<unsigned long>(comResult));

    HWND window = CreateViewerWindow(instance);
    if (!window) {
        gp::Log("Could not create the window (error %lu)", GetLastError());
        return 1;
    }

    {
        App app(window);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&app));

        if (!app.presenter.Start(window)) gp::Log("No Presenter: the window stays black");
        app.feed.Start();  // shows Loading, then the Feed or why not; recovers by itself

        ShowWindow(window, showCommand);
        SetTimer(window, kStatsTimer, 1000, nullptr);
        if (runSeconds) SetTimer(window, kRunTimer, static_cast<UINT>(runSeconds) * 1000, nullptr);

        MSG message;
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }

        gp::Log("Closing");
        KillTimer(window, kStatsTimer);
        app.keepAwake.Release("exit");
        ShowWindow(window, SW_HIDE);  // disappear at once; teardown follows
        app.feed.Stop();  // sound and capture, before the Presenter they feed
        app.presenter.Stop();
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
    }
    DestroyWindow(window);
    if (SUCCEEDED(comResult)) CoUninitialize();
    gp::Log("Exited cleanly");
    gp::LogClose();
    return 0;
}

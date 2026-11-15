// GamePlayer, the Viewer: shows the Console's Feed from the Capture Card.
// This file is the window; FeedKeeper wires Source -> Processor -> Presenter.
//
// Arguments: --run N   close by itself after N seconds (unattended smoke runs).
// --tolerance X   near-duplicate tolerance for Repeated Frames (default 0, exact).
// --rate auto|30|60   the Game Rate override for this run (not remembered).
// --settings PATH   use this settings file instead of %APPDATA%\GamePlayer\settings.ini.
// Keys: F11 toggles borderless fullscreen, Esc leaves it. Up / Down volume,
// M mute, J / K Audio Delay (sound earlier / later). F the FPS counter.
// The menu bar (windowed only) overrides the automatic choices; all its
// settings live in App::settings. They and the windowed position are
// remembered between runs (ticket 09, core/settings_file.h).
#include <windows.h>

#include <objbase.h>
#include <shellapi.h>

#include <algorithm>
#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/audio_controls.h"
#include "core/menu_model.h"
#include "core/settings_file.h"
#include "core/viewer_settings.h"
#include "core/window_shape.h"
#include "platform/feed_keeper.h"
#include "platform/feedback.h"
#include "platform/fullscreen.h"
#include "platform/keep_awake.h"
#include "platform/log.h"
#include "platform/menu_bar.h"
#include "platform/presenter.h"
#include "platform/settings_store.h"
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
    App(HWND window, gp::MenuBar& menuBar, double tolerance, std::wstring settingsPath, std::string settingsOnDisk)
        : window(window), menu(menuBar), settingsPath(std::move(settingsPath)), saveTiming(std::move(settingsOnDisk)),
          feedback(presenter), fullscreen(window), feed(window, presenter, sound, tolerance) {}
    HWND window;
    gp::MenuBar& menu;
    gp::ViewerSettings settings;    // every menu override in effect now
    // What is saved: `settings` as the user chose them. Differs only for this
    // run's fallbacks (a resolution that did not start) and --rate.
    gp::ViewerSettings remembered;
    std::wstring settingsPath;      // empty: nothing is saved
    gp::SettingsSaveTiming saveTiming;
    bool saveFailureLogged = false;
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

// A key or the Audio menu: the settings change, the sound follows, the line shows.
void PressAudio(App& app, gp::AudioKey audioKey, const char* from) {
    const gp::AudioSettings now = app.settings.audio;
    const gp::AudioKeyResult pressed = gp::PressAudioKey(now, audioKey);
    if (pressed.settings.muted != now.muted) app.sound.SetMuted(pressed.settings.muted);
    if (pressed.settings.volumePercent != now.volumePercent) app.sound.SetVolume(pressed.settings.volumePercent);
    if (pressed.settings.audioDelayMs != now.audioDelayMs) app.sound.SetAudioDelayMs(pressed.settings.audioDelayMs);
    app.settings.audio = pressed.settings;
    app.remembered.audio = pressed.settings;
    gp::Log("%s: %s", from, pressed.feedback.c_str());
    app.feedback.Show(pressed.feedback);
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
    PressAudio(app, audioKey, "Key");
    return true;
}

// The F key and View > FPS counter.
void ToggleFpsCounter(App& app, const char* from) {
    app.settings.fpsCounter = app.remembered.fpsCounter = !app.settings.fpsCounter;
    gp::Log("%s: FPS counter %s", from, app.settings.fpsCounter ? "on" : "off");
    app.feed.SetFpsCounter(app.settings.fpsCounter);
}

// Every setting at once: at launch, after loading the remembered ones.
void ApplyAllSettings(App& app) {
    const gp::ViewerSettings& s = app.settings;
    if (app.sound.Volume() != s.audio.volumePercent) app.sound.SetVolume(s.audio.volumePercent);
    if (app.sound.Muted() != s.audio.muted) app.sound.SetMuted(s.audio.muted);
    if (app.sound.AudioDelayMs() != s.audio.audioDelayMs) app.sound.SetAudioDelayMs(s.audio.audioDelayMs);
    app.feed.SetGameRateOverride(s.frameRate);  // logs the setting
    app.presenter.SetVsync(s.vsync);
    if (s.fpsCounter) app.feed.SetFpsCounter(true);  // with the first frame, no present of its own
    if (s.alwaysOnTop) app.fullscreen.SetAlwaysOnTop(true);
    if (!s.captureCard.empty() || s.resolution) app.feed.SetChoice(s.captureCard, s.captureCardId, s.resolution);
    gp::Log("Menu: V-sync %s", s.vsync ? "on" : "off");
}

void ShowAbout(App& app) {
    gp::AboutInfo info;
    info.device = app.feed.CurrentMode() ? app.feed.DeviceInUse() : L"";
    info.mode = app.feed.CurrentMode();
    info.rates = app.feed.Rates();
    info.frameRate = app.settings.frameRate;
    const std::wstring text = gp::AboutText(info);
    gp::Log("Menu: About (signal rate %d, game rate %d, presented rate %d)", info.rates.signalRate,
            info.rates.gameRate, info.rates.presentedRate);
    MessageBoxW(app.window, text.c_str(), L"About GamePlayer", MB_OK | MB_ICONINFORMATION);
}

// WM_COMMAND from the menu bar. Each change applies at once.
void HandleMenuCommand(App& app, UINT id) {
    gp::ViewerSettings& s = app.settings;
    gp::ViewerSettings& kept = app.remembered;  // each choice is remembered as made
    if (const auto card = app.menu.CaptureCardFor(id)) {
        gp::Log("Menu: Capture Card \"%s\"", gp::ToUtf8(*card).c_str());
        if (*card == app.feed.DeviceInUse() && app.feed.CurrentMode()) {  // already showing it
            if (kept.captureCard != *card) {  // the automatic choice, now chosen
                s.captureCard = kept.captureCard = *card;
                s.captureCardId = kept.captureCardId = app.feed.DeviceIdInUse();
                kept.resolution = s.resolution;
            }
            return;
        }
        s.captureCard = *card;
        s.captureCardId.clear();  // learnt once it opens (LearnCaptureCardId)
        s.resolution.reset();  // the Resolution list belongs to a card
        kept.captureCard = s.captureCard;
        kept.captureCardId = s.captureCardId;
        kept.resolution = s.resolution;
        app.feed.SetChoice(s.captureCard, s.captureCardId, s.resolution);
        return;
    }
    if (const auto resolution = app.menu.ResolutionFor(id)) {
        if (*resolution) {
            gp::Log("Menu: Resolution %ux%u", (*resolution)->width, (*resolution)->height);
        } else {
            gp::Log("Menu: Resolution default (1080p)");
        }
        kept.resolution = *resolution;
        if (*resolution == s.resolution) return;
        s.resolution = *resolution;
        app.feed.SetChoice(s.captureCard, s.captureCardId, s.resolution);
        return;
    }
    switch (id) {
    case gp::kMenuRateAuto:
    case gp::kMenuRate30:
    case gp::kMenuRate60:
        s.frameRate = id == gp::kMenuRateAuto ? gp::GameRateOverride::Auto
                      : id == gp::kMenuRate30 ? gp::GameRateOverride::Fps30
                                              : gp::GameRateOverride::Fps60;
        kept.frameRate = s.frameRate;
        app.feed.SetGameRateOverride(s.frameRate);  // logs "Frames: override ..."
        break;
    case gp::kMenuVolumeUp: PressAudio(app, gp::AudioKey::VolumeUp, "Menu"); break;
    case gp::kMenuVolumeDown: PressAudio(app, gp::AudioKey::VolumeDown, "Menu"); break;
    case gp::kMenuMute: PressAudio(app, gp::AudioKey::ToggleMute, "Menu"); break;
    case gp::kMenuDelayLater: PressAudio(app, gp::AudioKey::DelayLater, "Menu"); break;
    case gp::kMenuDelayEarlier: PressAudio(app, gp::AudioKey::DelayEarlier, "Menu"); break;
    case gp::kMenuDelayReset: PressAudio(app, gp::AudioKey::DelayReset, "Menu"); break;
    case gp::kMenuFullscreen:
        gp::Log("Menu: Fullscreen");
        app.fullscreen.Toggle();
        break;
    case gp::kMenuAlwaysOnTop:
        s.alwaysOnTop = !s.alwaysOnTop;
        kept.alwaysOnTop = s.alwaysOnTop;
        app.fullscreen.SetAlwaysOnTop(s.alwaysOnTop);  // logs
        gp::Log("Menu: always on top %s, window topmost %s", s.alwaysOnTop ? "on" : "off",
                (GetWindowLongPtrW(app.window, GWL_EXSTYLE) & WS_EX_TOPMOST) ? "yes" : "no");
        break;
    case gp::kMenuVsync:
        s.vsync = !s.vsync;
        kept.vsync = s.vsync;
        app.presenter.SetVsync(s.vsync);
        gp::Log("Menu: V-sync %s (%s)", s.vsync ? "on" : "off",
                s.vsync ? "presents wait for the refresh, sync interval 1" : "present on arrival, tearing allowed");
        break;
    case gp::kMenuFpsCounter: ToggleFpsCounter(app, "Menu"); break;
    case gp::kMenuHotkeys:
        gp::Log("Menu: Hotkeys");
        MessageBoxW(app.window, gp::HotkeysText().c_str(), L"GamePlayer hotkeys", MB_OK | MB_ICONINFORMATION);
        break;
    case gp::kMenuAbout: ShowAbout(app); break;
    }
}

void MenuOpening(App& app, HMENU popup) {
    gp::MenuBar::State state;
    state.settings = app.settings;
    state.fullscreen = app.fullscreen.IsOn();
    state.deviceInUse = app.feed.CurrentMode() ? app.feed.DeviceInUse() : L"";
    state.modes = app.feed.Modes();
    app.menu.Opening(popup, state);
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

// ---- Remembered settings (ticket 09) ----

gp::WindowRect FromRect(const RECT& r) { return {r.left, r.top, r.right, r.bottom}; }

// GetWindowPlacement gives workspace coordinates: screen coordinates shifted
// by any taskbar at the top or left of the work area.
gp::WindowRect ScreenFromWorkspace(const RECT& r) {
    MONITORINFO monitor = {sizeof monitor};
    GetMonitorInfoW(MonitorFromRect(&r, MONITOR_DEFAULTTOPRIMARY), &monitor);
    const LONG dx = monitor.rcWork.left - monitor.rcMonitor.left, dy = monitor.rcWork.top - monitor.rcMonitor.top;
    return {r.left + dx, r.top + dy, r.right + dx, r.bottom + dy};
}

// The windowed position to remember: in fullscreen, the one it returns to.
std::optional<gp::SavedWindow> WindowNow(App& app) {
    if (const WINDOWPLACEMENT* saved = app.fullscreen.WindowedPlacement()) {
        return gp::SavedWindow{ScreenFromWorkspace(saved->rcNormalPosition), saved->showCmd == SW_SHOWMAXIMIZED};
    }
    WINDOWPLACEMENT placement = {sizeof placement};
    if (!GetWindowPlacement(app.window, &placement)) return std::nullopt;
    const bool minimised = IsIconic(app.window) != FALSE;
    const bool maximised = IsZoomed(app.window) || (minimised && (placement.flags & WPF_RESTORETOMAXIMIZED));
    if (!minimised && !maximised) {  // where it is now, snapped or not
        RECT now = {};
        GetWindowRect(app.window, &now);
        return gp::SavedWindow{FromRect(now), false};
    }
    return gp::SavedWindow{ScreenFromWorkspace(placement.rcNormalPosition), maximised};
}

std::string SettingsTextNow(App& app) {
    gp::RememberedSettings now;
    now.viewer = app.remembered;
    now.window = WindowNow(app);
    return gp::SettingsToText(now);
}

// A chosen Capture Card found by name gets its identifier, one found by
// identifier its current name. The automatic stand-in for an unplugged
// chosen card changes nothing.
void LearnCaptureCardId(App& app) {
    gp::ViewerSettings& s = app.settings;
    if (s.captureCard.empty() || !app.feed.CurrentMode()) return;
    const std::wstring name = app.feed.DeviceInUse(), id = app.feed.DeviceIdInUse();
    const bool isChosen = name == s.captureCard || (!s.captureCardId.empty() && id == s.captureCardId);
    if (!isChosen || (name == s.captureCard && id == s.captureCardId)) return;
    gp::Log("Settings: Capture Card \"%s\" has identifier %s", gp::ToUtf8(name).c_str(), gp::ToUtf8(id).c_str());
    s.captureCard = app.remembered.captureCard = name;
    s.captureCardId = app.remembered.captureCardId = id;
}

void WriteSettings(App& app, const std::string& text, const char* why) {
    if (gp::WriteSettingsFile(app.settingsPath, text)) {
        app.saveTiming.Written(text);
        app.saveFailureLogged = false;
        gp::Log("Settings: saved (%s)", why);
    } else if (!app.saveFailureLogged) {
        app.saveFailureLogged = true;  // retried each second, logged once
        gp::Log("Settings: could not save to %s (error %lu)", gp::ToUtf8(app.settingsPath).c_str(), GetLastError());
    }
}

// Once a second: written shortly after a change has settled.
void SettingsSecond(App& app) {
    if (app.settingsPath.empty()) return;
    LearnCaptureCardId(app);
    const std::string text = SettingsTextNow(app);
    if (app.saveTiming.Second(text)) WriteSettings(app, text, "changed");
}

void SaveSettingsNow(App& app, const char* why) {
    if (app.settingsPath.empty()) return;
    LearnCaptureCardId(app);
    const std::string text = SettingsTextNow(app);
    if (app.saveTiming.Differs(text)) WriteSettings(app, text, why);
}

void LogSettings(const char* what, const gp::RememberedSettings& r) {
    const gp::ViewerSettings& v = r.viewer;
    gp::Log("Settings: %s: volume %d, muted %s, Audio Delay %d ms, Capture Card %s%s%s, resolution %s, frame rate %s, "
            "V-sync %s, always on top %s, FPS counter %s",
            what, v.audio.volumePercent, v.audio.muted ? "yes" : "no", v.audio.audioDelayMs,
            v.captureCard.empty() ? "automatic" : ("\"" + gp::ToUtf8(v.captureCard) + "\"").c_str(),
            v.captureCardId.empty() ? "" : " id ", gp::ToUtf8(v.captureCardId).c_str(),
            v.resolution ? (std::to_string(v.resolution->width) + "x" + std::to_string(v.resolution->height)).c_str()
                         : "default",
            gp::NameOf(v.frameRate), v.vsync ? "on" : "off", v.alwaysOnTop ? "on" : "off", v.fpsCounter ? "on" : "off");
    if (r.window) {
        const gp::WindowRect& w = r.window->normal;
        gp::Log("Settings: %s: window %d,%d %dx%d%s", what, w.left, w.top, w.right - w.left, w.bottom - w.top,
                r.window->maximised ? ", maximised" : "");
    } else {
        gp::Log("Settings: %s: window default", what);
    }
}

BOOL CALLBACK AddMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM list) {
    MONITORINFO info = {sizeof info};
    if (GetMonitorInfoW(monitor, &info)) reinterpret_cast<std::vector<gp::WindowRect>*>(list)->push_back(FromRect(info.rcMonitor));
    return TRUE;
}

// Puts the window where it was, if that is still fully on a connected
// monitor, with a 16:9 Video Area. Otherwise it keeps the default size,
// centred on the primary monitor. Before the window is shown.
void RestoreWindow(HWND window, const gp::SavedWindow& saved) {
    std::vector<gp::WindowRect> monitors;
    EnumDisplayMonitors(nullptr, nullptr, AddMonitor, reinterpret_cast<LPARAM>(&monitors));
    const int slack = MulDiv(10, static_cast<int>(GetDpiForWindow(window)), 96);  // the invisible resize border
    SIZE frame = FrameSize(window);
    const std::optional<gp::WindowRect> rect =
        gp::RestoredWindowRect(saved.normal, monitors, frame.cx, frame.cy, MinVideoAreaWidth(window), slack);
    if (!rect) {
        RECT now = {};
        GetWindowRect(window, &now);
        MONITORINFO primary = {sizeof primary};
        GetMonitorInfoW(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY), &primary);
        const RECT& work = primary.rcWork;
        const LONG w = now.right - now.left, h = now.bottom - now.top;
        SetWindowPos(window, nullptr, work.left + (work.right - work.left - w) / 2,
                     work.top + (work.bottom - work.top - h) / 2, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        gp::Log("Window: remembered position %d,%d is not fully on a connected monitor (%zu); default size on the "
                "primary monitor",
                saved.normal.left, saved.normal.top, monitors.size());
        return;
    }
    SetWindowPos(window, nullptr, rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    // On a monitor with another scale the frame has changed: shape again.
    frame = FrameSize(window);
    RECT placed = {};
    GetWindowRect(window, &placed);
    const gp::WindowRect shaped =
        gp::KeepVideoArea16x9(FromRect(placed), gp::SizingEdge::Right, frame.cx, frame.cy, MinVideoAreaWidth(window));
    if (!(shaped == FromRect(placed))) {
        SetWindowPos(window, nullptr, shaped.left, shaped.top, shaped.right - shaped.left, shaped.bottom - shaped.top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }
    RECT client = {};
    GetClientRect(window, &client);
    gp::Log("Window: remembered position %d,%d, Video Area %ldx%ld%s", shaped.left, shaped.top, client.right,
            client.bottom, saved.maximised ? ", maximised" : "");
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    App* app = reinterpret_cast<App*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    LRESULT handled = 0;
    if (app && message == WM_POWERBROADCAST) app->keepAwake.PowerBroadcast(wParam, lParam);  // observes only
    if (app && app->fullscreen.HandleMessage(message, wParam, lParam, &handled)) return handled;
    if (app && app->feed.HandleMessage(message, wParam, lParam, &handled)) return handled;
    if (message == gp::AnotherLaunchMessage()) {
        gp::Log("Another launch: brought this window forward, no second copy");
        return 0;
    }
    switch (message) {
    case WM_KEYDOWN:
        if (app && HandleAudioKey(*app, wParam, lParam)) return 0;
        if (app && wParam == 'F') {
            if (!(lParam & (1 << 30))) ToggleFpsCounter(*app, "Key");  // held F toggles once
            return 0;
        }
        break;
    case WM_ENTERMENULOOP:
    case WM_EXITMENULOOP:
        // Logged so the Stats lines in between show the Feed kept playing.
        if (!wParam) gp::Log("Menu: %s", message == WM_ENTERMENULOOP ? "open" : "closed");
        break;
    case WM_INITMENUPOPUP:
        if (app && !HIWORD(lParam)) MenuOpening(*app, reinterpret_cast<HMENU>(wParam));
        return 0;
    case WM_COMMAND:
        if (app && HIWORD(wParam) == 0 && lParam == 0) {  // from a menu
            HandleMenuCommand(*app, LOWORD(wParam));
            return 0;
        }
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
            SettingsSecond(*app);
        }
        if (wParam == kRunTimer) PostMessageW(window, WM_CLOSE, 0, 0);
        return 0;
    case WM_ENDSESSION:
        // Sign-out or shutdown may end the process without WM_CLOSE.
        if (app && wParam) SaveSettingsNow(*app, "session ending");
        return 0;
    case WM_CLOSE:
        // Leave the message loop first; the window is destroyed after the
        // pipeline has stopped, because the swap chain belongs to it.
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

struct Arguments {
    int runSeconds = 0;
    double tolerance = gp::kNearDuplicateTolerance;
    std::optional<gp::GameRateOverride> rate;  // empty: the remembered one
    std::wstring settingsPath;                  // empty: the default place
};

// --run N, --tolerance X (e.g. 0.005), --rate auto|30|60, --settings PATH.
Arguments ArgumentsFromCommandLine() {
    Arguments parsed;
    int count = 0;
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!arguments) return parsed;
    for (int i = 1; i + 1 < count; i++) {
        if (wcscmp(arguments[i], L"--run") == 0) parsed.runSeconds = std::max(_wtoi(arguments[i + 1]), 0);
        if (wcscmp(arguments[i], L"--tolerance") == 0) parsed.tolerance = std::max(_wtof(arguments[i + 1]), 0.0);
        gp::GameRateOverride rate;
        if (wcscmp(arguments[i], L"--rate") == 0 && gp::ParseGameRateOverride(arguments[i + 1], rate)) parsed.rate = rate;
        if (wcscmp(arguments[i], L"--settings") == 0) parsed.settingsPath = arguments[i + 1];
    }
    LocalFree(arguments);
    return parsed;
}

std::wstring LogPathBesideExecutable() {
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring folder = path;
    folder.resize(folder.find_last_of(L'\\') + 1);
    return folder + L"GamePlayer.log";
}

HWND CreateViewerWindow(HINSTANCE instance, HMENU menu) {
    WNDCLASSEXW windowClass = {sizeof windowClass};
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));  // black until the first frame
    windowClass.lpszClassName = kWindowClass;
    RegisterClassExW(&windowClass);

    const DWORD style = WS_OVERLAPPEDWINDOW;
    RECT bounds = {0, 0, kDefaultVideoAreaWidth, kDefaultVideoAreaWidth / 16 * 9};
    AdjustWindowRectEx(&bounds, style, TRUE, 0);  // a first guess; SetDefaultSize measures the real frame
    // The menu bar is attached from the start, so the measured frame includes it.
    HWND window = CreateWindowExW(0, kWindowClass, kWindowTitle, style, CW_USEDEFAULT, CW_USEDEFAULT,
                                  bounds.right - bounds.left, bounds.bottom - bounds.top, nullptr, menu, instance,
                                  nullptr);
    if (window) SetDefaultSize(window);
    return window;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    // Before the log opens, so a second launch leaves the running copy's log alone.
    if (!gp::ClaimSingleInstance(kWindowClass)) return 0;
    gp::LogOpen(LogPathBesideExecutable());
    const Arguments arguments = ArgumentsFromCommandLine();
    const int runSeconds = arguments.runSeconds;
    gp::Log("GamePlayer starting%s", runSeconds ? " (--run: will close by itself)" : "");

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(comResult)) gp::Log("CoInitializeEx failed hr=0x%08lX", static_cast<unsigned long>(comResult));

    // Remembered settings: whatever cannot be read is simply the default.
    const std::wstring settingsPath =
        arguments.settingsPath.empty() ? gp::DefaultSettingsPath() : arguments.settingsPath;
    const std::string settingsOnDisk = gp::ReadSettingsFile(settingsPath);
    const gp::RememberedSettings loaded = gp::SettingsFromText(settingsOnDisk);
    if (settingsPath.empty()) {
        gp::Log("Settings: no app-data folder; defaults, nothing will be saved");
    } else {
        gp::Log("Settings: %s, %zu bytes read%s", gp::ToUtf8(settingsPath).c_str(), settingsOnDisk.size(),
                settingsOnDisk.empty() ? " (none: defaults)" : "");
    }
    LogSettings("loaded", loaded);

    gp::MenuBar menuBar;  // outlives the window, which owns it while attached
    HWND window = CreateViewerWindow(instance, menuBar.Handle());
    if (!window) {
        gp::Log("Could not create the window (error %lu)", GetLastError());
        if (SUCCEEDED(comResult)) CoUninitialize();
        gp::LogClose();
        return 1;
    }
    // Always windowed at launch, even if it was closed in fullscreen.
    if (loaded.window) RestoreWindow(window, *loaded.window);
    const bool openMaximised = loaded.window && loaded.window->maximised && showCommand != SW_SHOWMINIMIZED &&
                               showCommand != SW_SHOWMINNOACTIVE && showCommand != SW_MINIMIZE;

    {
        App app(window, menuBar, arguments.tolerance, settingsPath, settingsOnDisk);
        app.settings = app.remembered = loaded.viewer;
        if (arguments.rate) app.settings.frameRate = *arguments.rate;  // this run only
        app.fullscreen.SetMenuBar(menuBar.Handle());
        app.feed.OnResolutionFallback([&app](gp::Resolution failed) {
            app.settings.resolution.reset();  // this run only; the choice stays remembered
            app.feedback.Show(std::to_string(failed.width) + "x" + std::to_string(failed.height) +
                              " did not start: back to 1080p");
        });
        ApplyAllSettings(app);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&app));

        // Without a graphics device yet, the Presenter keeps trying by itself.
        if (!app.presenter.Start(window)) gp::Log("No Presenter: the window stays black");
        app.feed.Start();  // shows Loading, then the Feed or why not; recovers by itself
        app.keepAwake.Start(window);  // logs display on / off

        ShowWindow(window, openMaximised ? SW_SHOWMAXIMIZED : showCommand);
        SetTimer(window, kStatsTimer, 1000, nullptr);
        if (runSeconds) SetTimer(window, kRunTimer, static_cast<UINT>(runSeconds) * 1000, nullptr);

        MSG message;
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }

        gp::Log("Closing");
        KillTimer(window, kStatsTimer);
        SaveSettingsNow(app, "exit");  // before the window hides; in fullscreen, its windowed place
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

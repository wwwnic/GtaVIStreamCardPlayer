// GamePlayer, the Viewer: shows the Console's Feed from the Capture Card.
// This file is the window and the wiring Source -> Processor -> Presenter.
//
// Arguments: --run N   close by itself after N seconds (unattended smoke runs).
#include <windows.h>

#include <objbase.h>
#include <shellapi.h>

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "core/processor.h"
#include "core/source.h"
#include "platform/log.h"
#include "platform/mf_source.h"
#include "platform/presenter.h"

namespace {

// Lossless Scaling matches its profile on this exact title; do not change it.
const wchar_t kWindowTitle[] = L"GamePlayer";
const wchar_t kWindowClass[] = L"GamePlayerWindow";
const int kDefaultVideoAreaWidth = 1280;
const int kDefaultVideoAreaHeight = 720;
const UINT_PTR kStatsTimer = 1;
const UINT_PTR kRunTimer = 2;

struct App {
    gp::Presenter presenter;
    std::atomic<uint32_t> framesArrived{0};
    int statsLogged = 0;
    int secondsSinceStats = 0;
};

// One line per second for the first ten seconds, then one per minute.
void LogStats(App& app) {
    app.secondsSinceStats++;
    if (app.statsLogged >= 10 && app.secondsSinceStats < 60) return;
    const double seconds = app.secondsSinceStats;
    const uint32_t arrived = app.framesArrived.exchange(0);
    const gp::Presenter::Stats shown = app.presenter.TakeStats();
    gp::Log("Stats: arrived %.1f/s, presented %.1f/s, replaced before shown %u, arrival-to-present avg %.2f ms worst %.2f ms",
            arrived / seconds, shown.presented / seconds, shown.replaced, shown.averageLatencyMs, shown.worstLatencyMs);
    app.statsLogged++;
    app.secondsSinceStats = 0;
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    App* app = reinterpret_cast<App*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    switch (message) {
    case WM_SIZE:
        // Also arrives continuously while an edge is dragged; the Presenter
        // repaints on its own thread, so the picture keeps its shape during the drag.
        if (app && wParam != SIZE_MINIMIZED) app->presenter.VideoAreaResized();
        return 0;
    case WM_TIMER:
        if (wParam == kStatsTimer && app) LogStats(*app);
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
    RECT bounds = {0, 0, kDefaultVideoAreaWidth, kDefaultVideoAreaHeight};
    AdjustWindowRectEx(&bounds, style, FALSE, 0);
    return CreateWindowExW(0, kWindowClass, kWindowTitle, style, CW_USEDEFAULT, CW_USEDEFAULT,
                           bounds.right - bounds.left, bounds.bottom - bounds.top, nullptr, nullptr, instance, nullptr);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
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
        App app;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&app));

        // A missing card or graphics failure leaves a black window; messages
        // and recovery arrive with ticket 06.
        if (!app.presenter.Start(window)) gp::Log("No Presenter: the window stays black");

        std::unique_ptr<gp::Processor> processor = gp::MakePassThroughProcessor();
        std::unique_ptr<gp::Source> source = gp::MakeMediaFoundationSource();
        std::vector<gp::TimedFrame> toShow;  // used only on the Source's thread
        const bool capturing = source->Start([&](gp::FramePtr captured) {
            app.framesArrived++;
            toShow.clear();
            processor->Process(std::move(captured), toShow);
            for (gp::TimedFrame& frame : toShow) app.presenter.Submit(std::move(frame));
        });
        if (!capturing) gp::Log("No Feed: the window stays black");

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
        ShowWindow(window, SW_HIDE);  // disappear at once; teardown follows
        // Order matters: the Source stops calling into the Processor and
        // Presenter before either goes away.
        source->Stop();
        source.reset();
        app.presenter.Stop();
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
    }
    DestroyWindow(window);
    if (SUCCEEDED(comResult)) CoUninitialize();
    gp::Log("Exited cleanly");
    gp::LogClose();
    return 0;
}

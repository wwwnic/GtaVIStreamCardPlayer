// Feedback: a short confirmation line ("Volume 80%") in the top-right corner
// of the Video Area, gone about 1.5 seconds after the last one (timing in
// core/feedback_line.h). A new line replaces the old one in place.
//
// Stand-in drawing: a small click-through popup window owned by the Viewer
// window, painted with GDI, so nothing here touches the Presenter or adds
// presents to the swap chain. When the Video Area has its own text drawing
// (ticket 06), only feedback.cpp changes: Show hands the line to it.
#pragma once

#include <windows.h>

#include <string>

#include "core/feedback_line.h"

namespace gp {

class Feedback {
public:
    explicit Feedback(HWND videoWindow);
    ~Feedback();
    Feedback(const Feedback&) = delete;
    Feedback& operator=(const Feedback&) = delete;

    // Shows the line (plain ASCII/UTF-8) now. Call on the window's thread.
    void Show(const std::string& line);

    // The Viewer window moved or changed size: keep the line in its corner.
    void VideoAreaMoved();

private:
    static LRESULT CALLBACK PopupProc(HWND popup, UINT message, WPARAM wParam, LPARAM lParam);
    void Place();
    void Paint();
    void Tick();

    HWND owner_;
    HWND popup_ = nullptr;
    HFONT font_ = nullptr;
    UINT fontDpi_ = 0;
    std::wstring text_;
    FeedbackLine line_;
};

}  // namespace gp

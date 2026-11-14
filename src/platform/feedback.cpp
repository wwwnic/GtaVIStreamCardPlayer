#include "platform/feedback.h"

namespace gp {

namespace {

const wchar_t kPopupClass[] = L"GamePlayerFeedback";
const UINT_PTR kHideTimer = 1;
const BYTE kOpacity = 210;           // of 255: the picture shows faintly through the box
const int kFontPixelsAt96Dpi = 22;
const int kPaddingAt96Dpi = 10;
const int kMarginAt96Dpi = 16;       // from the Video Area's edges

int64_t NowMs() { return static_cast<int64_t>(GetTickCount64()); }

std::wstring Widen(const std::string& text) {
    std::wstring wide(text.size(), L'\0');
    const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(),
                                           static_cast<int>(wide.size()));
    wide.resize(length > 0 ? static_cast<size_t>(length) : 0);
    return wide;
}

}  // namespace

Feedback::Feedback(HWND videoWindow) : owner_(videoWindow) {
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW popupClass = {sizeof popupClass};
    if (!GetClassInfoExW(instance, kPopupClass, &popupClass)) {
        popupClass.lpfnWndProc = PopupProc;
        popupClass.hInstance = instance;
        popupClass.lpszClassName = kPopupClass;
        RegisterClassExW(&popupClass);
    }
    // Owned by the Viewer window: stays just above it, goes when it is minimised.
    // Click-through and never activated, so focus and the cursor stay with the Viewer.
    popup_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, kPopupClass, L"",
                             WS_POPUP, 0, 0, 0, 0, owner_, nullptr, instance, this);
    if (popup_) SetLayeredWindowAttributes(popup_, 0, kOpacity, LWA_ALPHA);
}

Feedback::~Feedback() {
    if (popup_ && IsWindow(popup_)) DestroyWindow(popup_);
    if (font_) DeleteObject(font_);
}

void Feedback::Show(const std::string& line) {
    if (!popup_) return;
    line_.Show(line, NowMs());
    text_ = Widen(line);
    Place();
    InvalidateRect(popup_, nullptr, TRUE);
    ShowWindow(popup_, SW_SHOWNOACTIVATE);
    SetTimer(popup_, kHideTimer, static_cast<UINT>(FeedbackLine::kShowMs), nullptr);  // restarts the time
}

void Feedback::VideoAreaMoved() {
    if (popup_ && IsWindowVisible(popup_)) Place();
}

// Sized to the text, in the top-right corner of the Video Area (the FPS
// counter uses the top-left).
void Feedback::Place() {
    const UINT dpi = GetDpiForWindow(owner_);
    if (!font_ || dpi != fontDpi_) {
        if (font_) DeleteObject(font_);
        font_ = CreateFontW(-MulDiv(kFontPixelsAt96Dpi, static_cast<int>(dpi), 96), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE,
                            FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                            DEFAULT_PITCH, L"Segoe UI");
        fontDpi_ = dpi;
    }
    RECT textSize = {};
    HDC dc = GetDC(popup_);
    HGDIOBJ old = SelectObject(dc, font_);
    DrawTextW(dc, text_.c_str(), static_cast<int>(text_.size()), &textSize, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, old);
    ReleaseDC(popup_, dc);

    const int padding = MulDiv(kPaddingAt96Dpi, static_cast<int>(dpi), 96);
    const int margin = MulDiv(kMarginAt96Dpi, static_cast<int>(dpi), 96);
    const int width = textSize.right - textSize.left + 2 * padding;
    const int height = textSize.bottom - textSize.top + padding;
    RECT client = {};
    GetClientRect(owner_, &client);
    POINT corner = {client.right, client.top};
    ClientToScreen(owner_, &corner);
    SetWindowPos(popup_, nullptr, corner.x - margin - width, corner.y + margin, width, height,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
}

void Feedback::Paint() {
    PAINTSTRUCT paint;
    HDC dc = BeginPaint(popup_, &paint);
    RECT area = {};
    GetClientRect(popup_, &area);
    HBRUSH background = CreateSolidBrush(RGB(16, 16, 16));
    FillRect(dc, &area, background);
    DeleteObject(background);
    HGDIOBJ old = SelectObject(dc, font_);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(240, 240, 240));
    DrawTextW(dc, text_.c_str(), static_cast<int>(text_.size()), &area, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, old);
    EndPaint(popup_, &paint);
}

void Feedback::Tick() {
    if (!line_.TextAt(NowMs()).empty()) return;  // a newer line restarted the time
    KillTimer(popup_, kHideTimer);
    ShowWindow(popup_, SW_HIDE);
}

LRESULT CALLBACK Feedback::PopupProc(HWND popup, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(popup, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    auto* self = reinterpret_cast<Feedback*>(GetWindowLongPtrW(popup, GWLP_USERDATA));
    switch (message) {
    case WM_PAINT:
        if (self) {
            self->Paint();
            return 0;
        }
        break;
    case WM_ERASEBKGND:
        return 1;  // Paint fills it
    case WM_TIMER:
        if (self && wParam == kHideTimer) self->Tick();
        return 0;
    case WM_NCHITTEST:
        return HTTRANSPARENT;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    }
    return DefWindowProcW(popup, message, wParam, lParam);
}

}  // namespace gp

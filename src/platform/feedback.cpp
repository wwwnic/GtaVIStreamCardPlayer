#include "platform/feedback.h"

#include <windows.h>

#include "core/feedback_line.h"

namespace gp {

namespace {

std::wstring Widen(const std::string& text) {
    std::wstring wide(text.size(), L'\0');
    const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(),
                                           static_cast<int>(wide.size()));
    wide.resize(length > 0 ? static_cast<size_t>(length) : 0);
    return wide;
}

}  // namespace

void Feedback::Show(const std::string& line) {
    presenter_.ShowText(TextPlace::Top, VideoAreaText{Widen(line), L""}, static_cast<int>(FeedbackLine::kShowMs));
}

}  // namespace gp

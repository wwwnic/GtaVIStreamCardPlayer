#include "core/feedback_line.h"

namespace gp {

void FeedbackLine::Show(std::string text, int64_t nowMs) {
    text_ = std::move(text);
    hideAtMs_ = nowMs + kShowMs;
}

std::string FeedbackLine::TextAt(int64_t nowMs) const {
    if (!hideAtMs_ || nowMs >= *hideAtMs_) return {};
    return text_;
}

std::optional<int64_t> FeedbackLine::HideAtMs() const { return hideAtMs_; }

}  // namespace gp

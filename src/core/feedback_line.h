// The feedback line: one short confirmation shown in the Video Area for a
// moment after a key press. A new one replaces the old in place and restarts
// the time. Times are in milliseconds.
#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace gp {

class FeedbackLine {
public:
    static constexpr int64_t kShowMs = 1500;

    void Show(std::string text, int64_t nowMs);

    // The line to show now; empty when there is none.
    std::string TextAt(int64_t nowMs) const;

    // When the current line disappears; empty when none was shown yet.
    std::optional<int64_t> HideAtMs() const;

private:
    std::string text_;
    std::optional<int64_t> hideAtMs_;
};

}  // namespace gp

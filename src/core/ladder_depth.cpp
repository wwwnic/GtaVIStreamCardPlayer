#include "core/ladder_depth.h"

#include <algorithm>

namespace gp {

LadderDepth::LadderDepth(int64_t ticksPerSecond) : ticksPerSecond_(ticksPerSecond) {}

int LadderDepth::Arrived(int64_t arrivalTicks, bool forgetEarlier, int64_t presentSlotTicks) {
    // The gap before a Unique Frame the arrival rules forgot the earlier for
    // (a start, a pause, a hitch) is no interval of the Game Rate: left out,
    // the rest of the last second still counts.
    if (!forgetEarlier && last_) {
        intervals_.push_back({arrivalTicks, arrivalTicks - *last_});
        measuredTicks_ += arrivalTicks - *last_;
    }
    last_ = arrivalTicks;
    while (!intervals_.empty() && intervals_.front().endTicks < arrivalTicks - ticksPerSecond_) {
        measuredTicks_ -= intervals_.front().ticks;
        intervals_.pop_front();
    }
    if (measuredTicks_ * 2 < ticksPerSecond_ || presentSlotTicks <= 0) return depth_;
    const double interval = static_cast<double>(measuredTicks_) / static_cast<double>(intervals_.size());
    const double slots = interval / static_cast<double>(presentSlotTicks);
    if (slots > kEighthsAbove) depth_ = 8;
    if (slots < kQuartersBelow) depth_ = 4;
    return depth_;
}

double LadderDepth::GameRate() const {
    if (measuredTicks_ * 2 < ticksPerSecond_) return 0;
    return static_cast<double>(intervals_.size()) * static_cast<double>(ticksPerSecond_) /
           static_cast<double>(measuredTicks_);
}

int64_t LadderStartTicks(int64_t arrivalTicks, int64_t readyTicks, int64_t intervalTicks, int generated,
                         int64_t presentSlotTicks, bool takenAfterPresent) {
    if (generated < 1 || intervalTicks <= 0) return readyTicks;  // no ladder
    const int64_t steps = takenAfterPresent ? 5 : 2;  // quarter steps: 1.25 or 0.5
    const int64_t latest = arrivalTicks + presentSlotTicks + intervalTicks * steps / (4 * (generated + 1));
    return std::min(readyTicks, latest);
}

void LadderDepth::Reset() {
    intervals_.clear();
    measuredTicks_ = 0;
    last_.reset();
    depth_ = 4;
}

}  // namespace gp

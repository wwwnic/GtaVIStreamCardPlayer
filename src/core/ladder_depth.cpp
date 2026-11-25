#include "core/ladder_depth.h"

#include <algorithm>

#include "core/frame_schedule.h"

namespace gp {

double InputFrameRate::Arrived(int64_t arrivalTicks) {
    if (last_ && arrivalTicks < *last_) Reset();
    if (last_) {
        const int64_t gap = arrivalTicks - *last_;
        if (gap <= ticksPerSecond_ * kMaxGapMs / 1000) {
            intervals_.push_back({arrivalTicks, gap});
            measuredTicks_ += gap;
        }
    }
    last_ = arrivalTicks;
    while (!intervals_.empty() && intervals_.front().endTicks < arrivalTicks - ticksPerSecond_) {
        measuredTicks_ -= intervals_.front().ticks;
        intervals_.pop_front();
    }
    if (measuredTicks_ * 2 < ticksPerSecond_) return 0;
    return static_cast<double>(intervals_.size()) * static_cast<double>(ticksPerSecond_) /
           static_cast<double>(measuredTicks_);
}

int LadderDepth::Update(double inputRate, int64_t presentSlotTicks) {
    gameRate_ = inputRate;
    if (inputRate <= 0 || presentSlotTicks <= 0) return depth_;
    const double slots = static_cast<double>(ticksPerSecond_) / inputRate / static_cast<double>(presentSlotTicks);
    if (slots > kEighthsAbove) depth_ = 8;
    if (slots < kQuartersBelow) depth_ = 4;
    return depth_;
}

int64_t LadderStartTicks(int64_t arrivalTicks, int64_t readyTicks, int64_t intervalTicks, int generated,
                         int64_t presentSlotTicks, bool takenAfterPresent) {
    if (generated < 1 || intervalTicks <= 0) return readyTicks;  // no ladder
    const int64_t steps = takenAfterPresent ? 5 : 2;  // quarter steps: 1.25 or 0.5
    const int64_t latest = arrivalTicks + presentSlotTicks + intervalTicks * steps / (4 * (generated + 1));
    return std::min(readyTicks, latest);
}

void InputFrameRate::Reset() {
    intervals_.clear();
    measuredTicks_ = 0;
    last_.reset();
}

}  // namespace gp

#include "core/ladder_depth.h"

#include <algorithm>

namespace gp {

LadderDepth::LadderDepth(int64_t ticksPerSecond) : ticksPerSecond_(ticksPerSecond) {}

int LadderDepth::Arrived(int64_t arrivalTicks, bool forgetEarlier, int64_t refreshTicks) {
    if (forgetEarlier) arrivals_.clear();
    arrivals_.push_back(arrivalTicks);
    while (arrivals_.front() < arrivalTicks - ticksPerSecond_) arrivals_.pop_front();
    const int64_t span = arrivals_.back() - arrivals_.front();
    if (span * 2 < ticksPerSecond_ || refreshTicks <= 0) return depth_;
    const double interval = static_cast<double>(span) / static_cast<double>(arrivals_.size() - 1);
    const double slots = interval / static_cast<double>(refreshTicks);
    if (slots > kEighthsAbove) depth_ = 8;
    if (slots < kQuartersBelow) depth_ = 4;
    return depth_;
}

double LadderDepth::GameRate() const {
    const int64_t span = arrivals_.empty() ? 0 : arrivals_.back() - arrivals_.front();
    if (span * 2 < ticksPerSecond_) return 0;
    return static_cast<double>(arrivals_.size() - 1) * static_cast<double>(ticksPerSecond_) / static_cast<double>(span);
}

int64_t LadderStartTicks(int64_t arrivalTicks, int64_t readyTicks, int64_t intervalTicks, int generated,
                         int64_t refreshTicks) {
    if (generated != 7 || intervalTicks <= 0) return readyTicks;  // x4, 2x: as ticket 01
    const int64_t latest = arrivalTicks + refreshTicks + intervalTicks * 5 / (4 * (generated + 1));
    return std::min(readyTicks, latest);
}

void LadderDepth::Reset() {
    arrivals_.clear();
    depth_ = 4;
}

}  // namespace gp

#include "core/ladder_depth.h"

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

void LadderDepth::Reset() {
    arrivals_.clear();
    depth_ = 4;
}

}  // namespace gp

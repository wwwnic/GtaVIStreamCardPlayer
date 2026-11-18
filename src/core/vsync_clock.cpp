#include "core/vsync_clock.h"

namespace gp {

uint32_t VsyncClock::Sample(const VsyncSample& sample) {
    if (latest_ && (sample.presentCount < latest_->presentCount || sample.refreshCount < latest_->refreshCount ||
                    sample.syncRefresh < latest_->syncRefresh)) {
        // Statistics restarted (a new swap chain, a mode change): start over.
        reference_.reset();
        measuredPeriod_ = 0;
        latest_ = sample;
        reference_ = sample;
        return 0;
    }
    uint32_t missed = 0;
    if (latest_ && sample.presentCount > latest_->presentCount) {
        const uint64_t presents = sample.presentCount - latest_->presentCount;
        const uint64_t refreshes = sample.refreshCount - latest_->refreshCount;
        if (refreshes > presents) missed = static_cast<uint32_t>(refreshes - presents);
    }
    if (!reference_) reference_ = sample;
    const uint64_t span = sample.syncRefresh - reference_->syncRefresh;
    if (span >= kMeasureOverRefreshes) {
        measuredPeriod_ = (sample.vblankTicks - reference_->vblankTicks) / static_cast<int64_t>(span);
    }
    latest_ = sample;
    return missed;
}

void VsyncClock::Reset() {
    latest_.reset();
    reference_.reset();
    measuredPeriod_ = 0;
    lastGiven_.reset();
}

int64_t VsyncClock::Period(int64_t nominalPeriodTicks) const {
    if (measuredPeriod_ <= 0) return nominalPeriodTicks;
    const int64_t slack = nominalPeriodTicks / 4;
    if (measuredPeriod_ < nominalPeriodTicks - slack || measuredPeriod_ > nominalPeriodTicks + slack)
        return nominalPeriodTicks;
    return measuredPeriod_;
}

int64_t VsyncClock::NextVsync(int64_t nowTicks, int64_t nominalPeriodTicks) {
    const int64_t period = Period(nominalPeriodTicks);
    int64_t next = nowTicks;
    if (latest_ && period > 0) {
        const int64_t base = latest_->vblankTicks;
        next = nowTicks >= base ? base + ((nowTicks - base) / period + 1) * period
                                : base - ((base - nowTicks) / period) * period;
    }
    if (lastGiven_ && next < *lastGiven_ + period / 2) next = *lastGiven_ + period;
    lastGiven_ = next;
    return next;
}

}  // namespace gp

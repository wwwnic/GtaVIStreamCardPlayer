#include "core/vsync_clock.h"

namespace gp {

uint32_t VsyncClock::Sample(const VsyncSample& sample) {
    if (sample.refreshCount == 0 || sample.syncRefresh == 0) return 0;  // nothing shown yet
    if (latest_ && (sample.presentCount < latest_->presentCount || sample.refreshCount < latest_->refreshCount ||
                    sample.syncRefresh < latest_->syncRefresh ||
                    sample.refreshCount - latest_->refreshCount > kMostVblanksBetween)) {
        // Statistics restarted (a new swap chain, a mode change) or not
        // believable: start over from this one.
        reference_.reset();
        measuredPeriod_ = 0;
        validSamples_ = 1;
        latest_ = sample;
        return 0;
    }
    uint32_t missed = 0;
    if (latest_ && sample.presentCount > latest_->presentCount) {
        const uint64_t presents = sample.presentCount - latest_->presentCount;
        const uint64_t refreshes = sample.refreshCount - latest_->refreshCount;
        if (refreshes > presents) missed = static_cast<uint32_t>(refreshes - presents);
    }
    // The first sample may be stale (a time from another vblank than its
    // count): the period is measured from the second on.
    if (++validSamples_ == 2) reference_ = sample;
    if (reference_) {
        const uint64_t span = sample.syncRefresh - reference_->syncRefresh;
        if (span >= kMeasureOverRefreshes) {
            measuredPeriod_ = (sample.vblankTicks - reference_->vblankTicks) / static_cast<int64_t>(span);
        }
    }
    latest_ = sample;
    return missed;
}

void VsyncClock::Reset() {
    latest_.reset();
    reference_.reset();
    measuredPeriod_ = 0;
    validSamples_ = 0;
    lastGiven_.reset();
}

int64_t VsyncClock::Period(int64_t nominalPeriodTicks) const {
    return measuredPeriod_ > 0 ? measuredPeriod_ : nominalPeriodTicks;
}

int64_t VsyncClock::NextVsync(int64_t nowTicks, int64_t nominalPeriodTicks) {
    const int64_t period = Period(nominalPeriodTicks);
    int64_t next = nowTicks;
    if (latest_ && period > 0) {
        const int64_t base = latest_->vblankTicks;
        next = nowTicks >= base ? base + ((nowTicks - base) / period + 1) * period
                                : base - ((base - nowTicks) / period) * period;
    }
    // Woken faster than the period (it is wrong, or the swap chain paces
    // otherwise): the clock wins, so the time never runs ahead of it.
    if (lastGiven_ && next < *lastGiven_ + period / 2 && *lastGiven_ + period <= nowTicks + period + period / 2)
        next = *lastGiven_ + period;
    lastGiven_ = next;
    return next;
}

}  // namespace gp

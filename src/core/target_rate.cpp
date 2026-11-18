#include "core/target_rate.h"

#include <algorithm>

namespace gp {

int RateInUse(int targetRate, int refresh) {
    if (targetRate <= kTargetRateMonitor) return refresh;
    return std::min(targetRate, refresh);
}

int CustomBoxStart(int targetRate, int customRate, int refresh) {
    return customRate != kNoCustomTargetRate ? customRate : RateInUse(targetRate, refresh);
}

std::optional<int> TargetRateTyped(std::wstring_view text) {
    while (!text.empty() && text.front() == L' ') text.remove_prefix(1);
    while (!text.empty() && text.back() == L' ') text.remove_suffix(1);
    if (text.empty()) return std::nullopt;
    int value = 0;
    for (const wchar_t c : text) {
        if (c < L'0' || c > L'9') return std::nullopt;
        value = value * 10 + (c - L'0');
        if (value > kTargetRateHighest) return std::nullopt;  // also no overflow
    }
    if (value < kTargetRateLowest) return std::nullopt;
    return value;
}

bool RealFramesOnly(int64_t intervalTicks, int rateInUse, int64_t ticksPerSecond) {
    if (intervalTicks <= 0 || rateInUse < 1) return true;
    return static_cast<int64_t>(rateInUse) * intervalTicks * 100 <= ticksPerSecond * (100 + kRealOnlyMarginPercent);
}

std::optional<std::string> UnevenNotice::RateApplies(int rateInUse, int refresh) {
    const bool same = rateInUse == rate_ && refresh == refresh_;
    rate_ = rateInUse;
    refresh_ = refresh;
    if (same || rateInUse < 1 || refresh < 1 || refresh % rateInUse == 0) return std::nullopt;
    return std::to_string(rateInUse) + " on a " + std::to_string(refresh) +
           " Hz monitor: motion will not be perfectly even";
}

void UnevenNotice::Reset() { rate_ = refresh_ = 0; }

PresentPacer::PresentPacer(int rate, int refresh)
    : rate_(refresh < 1 ? 1 : rate < 1 || rate > refresh ? refresh : rate), refresh_(refresh < 1 ? 1 : refresh) {}

PacedPresent PresentPacer::Next() {
    PacedPresent present;
    present.fresh = pending_ == 0;
    if (present.fresh) {
        carry_ += refresh_;
        pending_ = carry_ / rate_;
        carry_ -= pending_ * rate_;
    }
    present.syncInterval = std::min(pending_, kMostSyncInterval);
    pending_ -= present.syncInterval;
    return present;
}

}  // namespace gp

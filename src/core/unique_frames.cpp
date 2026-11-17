#include "core/unique_frames.h"

#include <cmath>
#include <cstring>

namespace gp {

FrameMatch CompareFrames(const Frame& a, const Frame& b, double tolerance, double* differingShare,
                         double closeShare) {
    if (a.width != b.width || a.height != b.height || a.format != b.format || a.pixels.size() != b.pixels.size()) {
        return FrameMatch::Different;
    }
    const size_t size = a.pixels.size();
    const uint8_t* pa = a.pixels.data();
    const uint8_t* pb = b.pixels.data();
    // Fast path: identical bytes. On a moving picture memcmp stops at the first difference.
    if (std::memcmp(pa, pb, size) == 0) {
        if (differingShare) *differingShare = 0;
        return FrameMatch::Exact;
    }
    if (differingShare) *differingShare = 1.0;

    // Count differing 8-byte words (a short tail counts as one), giving up as
    // soon as there are too many to be near or close.
    const size_t whole = size / 8;
    const size_t words = whole + (size % 8 ? 1 : 0);
    const double nearLimit = static_cast<double>(words) * (tolerance > 0 ? tolerance : 0.0);
    const double limit = static_cast<double>(words) * (tolerance > closeShare ? tolerance : closeShare);
    if (limit <= 0) return FrameMatch::Different;  // exact only, closeness not wanted
    size_t differing = 0;
    for (size_t i = 0; i < whole; i++) {
        uint64_t wa, wb;
        std::memcpy(&wa, pa + i * 8, 8);
        std::memcpy(&wb, pb + i * 8, 8);
        if (wa != wb && static_cast<double>(++differing) >= limit) return FrameMatch::Different;
    }
    if (whole * 8 < size && std::memcmp(pa + whole * 8, pb + whole * 8, size - whole * 8) != 0 &&
        static_cast<double>(++differing) >= limit) {
        return FrameMatch::Different;
    }
    if (differingShare) *differingShare = static_cast<double>(differing) / static_cast<double>(words);
    return static_cast<double>(differing) < nearLimit ? FrameMatch::Near : FrameMatch::Different;
}

const char* NameOf(GameRateOverride setting) {
    switch (setting) {
    case GameRateOverride::Auto: return "Auto";
    case GameRateOverride::Fps30: return "30";
    case GameRateOverride::Fps60: return "60";
    }
    return "?";
}

bool ParseGameRateOverride(const std::wstring& word, GameRateOverride& setting) {
    if (word == L"auto" || word == L"Auto") setting = GameRateOverride::Auto;
    else if (word == L"30") setting = GameRateOverride::Fps30;
    else if (word == L"60") setting = GameRateOverride::Fps60;
    else return false;
    return true;
}

UniqueFrameProcessor::UniqueFrameProcessor(int64_t ticksPerSecond, double tolerance)
    : ticksPerSecond_(ticksPerSecond > 0 ? ticksPerSecond : 1),
      tolerance_(tolerance > kMaxTolerance ? kMaxTolerance : tolerance > 0 ? tolerance : 0) {}

void UniqueFrameProcessor::Process(FramePtr captured, std::vector<TimedFrame>& out) {
    double share = 1.0;  // only a Near repeat's share is counted here
    // Only what decides Unique or Repeated is done before the frame is handed
    // on: the compare stops at the first differing byte (exact only) or once
    // the tolerance is passed. How close a Unique Frame was is measured later.
    const FrameMatch match =
        reference_ ? CompareFrames(*captured, *reference_, tolerance_, &share, 0.0) : FrameMatch::Different;
    const bool unique = match == FrameMatch::Different;
    // A Unique Frame differs by the tolerance or more: with a tolerance of
    // kCloseShare or above it cannot be close, so there is nothing to measure.
    if (unique && reference_ && tolerance_ < kCloseShare) {
        measureNew_ = captured;
        measureOld_ = reference_;
    }
    if (unique) {
        reference_ = captured;
        referenceShown_ = false;
        recent_.insert(recent_.begin(), captured);
        if (recent_.size() > kRecentUnique) recent_.pop_back();
    }
    if (sinceEmitted_ < 2) sinceEmitted_++;  // only "two or more" matters: no wrap on a long still picture

    bool emit = false;
    switch (override_.load()) {
    // A Unique Frame, or the next copy of one that override 30 held back just
    // before the setting changed (it would otherwise never be shown on a
    // picture that then stays still).
    case GameRateOverride::Auto: emit = !referenceShown_; break;
    case GameRateOverride::Fps60: emit = true; break;
    case GameRateOverride::Fps30:
        // A Unique Frame that came too soon is shown by its next copy.
        emit = !referenceShown_ && sinceEmitted_ >= 2;
        break;
    }
    if (emit) {
        referenceShown_ = true;
        sinceEmitted_ = 0;
    }
    const int64_t arrival = captured->arrivalQpc;
    {
        std::lock_guard lock(mutex_);
        if (warmPending_) {
            warmPending_ = false;
            warmFrom_ = arrival;
        }
        history_[historyNext_] = {arrival, unique, emit};
        historyNext_ = (historyNext_ + 1) % kHistory;
        if (historyCount_ < kHistory) historyCount_++;
        totals_.arrived++;
        if (unique) totals_.unique++;
        if (match == FrameMatch::Exact) totals_.repeatedExact++;
        if (match == FrameMatch::Near) {
            totals_.repeatedNear++;
            if (share > closeness_.nearShareMax) closeness_.nearShareMax = share;
        }
        if (emit) totals_.presented++; else totals_.dropped++;
    }
    if (emit) out.push_back({std::move(captured), arrival});
}

void UniqueFrameProcessor::AfterSubmit() {
    if (!measureNew_) return;
    double share = 1.0;
    CompareFrames(*measureNew_, *measureOld_, 0.0, &share, kCloseShare);
    measureNew_.reset();
    measureOld_.reset();
    if (share >= kCloseShare) return;
    std::lock_guard lock(mutex_);
    closeness_.closeUnique++;
    if (share < closeness_.closeShareMin) closeness_.closeShareMin = share;
}

void UniqueFrameProcessor::Reset() {
    measureNew_.reset();
    measureOld_.reset();
    reference_.reset();
    referenceShown_ = false;
    sinceEmitted_ = 2;
    recent_.clear();
    std::lock_guard lock(mutex_);
    warmPending_ = true;
    // The stopped Source's arrivals say nothing about the new one's rates.
    historyNext_ = 0;
    historyCount_ = 0;
    shownSignal_ = shownGame_ = shownPresented_ = 0;
}

FrameRates UniqueFrameProcessor::RatesAt(int64_t nowTicks) {
    std::lock_guard lock(mutex_);
    const int64_t from = nowTicks - kRateWindowSeconds * ticksPerSecond_;
    size_t n = 0, uniques = 0, presented = 0;
    int64_t first = 0, last = 0;
    for (size_t i = 0; i < historyCount_; i++) {
        const Arrival& a = history_[(historyNext_ + kHistory - 1 - i) % kHistory];  // newest first
        // Recorded after the caller read its clock (the Source's thread got in
        // first): not part of this reading, but the older ones are.
        if (a.ticks > nowTicks) continue;
        if (a.ticks <= from) break;
        if (n == 0) last = a.ticks;
        first = a.ticks;
        n++;
        if (a.unique) uniques++;
        if (a.presented) presented++;
    }
    double signal = 0;
    if (n >= 2 && last > first) {
        signal = static_cast<double>(n - 1) * static_cast<double>(ticksPerSecond_) / static_cast<double>(last - first);
    } else {
        signal = static_cast<double>(n) / kRateWindowSeconds;
    }
    const double game = n ? signal * static_cast<double>(uniques) / static_cast<double>(n) : 0.0;
    const double shown = n ? signal * static_cast<double>(presented) / static_cast<double>(n) : 0.0;
    // A small change is jitter of the window, not of the game: the figure
    // moves only once the rate is kRateStep away from it. Under 1, so a rate
    // just below a whole number (59.94) returns to it after a dip instead of
    // staying one short.
    auto follow = [](int& shown, double raw) {
        if (std::abs(raw - shown) >= kRateStep || (raw < 0.5 && shown != 0)) shown = static_cast<int>(std::lround(raw));
    };
    follow(shownSignal_, signal);
    follow(shownGame_, game);
    follow(shownPresented_, shown);
    const bool warm = !warmPending_ && nowTicks - warmFrom_ >= kRateWindowSeconds * ticksPerSecond_;
    return {shownSignal_, shownGame_, shownPresented_, warm};
}

UniqueFrameTotals UniqueFrameProcessor::Totals() {
    std::lock_guard lock(mutex_);
    return totals_;
}

UniqueCloseness UniqueFrameProcessor::TakeCloseness() {
    std::lock_guard lock(mutex_);
    const UniqueCloseness taken = closeness_;
    closeness_ = {};
    return taken;
}

std::wstring FpsCounterText(FrameRates rates, int shownRate, int runningFactor, int realPresentedRate) {
    if (!rates.warm) return L"";
    if (runningFactor > 1) return L"Game " + std::to_wstring(realPresentedRate) + L"\nShown " + std::to_wstring(shownRate);
    return std::to_wstring(rates.presentedRate) + L" FPS";
}

}  // namespace gp

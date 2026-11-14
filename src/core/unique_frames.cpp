#include "core/unique_frames.h"

#include <cstring>

namespace gp {

FrameMatch CompareFrames(const Frame& a, const Frame& b, double tolerance, double* differingShare) {
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
    if (tolerance <= 0) return FrameMatch::Different;

    // Near-duplicate: count differing 8-byte words (a short tail counts as one),
    // giving up as soon as there are too many to be "fewer than" the tolerance.
    const size_t whole = size / 8;
    const size_t words = whole + (size % 8 ? 1 : 0);
    const double limit = static_cast<double>(words) * tolerance;
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
    return FrameMatch::Near;
}

const char* NameOf(GameRateOverride setting) {
    switch (setting) {
    case GameRateOverride::Auto: return "Auto";
    case GameRateOverride::Fps30: return "30";
    case GameRateOverride::Fps60: return "60";
    }
    return "?";
}

UniqueFrameProcessor::UniqueFrameProcessor(int64_t ticksPerSecond, double tolerance)
    : ticksPerSecond_(ticksPerSecond > 0 ? ticksPerSecond : 1), tolerance_(tolerance > 0 ? tolerance : 0) {}

void UniqueFrameProcessor::Process(FramePtr captured, std::vector<TimedFrame>& out) {
    const bool unique = !reference_ || CompareFrames(*captured, *reference_, tolerance_) == FrameMatch::Different;
    if (unique) {
        reference_ = captured;
        const int64_t arrival = captured->arrivalQpc;
        out.push_back({std::move(captured), arrival});
    }
}

void UniqueFrameProcessor::Reset() {
    reference_.reset();
    sinceEmitted_ = 0;
    recent_.clear();
}

FrameRates UniqueFrameProcessor::RatesAt(int64_t) { return {}; }

UniqueFrameTotals UniqueFrameProcessor::Totals() {
    std::lock_guard lock(mutex_);
    return totals_;
}

std::wstring FpsCounterText(FrameRates) { return {}; }

}  // namespace gp

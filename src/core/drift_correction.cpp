#include "core/drift_correction.h"

#include <algorithm>
#include <cmath>

namespace gp {

namespace {

// The fill jumps by a whole capture packet each time one arrives, so the
// readings are averaged over about this long before they steer anything.
const double kSmoothingSeconds = 2.0;

// A fill error is worked off over about this long. Slow enough that the rate
// barely moves; fast enough that a clock difference of 300 parts per million
// leaves the fill only 3 ms from its target.
const double kCorrectionSeconds = 10.0;

}  // namespace

void DriftCorrection::Reset(double fillSeconds) {
    smoothedFill_ = fillSeconds;
    rate_ = 1.0;
}

double DriftCorrection::Update(double fillSeconds, double targetSeconds, double elapsedSeconds) {
    const double weight = 1.0 - std::exp(-std::max(elapsedSeconds, 0.0) / kSmoothingSeconds);
    smoothedFill_ += (fillSeconds - smoothedFill_) * weight;
    const double correction = (smoothedFill_ - targetSeconds) / kCorrectionSeconds;
    rate_ = 1.0 + std::clamp(correction, -kMaxCorrection, kMaxCorrection);
    return rate_;
}

}  // namespace gp

#include "core/drift_correction.h"

#include <algorithm>
#include <cmath>

namespace gp {

namespace {

// The fill jumps by a whole capture packet each time one arrives, so the
// readings are averaged over about this long before they steer anything.
const double kSmoothingSeconds = 2.0;

// A fill error is worked off over about this long. Slow enough that the rate
// barely moves. By itself this would leave the fill resting off its target
// under a steady clock difference (3 ms at 300 parts per million), which is
// most of the margin the target fill is chosen for; the trim below removes
// that.
const double kCorrectionSeconds = 10.0;

// The trim: the clock difference learnt so far. It takes over whatever part
// of the correction stays, over about this long, so the fill comes back to
// the target itself. Much slower than the correction above, so the two do
// not swing (with the smoothing, all three settle without overshoot of the
// rate).
const double kTrimSeconds = 60.0;

}  // namespace

void DriftCorrection::Reset(double fillSeconds) {
    smoothedFill_ = fillSeconds;
    trim_ = 0.0;
    rate_ = 1.0;
}

void DriftCorrection::Resume(double fillSeconds) {
    smoothedFill_ = fillSeconds;
    rate_ = 1.0;
}

double DriftCorrection::Update(double fillSeconds, double targetSeconds, double elapsedSeconds) {
    const double elapsed = std::max(elapsedSeconds, 0.0);
    const double weight = 1.0 - std::exp(-elapsed / kSmoothingSeconds);
    smoothedFill_ += (fillSeconds - smoothedFill_) * weight;
    const double correction = (smoothedFill_ - targetSeconds) / kCorrectionSeconds;
    // The trim learns only while the rate is inside its bound, or when that
    // brings it back inside: a long spell far off the target teaches nothing.
    const double wanted = correction + trim_;
    if (std::fabs(wanted) < kMaxCorrection || wanted * correction < 0.0) {
        trim_ = std::clamp(trim_ + correction * elapsed / kTrimSeconds, -kMaxCorrection, kMaxCorrection);
    }
    rate_ = 1.0 + std::clamp(correction + trim_, -kMaxCorrection, kMaxCorrection);
    return rate_;
}

}  // namespace gp

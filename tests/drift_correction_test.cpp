#include "doctest.h"

#include <cmath>

#include "core/drift_correction.h"

using gp::DriftCorrection;

namespace {
const double kTarget = 0.015;  // seconds of sound waiting in the buffer
const double kPeriod = 0.010;  // one reading per 10 ms playback period

// Feeds the same fill reading for `seconds`; returns the last playback rate.
double Hold(DriftCorrection& correction, double fill, double seconds) {
    double rate = 1.0;
    for (double t = 0; t < seconds; t += kPeriod) rate = correction.Update(fill, kTarget, kPeriod);
    return rate;
}
}  // namespace

TEST_CASE("a buffer held at its target fill plays at exactly the normal rate") {
    DriftCorrection correction;
    correction.Reset(kTarget);
    CHECK(Hold(correction, kTarget, 5.0) == doctest::Approx(1.0));
}

TEST_CASE("a buffer fuller than its target plays slightly faster, within inaudible bounds") {
    DriftCorrection correction;
    correction.Reset(kTarget);
    const double rate = Hold(correction, kTarget + 0.005, 5.0);
    CHECK(rate > 1.0);
    CHECK(rate <= 1.0 + DriftCorrection::kMaxCorrection);
}

TEST_CASE("a buffer emptier than its target plays slightly slower, within inaudible bounds") {
    DriftCorrection correction;
    correction.Reset(kTarget);
    const double rate = Hold(correction, kTarget - 0.005, 5.0);
    CHECK(rate < 1.0);
    CHECK(rate >= 1.0 - DriftCorrection::kMaxCorrection);
}

TEST_CASE("however far the fill is off, the correction never exceeds its bound") {
    DriftCorrection correction;
    correction.Reset(kTarget);
    CHECK(Hold(correction, 1.0, 30.0) == doctest::Approx(1.0 + DriftCorrection::kMaxCorrection));
    CHECK(Hold(correction, 0.0, 60.0) == doctest::Approx(1.0 - DriftCorrection::kMaxCorrection));
    CHECK(DriftCorrection::kMaxCorrection <= 0.002);  // 0.2% is about 3.5 cents: not heard
}

TEST_CASE("the playback rate changes smoothly, never in a jump") {
    DriftCorrection correction;
    correction.Reset(kTarget);
    double previous = 1.0;
    double largestStep = 0.0;
    for (int i = 0; i < 1000; i++) {
        const double fill = (i < 500) ? kTarget + 0.010 : kTarget - 0.010;  // sudden 20 ms swing
        const double rate = correction.Update(fill, kTarget, kPeriod);
        largestStep = std::fmax(largestStep, std::fabs(rate - previous));
        previous = rate;
    }
    CHECK(largestStep < 0.00002);  // 20 parts per million per 10 ms
}

// Closed loop: the Capture Card's clock runs 300 parts per million fast, sound
// arrives in 10 ms packets and leaves in 10 ms periods, for an hour.
TEST_CASE("with the card's clock running fast, the fill settles near the target and does not grow") {
    DriftCorrection correction;
    double fill = kTarget;
    correction.Reset(fill);
    const double cardRate = 1.0003;
    double rate = 1.0;
    double worstAfterSettling = 0.0;
    const int periods = 3600 * 100;
    for (int i = 0; i < periods; i++) {
        fill += kPeriod * cardRate;     // one capture packet
        fill -= kPeriod * rate;         // one playback period
        const double reading = fill - ((i % 2) ? 0.002 : 0.0);  // packet jitter
        rate = correction.Update(reading, kTarget, kPeriod);
        if (i > 60 * 100) worstAfterSettling = std::fmax(worstAfterSettling, std::fabs(fill - kTarget));
    }
    CHECK(worstAfterSettling < 0.005);
    CHECK(rate == doctest::Approx(cardRate).epsilon(0.0001));
}

TEST_CASE("with the card's clock running slow, the fill settles near the target too") {
    DriftCorrection correction;
    double fill = kTarget;
    correction.Reset(fill);
    const double cardRate = 0.9997;
    double rate = 1.0;
    for (int i = 0; i < 600 * 100; i++) {
        fill += kPeriod * cardRate;
        fill -= kPeriod * rate;
        rate = correction.Update(fill, kTarget, kPeriod);
    }
    CHECK(std::fabs(fill - kTarget) < 0.005);
}

TEST_CASE("a raised target (later: Audio Delay) is reached by the same slow correction") {
    DriftCorrection correction;
    double fill = kTarget;
    correction.Reset(fill);
    const double raised = kTarget + 0.004;
    double rate = 1.0;
    for (int i = 0; i < 300 * 100; i++) {
        fill += kPeriod;
        fill -= kPeriod * rate;
        rate = correction.Update(fill, raised, kPeriod);
    }
    CHECK(fill == doctest::Approx(raised).epsilon(0.05));
}

#include "doctest.h"

#include "core/vsync_clock.h"

using gp::VsyncClock;
using gp::VsyncSample;

namespace {

const int64_t kPeriod = 6944;  // µs, 144 Hz

VsyncSample At(uint64_t present, uint64_t refresh) {
    return {present, refresh, static_cast<int64_t>(refresh) * kPeriod, refresh};
}

}  // namespace

TEST_CASE("vsync clock: without a sample the display time is now") {
    VsyncClock clock;
    CHECK_FALSE(clock.HaveSample());
    CHECK(clock.NextVsync(1000, kPeriod) == 1000);
    CHECK(clock.Period(kPeriod) == kPeriod);
}

TEST_CASE("vsync clock: the next vblank after now, from the latest sample") {
    VsyncClock clock;
    clock.Sample(At(10, 100));  // vblank 100 at 694400
    CHECK(clock.NextVsync(100 * kPeriod + 10, kPeriod) == 101 * kPeriod);
    CHECK(clock.NextVsync(103 * kPeriod + 6000, kPeriod) == 104 * kPeriod);
}

TEST_CASE("vsync clock: two presents never claim the same vblank") {
    VsyncClock clock;
    clock.Sample(At(10, 100));
    CHECK(clock.NextVsync(100 * kPeriod + 6900, kPeriod) == 101 * kPeriod);
    // Woken again just after: the same vblank is gone, the next one is its.
    CHECK(clock.NextVsync(100 * kPeriod + 6940, kPeriod) == 102 * kPeriod);
    VsyncClock fallback;  // without samples too
    CHECK(fallback.NextVsync(5000, kPeriod) == 5000);
    CHECK(fallback.NextVsync(5100, kPeriod) == 5000 + kPeriod);
    CHECK(fallback.NextVsync(5000 + 2 * kPeriod, kPeriod) == 5000 + 2 * kPeriod);
}

TEST_CASE("vsync clock: missed vsyncs are vblanks that showed no new present") {
    VsyncClock clock;
    CHECK(clock.Sample(At(10, 100)) == 0u);
    CHECK(clock.Sample(At(11, 101)) == 0u);  // one present per vblank
    CHECK(clock.Sample(At(12, 103)) == 1u);  // vblank 102 kept the previous frame
    CHECK(clock.Sample(At(12, 103)) == 0u);  // nothing new
    CHECK(clock.Sample(At(15, 106)) == 0u);  // three at once, three vblanks
    CHECK(clock.Sample(At(3, 2)) == 0u);     // statistics restarted
    CHECK(clock.Sample(At(4, 4)) == 1u);
}

TEST_CASE("vsync clock: the period is measured over enough vblanks, else nominal") {
    VsyncClock clock;
    const int64_t real = 6950;  // the monitor is a little slower than its nominal 144
    clock.Sample({1, 1000, 1000 * real, 1000});
    clock.Sample({2, 1010, 1010 * real, 1010});
    CHECK(clock.Period(kPeriod) == kPeriod);  // 10 vblanks: not yet
    clock.Sample({3, 1100, 1100 * real, 1100});
    CHECK(clock.Period(kPeriod) == real);
    CHECK(clock.Period(16667) == 16667);  // far from the nominal (moved to a 60 Hz screen): not trusted
    clock.Reset();
    CHECK_FALSE(clock.HaveSample());
    CHECK(clock.Period(kPeriod) == kPeriod);
}

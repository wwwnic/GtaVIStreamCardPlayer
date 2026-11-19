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

TEST_CASE("vsync clock: woken more often than the period, the display time never runs ahead of the clock") {
    VsyncClock clock;
    const int64_t nominal = 16949;  // told 59 Hz, woken at 144 Hz
    for (int64_t now = 0; now < 200000; now += kPeriod) {
        const int64_t next = clock.NextVsync(now, nominal);
        CHECK(next >= now);
        CHECK(next <= now + 2 * nominal);
    }
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

TEST_CASE("vsync clock: a late present is one first shown after the vblank it was made for") {
    VsyncClock clock;
    clock.Presented(10, 100 * kPeriod);
    CHECK(clock.Late(At(10, 100), kPeriod) == 0u);  // on time
    clock.Presented(11, 101 * kPeriod);
    CHECK(clock.Late(At(11, 103), kPeriod) == 2u);  // two vblanks late: the frame before stayed two more
    CHECK(clock.Late(At(11, 103), kPeriod) == 0u);  // judged once
    CHECK(clock.Late(At(12, 104), kPeriod) == 0u);  // a present it was not told of
}

TEST_CASE("vsync clock: real frames only, vblanks with no present are not late ones") {
    VsyncClock clock;
    // A present for each new frame, about 2.4 vblanks apart (60 fps on 144 Hz).
    uint64_t refresh = 100;
    for (uint64_t present = 1; present <= 20; present++) {
        refresh += present % 5 == 0 || present % 5 == 2 ? 3 : 2;
        clock.Presented(present, static_cast<int64_t>(refresh) * kPeriod);
        clock.Sample(At(present, refresh));
        CHECK(clock.Late(At(present, refresh), kPeriod) == 0u);
    }
}

TEST_CASE("vsync clock: refresh counts that lag or jump (a second monitor) are judged by time, never late early") {
    VsyncClock clock;
    clock.Presented(14, 112 * kPeriod);
    // The latest vblank is 115, but the present's own count lags at 105: shown
    // "before" it was asked for, so not late.
    CHECK(clock.Late({14, 105, 115 * kPeriod, 115}, kPeriod) == 0u);
    clock.Presented(15, 120 * kPeriod);
    CHECK(clock.Late({15, 120, 400 * kPeriod, 400}, kPeriod) == 0u);  // the counts caught up in a jump
    clock.Presented(16, 401 * kPeriod);
    CHECK(clock.Late({16, 402, 402 * kPeriod + 100, 402}, kPeriod) == 1u);
}

TEST_CASE("vsync clock: a sample from before anything was shown, or an impossible jump, counts nothing") {
    VsyncClock clock;
    CHECK(clock.Sample({1, 0, 0, 0}) == 0u);  // nothing shown yet: no refresh count
    CHECK_FALSE(clock.HaveSample());
    CHECK(clock.Sample(At(2, 5000000)) == 0u);
    CHECK(clock.Sample(At(3, 5000001)) == 0u);
    CHECK(clock.Sample(At(4, 5000002 + VsyncClock::kMostVblanksBetween)) == 0u);  // not believable: start over
    CHECK(clock.Sample(At(5, 5000004 + VsyncClock::kMostVblanksBetween)) == 1u);
}

TEST_CASE("vsync clock: the period is measured over enough vblanks, else nominal") {
    VsyncClock clock;
    const int64_t real = 6950;  // the monitor is a little slower than its nominal 144
    clock.Sample({1, 1000, 1000 * real, 1000});
    clock.Sample({2, 1010, 1010 * real, 1010});
    CHECK(clock.Period(kPeriod) == kPeriod);  // 10 vblanks: not yet
    clock.Sample({3, 1100, 1100 * real, 1100});
    CHECK(clock.Period(kPeriod) == real);
    VsyncClock stale;  // a first sample whose time does not belong to its count is not the reference
    stale.Sample({1, 1000, 1000 * real + 900, 1000});
    stale.Sample({2, 1001, 1001 * real, 1001});
    stale.Sample({3, 1101, 1101 * real, 1101});
    CHECK(stale.Period(kPeriod) == real);
    // Far from the nominal (a 59 Hz screen, but the swap chain still paced at
    // 144 Hz): the measured one is what presents actually follow.
    CHECK(clock.Period(16949) == real);
    clock.Reset();
    CHECK_FALSE(clock.HaveSample());
    CHECK(clock.Period(kPeriod) == kPeriod);
}

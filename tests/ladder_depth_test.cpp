#include "doctest.h"

#include "core/ladder_depth.h"

using gp::LadderDepth;

namespace {

const int64_t kSecond = 1000000;  // the tests count in microseconds
const int64_t k144 = kSecond / 144;

// Unique Frames at `fps` for `seconds`, starting at `from`; the depth after the last.
int Feed(LadderDepth& depth, double fps, double seconds, int64_t& at, int64_t refresh = k144) {
    const int64_t interval = static_cast<int64_t>(kSecond / fps);
    int result = 0;
    for (int64_t end = at + static_cast<int64_t>(seconds * kSecond); at < end; at += interval)
        result = depth.Arrived(at, false, refresh);
    return result;
}

}  // namespace

TEST_CASE("Ladder depth: x4 until a Game Rate is known") {
    LadderDepth depth(kSecond);
    CHECK(depth.Arrived(0, true, k144) == 4);
    CHECK(depth.Arrived(kSecond / 30, false, k144) == 4);  // one interval is not yet a rate
}

TEST_CASE("Ladder depth at 144 Hz: 30 fps uses x8, 60 fps uses x4") {
    LadderDepth thirty(kSecond);
    int64_t at = 0;
    CHECK(Feed(thirty, 30, 1.0, at) == 8);
    LadderDepth sixty(kSecond);
    at = 0;
    CHECK(Feed(sixty, 60, 1.0, at) == 4);
}

TEST_CASE("Ladder depth thresholds at 144 Hz: about 51 fps and below x8, about 58 and above x4") {
    LadderDepth low(kSecond);
    int64_t at = 0;
    CHECK(Feed(low, 50, 1.0, at) == 8);  // 2.88 refreshes per Unique Frame
    LadderDepth high(kSecond);
    at = 0;
    CHECK(Feed(high, 59, 1.0, at) == 4);  // 2.44
    LadderDepth between(kSecond);
    at = 0;
    CHECK(Feed(between, 54, 1.0, at) == 4);  // 2.67: from x4 it stays x4
}

TEST_CASE("Ladder depth has hysteresis: between the thresholds it keeps what it had") {
    LadderDepth depth(kSecond);
    int64_t at = 0;
    CHECK(Feed(depth, 30, 1.0, at) == 8);
    CHECK(Feed(depth, 54, 2.0, at) == 8);  // 2.67: still x8
    CHECK(Feed(depth, 60, 2.0, at) == 4);  // 2.4: now x4
    CHECK(Feed(depth, 54, 2.0, at) == 4);  // and stays x4
    CHECK(Feed(depth, 45, 2.0, at) == 8);
}

TEST_CASE("Ladder depth does not flip on a steady but uneven Game Rate (33 / 50 ms gaps)") {
    LadderDepth depth(kSecond);
    int64_t at = 0;
    int changes = 0, last = 4;
    for (int i = 0; i < 200; i++) {
        at += (i % 2 ? 50000 : 33333);
        const int now = depth.Arrived(at, false, k144);
        if (now != last) changes++;
        last = now;
    }
    CHECK(last == 8);
    CHECK(changes == 1);
}

TEST_CASE("Ladder depth follows the recent second, not the whole run") {
    LadderDepth depth(kSecond);
    int64_t at = 0;
    Feed(depth, 60, 10.0, at);
    CHECK(Feed(depth, 30, 1.2, at) == 8);  // ten seconds of 60 do not hold it back
}

TEST_CASE("Ladder depth follows the rate in use: 30 fps on a 60 Hz monitor is x4") {
    LadderDepth depth(kSecond);
    int64_t at = 0;
    CHECK(Feed(depth, 30, 1.0, at, kSecond / 60) == 4);  // 2 refreshes per Unique Frame
    CHECK(Feed(depth, 30, 1.0, at, kSecond / 144) == 8);
}

TEST_CASE("Ladder depth after a pause keeps its depth and measures again") {
    LadderDepth depth(kSecond);
    int64_t at = 0;
    CHECK(Feed(depth, 30, 1.0, at) == 8);
    at += 2 * kSecond;  // a still picture
    CHECK(depth.Arrived(at, true, k144) == 8);  // x8 resumes at once
    at += kSecond / 30;
    CHECK(depth.Arrived(at, false, k144) == 8);
    CHECK(Feed(depth, 60, 1.0, at) == 4);  // the gap itself is not counted as a slow rate
}

TEST_CASE("x8 all or fall back: a missing eighth gives the x4 ladder, never a partial x8") {
    CHECK(gp::ReadyLadderFrames(3, 4) == 7);
    CHECK(gp::ReadyLadderFrames(3, 3) == 3);
    CHECK(gp::ReadyLadderFrames(3, 0) == 3);
    CHECK(gp::ReadyLadderFrames(1, 4) == 1);  // quarters missing: the middle only
    CHECK(gp::ReadyLadderFrames(0, 0) == 0);
}

TEST_CASE("The third stage restarts its history from the current real frame after a gap in its stream") {
    using gp::EighthStep;
    CHECK(gp::ThirdStageStep(true, true, 3, false) == EighthStep::Generate);
    CHECK(gp::ThirdStageStep(true, false, 3, false) == EighthStep::Restart);  // entering x8, or it was idle
    CHECK(gp::ThirdStageStep(true, true, 1, false) == EighthStep::Restart);   // quarters missing for N
    CHECK(gp::ThirdStageStep(true, true, 0, false) == EighthStep::Restart);
    CHECK(gp::ThirdStageStep(true, true, 3, true) == EighthStep::Restart);    // a pause, a Feed restart, a size change
    CHECK(gp::ThirdStageStep(false, true, 3, false) == EighthStep::Idle);     // x4 wanted: not fed
}

TEST_CASE("Ladder depth: Reset starts over at x4") {
    LadderDepth depth(kSecond);
    int64_t at = 0;
    CHECK(Feed(depth, 30, 1.0, at) == 8);
    depth.Reset();
    CHECK(depth.Depth() == 4);
    CHECK(depth.Arrived(at, true, k144) == 4);
}

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

TEST_CASE("Ladder start: no later than a refresh and 1.25 steps after the Unique Frame arrived") {
    const int64_t interval = kSecond / 30;  // an eighth is 4167, a quarter 8333
    // x8 made in 5.4 ms after a take up to a refresh after arrival: ready
    // (made + a refresh) at 12.3 to 19.2 ms. The latest start: 6.944 + 5.208.
    CHECK(gp::LadderStartTicks(0, 19200, interval, 7, k144) == k144 + 5208);
    CHECK(gp::LadderStartTicks(1000, 20000, interval, 7, k144) == 1000 + k144 + 5208);
    CHECK(gp::LadderStartTicks(0, 12000, interval, 7, k144) == 12000);  // ready earlier: unchanged
    // x4 and 2x keep their ready time (ticket 01), even when it is later.
    CHECK(gp::LadderStartTicks(0, 9400, interval, 3, k144) == 9400);
    CHECK(gp::LadderStartTicks(0, 12500, kSecond / 60, 3, k144) == 12500);
    CHECK(gp::LadderStartTicks(0, 30000, interval, 1, k144) == 30000);
    CHECK(gp::LadderStartTicks(1000, 30000, interval, 0, k144) == 30000);  // real frames only
    CHECK(gp::LadderStartTicks(1000, 30000, 0, 7, k144) == 30000);
}

#include <algorithm>
#include <cstdint>
#include <vector>
#include "core/target_schedule.h"
// The Presenter on the x8 ladder: one pass per vblank; a Unique Frame that
// arrived waits for the next pass, which presents first, then takes it and
// generates for `generation` (the next pass waits for that). Every pass's
// present is displayed a refresh later.
TEST_CASE("Target x8 at 30 fps, generating 5.4 ms after the present: every Unique Frame shown, pacing steady") {
    const int64_t interval = kSecond / 30, eighth = interval / 8, generation = 5400;
    for (const int64_t phase : {0, 1000, 2500, 4000, 6000, 6900}) {  // where the arrivals fall between vblanks
        gp::TargetSchedule schedule(kSecond);
        std::vector<int64_t> arrivals;
        for (int i = 0; i < 60; i++) arrivals.push_back(10000 + phase + i * interval);
        size_t next = 0;
        int current = -1;
        int64_t busyUntil = 0, lead = 0, lowest = INT64_MAX, highest = INT64_MIN;
        for (int64_t vblank = 0; vblank < arrivals.back() + interval; vblank += k144) {
            if (busyUntil >= vblank + k144) continue;  // still generating at the next vblank: missed
            const int64_t display = vblank + k144;
            if (const std::optional<int> frame = schedule.FrameFor(display); frame && current > 0) {
                // Content time of the shown frame: eighths before its Unique Frame.
                const int64_t content = arrivals[current] - (schedule.Generated() - *frame) * eighth;
                lead = display - content;
                lowest = std::min(lowest, lead);
                highest = std::max(highest, lead);
            }
            const int64_t pass = std::max(vblank, busyUntil);
            if (next < arrivals.size() && arrivals[next] <= pass) {
                schedule.UniqueFrameArrived(arrivals[next], next + 1, 1920, 1080);
                busyUntil = pass + generation;
                schedule.Begin(7, k144, gp::LadderStartTicks(arrivals[next], busyUntil + k144, interval, 7, k144));
                current = static_cast<int>(next++);
            }
        }
        const gp::TargetStats stats = schedule.TakeStats();
        CHECK(stats.notShown == 0u);
        CHECK(stats.uniqueShown >= arrivals.size() - 1);  // the last one may be in progress
        CHECK(stats.ladder == 8);
        // 4.8 vsyncs per Unique Frame, 8 frames to pick from: most vsyncs
        // show a new frame; fewer repeats than x4 at 30 fps (24 of 144). Due
        // a vsync too early, the Unique Frame would be shown again at the
        // vsync that takes the next, every time (a quarter of all vsyncs).
        CHECK(stats.repeats * 8 <= stats.vsyncs);
        // Shown frames follow the content's clock: within 1.75 eighths
        // (nearest picking, a ladder's first frame early). Anchored to the
        // take instead, it would wander by a refresh more.
        CHECK(highest - lowest <= eighth * 7 / 4);
    }
}

TEST_CASE("Ladder depth: Reset starts over at x4") {
    LadderDepth depth(kSecond);
    int64_t at = 0;
    CHECK(Feed(depth, 30, 1.0, at) == 8);
    depth.Reset();
    CHECK(depth.Depth() == 4);
    CHECK(depth.Arrived(at, true, k144) == 4);
}

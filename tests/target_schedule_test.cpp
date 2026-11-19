#include "doctest.h"

#include <algorithm>
#include <vector>

#include "core/ladder_depth.h"
#include "core/target_rate.h"
#include "core/target_schedule.h"

using gp::TargetSchedule;

namespace {

const int64_t kSecond = 1000000;  // the tests count in microseconds
const int64_t kMs = 1000;
const int64_t k144 = kSecond / 144;  // 6944 µs between vsyncs
const int64_t k60 = kSecond / 60;

// Unique Frame 1 at `first`, Unique Frame 2 `interval` later and ready at
// once, its ladder of `generated` begun: the schedule is ready for FrameFor.
void TwoFrames(TargetSchedule& schedule, int64_t first, int64_t interval, int generated = 3, int rate = 144) {
    schedule.UniqueFrameArrived(first, 1, 1920, 1080);
    schedule.Begin(generated, rate, first);
    schedule.FrameFor(first + 1);
    schedule.UniqueFrameArrived(first + interval, 2, 1920, 1080);
    schedule.Begin(generated, rate, first + interval);
}

// Arrivals at the given times (each ready `readyAfter` later, begun with a
// full ladder), vsyncs every `refresh` from 0 to `until`. For every vsync:
// which Unique Frame was current (its index in `arrivals`) and what FrameFor
// answered (-1: none).
struct Shown {
    int unique;
    int frame;
};
std::vector<Shown> Play(TargetSchedule& schedule, const std::vector<int64_t>& arrivals, int64_t refresh,
                        int64_t until, int64_t readyAfter = 3 * kMs) {
    std::vector<Shown> shown;
    size_t next = 0;
    int current = -1;
    for (int64_t vsync = 0; vsync <= until; vsync += refresh) {
        while (next < arrivals.size() && arrivals[next] + readyAfter <= vsync) {
            schedule.UniqueFrameArrived(arrivals[next], next + 1, 1920, 1080);
            schedule.Begin(3, static_cast<int>(kSecond / refresh), arrivals[next] + readyAfter);
            current = static_cast<int>(next++);
        }
        const std::optional<int> frame = schedule.FrameFor(vsync);
        shown.push_back({current, frame ? *frame : -1});
    }
    return shown;
}

// The Presenter on the x4 ladder (ticket 01): a Unique Frame is taken the
// moment it arrives and generated for `generation` (a pass due meanwhile
// waits for it); its first ladder frame is due a refresh after that. One
// pass per present (PresentPacer at `rate` on 144 Hz), displayed at the
// vblank its sync interval ends on; the next pass comes then. `jitter`: each
// arrival up to that much early or late.
struct Run {
    gp::TargetStats stats;
    double uniqueLatencyMs = 0;  // a Unique Frame's arrival to its own picture on screen, on average
    uint32_t arrivals = 0;
};
Run PlayX4(int64_t interval, int rate, int64_t phase, int64_t jitter = 0, int64_t generation = 3 * kMs) {
    TargetSchedule schedule(kSecond);
    gp::PresentPacer pacer(rate, 144);
    std::vector<int64_t> arrivals;
    for (int i = 0; i < 600; i++) {
        const int64_t wobble = jitter ? (static_cast<int64_t>(i) * 7919 % 2001 - 1000) * jitter / 1000 : 0;
        arrivals.push_back(10 * kMs + phase + i * interval + wobble);
    }
    size_t next = 0;
    int current = -1;
    int64_t busyUntil = 0, latencySum = 0, latencyCount = 0;
    for (int64_t vblank = 0; vblank < arrivals.back() + interval;) {
        while (next < arrivals.size() && arrivals[next] <= std::max(vblank, busyUntil)) {
            schedule.UniqueFrameArrived(arrivals[next], next + 1, 1920, 1080);
            busyUntil = std::max(arrivals[next], busyUntil) + generation;
            schedule.Begin(3, rate, gp::LadderStartTicks(arrivals[next], busyUntil + k144, interval, 3, kSecond / rate, false));
            current = static_cast<int>(next++);
        }
        const gp::PacedPresent paced = pacer.Next();
        const int64_t display = vblank + paced.syncInterval * k144;
        if (paced.fresh) {
            const std::optional<int> frame = schedule.FrameFor(display);
            if (frame && *frame == schedule.Generated() && current >= 0) {
                latencySum += display - arrivals[current];
                latencyCount++;
            }
        }
        vblank = display;
    }
    Run run;
    run.stats = schedule.TakeStats();
    run.uniqueLatencyMs = latencyCount ? static_cast<double>(latencySum) / latencyCount / kMs : 0;
    run.arrivals = static_cast<uint32_t>(arrivals.size());
    return run;
}

}  // namespace

TEST_CASE("Target x4 at 60 fps on 144 Hz: every Unique Frame's own picture is shown") {
    for (const int64_t phase : {0, 1000, 2500, 4000, 5500, 6900}) {
        for (const int64_t jitter : {0, 300}) {
            const Run run = PlayX4(k60, 144, phase, jitter);
            MESSAGE("60 fps, phase ", phase, " jitter ", jitter, ": not shown ", run.stats.notShown, ", repeats ",
                    run.stats.repeats, ", latency ", run.uniqueLatencyMs, " ms");
            CHECK(run.stats.notShown == 0u);
            CHECK(run.stats.uniqueShown >= run.arrivals - 1);  // the last one may be in progress
            // Held back less than before (21.9 ms: a refresh after generating, as 4x).
            CHECK(run.uniqueLatencyMs < 21.0);
            // A vsync just before the next Unique Frame is due shows this one
            // (arrival jitter) and, when the next is on time, again: at most
            // one Unique Frame in five, at the worst phases.
            CHECK(run.stats.repeats <= run.arrivals / 5 + 5);  // and a few after the last
        }
    }
}

TEST_CASE("Target x4 at 30 fps (no third FRC) on 144 Hz: every Unique Frame shown, timing as 4x") {
    for (const int64_t phase : {0, 2500, 5500}) {
        const Run run = PlayX4(kSecond / 30, 144, phase, 300);
        CHECK(run.stats.notShown == 0u);
        CHECK(run.uniqueLatencyMs < 37.0);
    }
}

TEST_CASE("Target x4 at 60 fps with the rate in use at 120 and 72: every Unique Frame's own picture is shown") {
    for (const int rate : {120, 72}) {
        for (const int64_t phase : {0, 1000, 2500, 4000, 5500, 6900}) {
            const Run run = PlayX4(k60, rate, phase, 300);
            MESSAGE("60 fps at ", rate, ", phase ", phase, ": not shown ", run.stats.notShown, ", repeats ",
                    run.stats.repeats, ", latency ", run.uniqueLatencyMs, " ms");
            CHECK(run.stats.notShown == 0u);
        }
    }
}

TEST_CASE("Target: nothing to show before the first Unique Frame") {
    TargetSchedule schedule(kSecond);
    CHECK_FALSE(schedule.FrameFor(0).has_value());
    CHECK_FALSE(schedule.FrameFor(k144).has_value());
    CHECK(schedule.Shown() == -1);
    const gp::TargetStats stats = schedule.TakeStats();
    CHECK(stats.vsyncs == 2u);
    CHECK(stats.repeats == 2u);
}

TEST_CASE("Target: the first frame after a start is shown alone, then repeated") {
    TargetSchedule schedule(kSecond);
    const auto arrival = schedule.UniqueFrameArrived(100 * kMs, 1, 1920, 1080);
    CHECK(arrival.forgetEarlier);
    schedule.Begin(3, 144, 100 * kMs);
    CHECK(schedule.Generated() == 0);
    CHECK(schedule.FrameFor(101 * kMs) == 0);  // the Unique Frame itself, at the first vsync
    CHECK_FALSE(schedule.FrameFor(101 * kMs + k144).has_value());
    CHECK_FALSE(schedule.FrameFor(101 * kMs + 2 * k144).has_value());
    const gp::TargetStats stats = schedule.TakeStats();
    CHECK(stats.uniqueShown == 1u);
    CHECK(stats.repeats == 2u);
    CHECK(stats.ladder == 1);
}

TEST_CASE("Target at 30 fps on 144 Hz: first quarter, middle, last quarter, the Unique Frame, then repeats") {
    TargetSchedule schedule(kSecond);
    const int64_t interval = kSecond / 30;  // 33333: a quarter is 8333
    TwoFrames(schedule, 0, interval);
    CHECK(schedule.Generated() == 3);
    const int64_t ready = interval;
    const int64_t v0 = ready + 500;  // the first vsync after it is ready
    CHECK(schedule.FrameFor(v0) == 0);              // 1/4, as 4x shows it at once
    CHECK(schedule.FrameFor(v0 + k144) == 1);       // 7.4 ms after ready: 0.89 quarters -> 2/4
    CHECK(schedule.FrameFor(v0 + 2 * k144) == 2);   // 14.4 ms: 1.73 -> 3/4
    CHECK(schedule.FrameFor(v0 + 3 * k144) == 3);   // 21.3 ms: 2.56 -> N
    CHECK_FALSE(schedule.FrameFor(v0 + 4 * k144).has_value());  // nothing newer: repeat N
    CHECK_FALSE(schedule.FrameFor(v0 + 5 * k144).has_value());
    const gp::TargetStats stats = schedule.TakeStats();
    CHECK(stats.generatedShown == 3u);
    CHECK(stats.uniqueShown == 2u);  // the first Unique Frame and N
    CHECK(stats.repeats == 2u);
    CHECK(stats.ladder == 4);
}

TEST_CASE("Target picks the nearest ladder frame, counted from when the ladder was ready") {
    TargetSchedule schedule(kSecond);
    const int64_t interval = 40 * kMs;  // 25 fps: a quarter is 10 ms
    TwoFrames(schedule, 0, interval);
    const int64_t ready = interval;
    CHECK(schedule.FrameFor(ready + 1000) == 0);
    CHECK_FALSE(schedule.FrameFor(ready + 4999).has_value());  // still nearest the first quarter
    CHECK(schedule.FrameFor(ready + 5000) == 1);              // exactly half way: the later
    CHECK_FALSE(schedule.FrameFor(ready + 14999).has_value());
    CHECK(schedule.FrameFor(ready + 15000) == 2);
    CHECK(schedule.FrameFor(ready + 25000) == 3);
    CHECK_FALSE(schedule.FrameFor(ready + 90000).has_value());  // never past the Unique Frame
}

TEST_CASE("Target: the first vsync of a ladder shows its nearest frame, at least the first one") {
    TargetSchedule late(kSecond);
    TwoFrames(late, 0, 40 * kMs);
    CHECK(late.FrameFor(40 * kMs + 12 * kMs) == 1);  // a first vsync 12 ms after ready: the middle
    TargetSchedule early(kSecond);
    TwoFrames(early, 0, 40 * kMs);
    CHECK(early.FrameFor(40 * kMs - 3 * kMs) == 0);  // before it was ready: still its own first frame
}

TEST_CASE("Target never goes backwards: an earlier vsync time or a missed vsync") {
    TargetSchedule schedule(kSecond);
    TwoFrames(schedule, 0, 40 * kMs);
    const int64_t ready = 40 * kMs;
    CHECK(schedule.FrameFor(ready + 1000) == 0);
    CHECK(schedule.FrameFor(ready + 22 * kMs) == 2);  // a missed vsync: straight to 3/4
    CHECK_FALSE(schedule.FrameFor(ready + 6 * kMs).has_value());  // an earlier time: no going back
    CHECK(schedule.Shown() == 2);
    CHECK(schedule.FrameFor(ready + 31 * kMs) == 3);
}

TEST_CASE("Target: a new Unique Frame mid-sequence starts its own ladder; the old one is not shown again") {
    TargetSchedule schedule(kSecond);
    TwoFrames(schedule, 0, 40 * kMs);
    CHECK(schedule.FrameFor(41 * kMs) == 0);
    CHECK(schedule.FrameFor(51 * kMs) == 1);
    schedule.UniqueFrameArrived(60 * kMs, 3, 1920, 1080);  // early: 20 ms after the last
    schedule.Begin(3, 144, 60 * kMs);
    CHECK(schedule.Shown() == -1);
    CHECK(schedule.FrameFor(62 * kMs) == 0);  // the new first quarter, not the old 3/4
    CHECK(schedule.FrameFor(67 * kMs) == 1);  // its quarters are 5 ms now
    const gp::TargetStats stats = schedule.TakeStats();
    CHECK(stats.notShown == 1u);  // Unique Frame 2 itself never reached the screen
}

TEST_CASE("Target at an even 30 fps on 144 Hz shows every Unique Frame itself and never skips a step") {
    TargetSchedule schedule(kSecond);
    std::vector<int64_t> arrivals;
    for (int i = 0; i < 60; i++) arrivals.push_back(10 * kMs + i * kSecond / 30);
    const std::vector<Shown> shown = Play(schedule, arrivals, k144, 2 * kSecond + 100 * kMs);
    const gp::TargetStats stats = schedule.TakeStats();
    CHECK(stats.notShown == 0u);
    CHECK(stats.uniqueShown == arrivals.size());
    // The middle and the last quarter are on screen longer than a refresh:
    // each is always shown. The first quarter is shown when a vsync comes near it.
    CHECK(stats.generatedShown >= 2 * (arrivals.size() - 1));
    int unique = -2, last = -1;
    for (const Shown& s : shown) {
        if (s.unique != unique) {
            unique = s.unique;
            last = -1;
        }
        if (s.frame < 0) continue;
        if (last >= 0) CHECK(s.frame == last + 1);  // after the first pick, one step at a time
        last = s.frame;
    }
}

TEST_CASE("Target with uneven 33 / 50 ms gaps (a 26 fps game on a 60 Hz signal): steady, never backwards") {
    TargetSchedule schedule(kSecond);
    std::vector<int64_t> arrivals;
    int64_t at = 10 * kMs;
    for (int i = 0; i < 40; i++) {
        arrivals.push_back(at);
        at += (i % 2 ? 50 : 33) * kMs + (i % 2 ? 0 : 333);
    }
    const std::vector<Shown> shown = Play(schedule, arrivals, k144, at);
    int unique = -2, last = -1, newFrames = 0;
    for (const Shown& s : shown) {
        if (s.unique != unique) {
            unique = s.unique;
            last = -1;
            if (unique >= 0) CHECK(s.frame >= 0);  // every Unique Frame shows a frame of its own at once
        }
        if (s.frame >= 0) {
            CHECK(s.frame > last);  // never backwards, never twice
            CHECK(s.frame <= 3);
            last = s.frame;
            newFrames++;
        }
    }
    // About 144 vsyncs a second; most of them show a new frame (4 per Unique
    // Frame at most), the rest repeat.
    const gp::TargetStats stats = schedule.TakeStats();
    CHECK(stats.vsyncs == shown.size());
    CHECK(stats.generatedShown + stats.uniqueShown + stats.repeats == stats.vsyncs);
    CHECK(newFrames > static_cast<int>(arrivals.size()) * 3);
    CHECK(stats.ladder == 4);
}

TEST_CASE("Target after a pause: the next Unique Frame is shown alone at once, then held") {
    TargetSchedule schedule(kSecond);
    TwoFrames(schedule, 0, 33 * kMs);
    for (int64_t v = 34 * kMs; v < 100 * kMs; v += k144) schedule.FrameFor(v);
    // Still picture: nothing new for a long time, every vsync repeats.
    for (int64_t v = 100 * kMs; v < 400 * kMs; v += k144) CHECK_FALSE(schedule.FrameFor(v).has_value());
    const auto arrival = schedule.UniqueFrameArrived(400 * kMs, 3, 1920, 1080);
    CHECK(arrival.forgetEarlier);
    schedule.Begin(3, 144, 400 * kMs);
    CHECK(schedule.Generated() == 0);
    CHECK(schedule.FrameFor(401 * kMs) == 0);
    CHECK_FALSE(schedule.FrameFor(401 * kMs + k144).has_value());
}

TEST_CASE("Target on the x8 ladder at 30 fps: eighths a refresh or so apart, then the Unique Frame") {
    TargetSchedule schedule(kSecond);
    const int64_t interval = 40 * kMs;  // 25 fps: an eighth is 5 ms
    TwoFrames(schedule, 0, interval, 7);
    CHECK(schedule.Generated() == 7);
    const int64_t ready = interval;
    CHECK(schedule.FrameFor(ready + 1000) == 0);                // 1/8
    CHECK(schedule.FrameFor(ready + 7 * kMs) == 1);             // 1.4 eighths -> 2/8
    CHECK(schedule.FrameFor(ready + 14 * kMs) == 3);            // 2.8 -> 4/8 (3/8 passed between vsyncs)
    CHECK(schedule.FrameFor(ready + 21 * kMs) == 4);            // 4.2 -> 5/8
    CHECK(schedule.FrameFor(ready + 28 * kMs) == 6);            // 5.6 -> 7/8
    CHECK(schedule.FrameFor(ready + 35 * kMs) == 7);            // 7 -> N
    CHECK_FALSE(schedule.FrameFor(ready + 42 * kMs).has_value());
    CHECK(schedule.TakeStats().ladder == 8);
}

TEST_CASE("Target: more Generated Frames than the x8 ladder count as x8; 4 to 6 as x4") {
    TargetSchedule eight(kSecond);
    TwoFrames(eight, 0, 40 * kMs, 9);
    CHECK(eight.Generated() == 7);
    TargetSchedule four(kSecond);
    TwoFrames(four, 0, 40 * kMs, 5);
    CHECK(four.Generated() == 3);
}

TEST_CASE("Target at 2x frames only: the middle, then the Unique Frame") {
    TargetSchedule schedule(kSecond);
    TwoFrames(schedule, 0, 40 * kMs, 1);
    CHECK(schedule.Generated() == 1);
    CHECK(schedule.FrameFor(41 * kMs) == 0);
    CHECK_FALSE(schedule.FrameFor(40 * kMs + 9999).has_value());  // under half a step (10 ms)
    CHECK(schedule.FrameFor(40 * kMs + 10 * kMs) == 1);
    CHECK(schedule.TakeStats().ladder == 2);
}

TEST_CASE("Target with the rate in use not above the Game Rate: real frames only") {
    TargetSchedule schedule(kSecond);
    TwoFrames(schedule, 0, k60, 3, 60);  // a 60 fps game on a 60 Hz monitor
    CHECK(schedule.Generated() == 0);
    CHECK(schedule.FrameFor(k60 + 100) == 0);
    CHECK(schedule.RealOnly());
    CHECK(schedule.TakeStats().realOnly);
    TargetSchedule faster(kSecond);
    TwoFrames(faster, 0, k60, 3, 144);  // the same game at 144 Hz: the ladder is used
    CHECK(faster.Generated() == 3);
    CHECK_FALSE(faster.RealOnly());
}

TEST_CASE("Target real frames only is RealFramesOnly's rule: a tenth of margin over the Game Rate") {
    TargetSchedule sixtyFive(kSecond);
    TwoFrames(sixtyFive, 0, k60, 3, 65);  // 65 at 60 fps: within the margin
    CHECK(sixtyFive.RealOnly());
    CHECK(sixtyFive.Generated() == 0);
    TargetSchedule seventyTwo(kSecond);
    TwoFrames(seventyTwo, 0, k60, 3, 72);
    CHECK_FALSE(seventyTwo.RealOnly());
    CHECK(seventyTwo.Generated() == 3);
}

TEST_CASE("Target after a short hitch: no ladder for the Unique Frame after it, but not real frames only") {
    TargetSchedule schedule(kSecond);
    TwoFrames(schedule, 0, k60);
    schedule.TakeStats();
    const auto arrival = schedule.UniqueFrameArrived(k60 + 250 * kMs, 3, 1920, 1080);  // over kMaxGapMs
    CHECK(arrival.forgetEarlier);
    schedule.Begin(3, 144, k60 + 250 * kMs);
    CHECK(schedule.Generated() == 0);  // nothing to make it from
    CHECK_FALSE(schedule.RealOnly());  // the Presenter keeps presenting every vsync
    const gp::TargetStats stats = schedule.TakeStats();
    CHECK(stats.ladder == 4);  // the ladder in use is still x4
    CHECK_FALSE(stats.realOnly);
    // In real frames only it stays so.
    TargetSchedule real(kSecond);
    TwoFrames(real, 0, k60, 3, 60);
    real.UniqueFrameArrived(k60 + 250 * kMs, 3, 1920, 1080);
    real.Begin(3, 60, k60 + 250 * kMs);
    CHECK(real.RealOnly());
}

TEST_CASE("Target: an interval the arrival rules refuse gives real frames only, and Forget starts over") {
    TargetSchedule schedule(kSecond);
    TwoFrames(schedule, 0, 120 * kMs);  // over kMaxIntervalMs, under kMaxGapMs
    CHECK(schedule.Generated() == 0);
    schedule.Forget();
    CHECK_FALSE(schedule.FrameFor(200 * kMs).has_value());
    CHECK(schedule.UniqueFrameArrived(210 * kMs, 9, 1920, 1080).forgetEarlier);
}

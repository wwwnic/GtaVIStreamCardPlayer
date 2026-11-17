#include "doctest.h"

#include "core/frame_schedule.h"

using gp::FrameSchedule;
using gp::TimedFrameStats;

namespace {

const int64_t kSecond = 1000000;  // the tests count in microseconds
const int64_t kMs = 1000;

// Two Unique Frames `intervalMs` apart, the second at `secondArrival`: the
// schedule is then ready for Begin.
FrameSchedule::Arrival TwoFrames(FrameSchedule& schedule, int64_t secondArrival, int64_t interval) {
    schedule.UniqueFrameArrived(secondArrival - interval, 1, 1920, 1080);
    schedule.Begin(0);
    schedule.Presented(secondArrival - interval);
    return schedule.UniqueFrameArrived(secondArrival, 2, 1920, 1080);
}

}  // namespace

TEST_CASE("the Generated Frames for a Generation Factor: 1 at 2x, 3 at 4x, none otherwise") {
    CHECK(gp::GeneratedFramesFor(2) == 1);
    CHECK(gp::GeneratedFramesFor(4) == 3);
    CHECK(gp::GeneratedFramesFor(1) == 0);
    CHECK(gp::GeneratedFramesFor(0) == 0);
    CHECK(gp::GeneratedFramesFor(3) == 0);
    CHECK(gp::GeneratedFramesFor(8) == 0);
    CHECK(gp::GeneratedFramesFor(-2) == 0);
}

TEST_CASE("nothing is due and nothing is held before the first Unique Frame") {
    FrameSchedule schedule(kSecond);
    CHECK_FALSE(schedule.DueFrame(0).has_value());
    CHECK_FALSE(schedule.InProgress());
    CHECK_FALSE(schedule.HoldsRedraw(0));
    CHECK_FALSE(schedule.TicksUntilWake(0, true).has_value());
    CHECK(schedule.Presented(5) == 0);  // nothing to present: no effect
    CHECK(schedule.TakeStats().presented == 0u);
}

TEST_CASE("the interval is the time between the arrivals of the last two Unique Frames") {
    FrameSchedule schedule(kSecond);
    CHECK(schedule.UniqueFrameArrived(100 * kMs, 1, 1920, 1080).intervalTicks == 0);  // no earlier one
    schedule.Begin(0);
    FrameSchedule::Arrival arrival = schedule.UniqueFrameArrived(133 * kMs, 3, 1920, 1080);  // a 30 fps Game Rate
    CHECK(arrival.mayGenerate);
    CHECK_FALSE(arrival.forgetEarlier);
    CHECK(arrival.intervalTicks == 33 * kMs);
    schedule.Begin(0);
    arrival = schedule.UniqueFrameArrived(150 * kMs, 4, 1920, 1080);  // then 60: it follows by itself
    CHECK(arrival.mayGenerate);
    CHECK(arrival.intervalTicks == 17 * kMs);
}

TEST_CASE("k = 1: the Generated Frame at once, the Unique Frame half an interval after that present") {
    FrameSchedule schedule(kSecond);
    REQUIRE(TwoFrames(schedule, 1000 * kMs, 32 * kMs).mayGenerate);
    schedule.Begin(1);
    CHECK(schedule.Generated() == 1);
    CHECK(schedule.InProgress());
    CHECK(schedule.TicksUntilWake(1001 * kMs, false) == 0);
    CHECK(schedule.DueFrame(1001 * kMs) == 0);  // the Generated Frame, whatever the time
    CHECK(schedule.Presented(1002 * kMs) == 0);  // the times count from this present, not from the arrival

    CHECK_FALSE(schedule.DueFrame(1017 * kMs).has_value());
    CHECK(schedule.TicksUntilWake(1010 * kMs, false) == 8 * kMs);
    CHECK(schedule.DueFrame(1018 * kMs) == 1);  // the Unique Frame itself: 1002 + 32 / 2
    CHECK(schedule.Presented(1018 * kMs) == 0);
    CHECK_FALSE(schedule.InProgress());
    CHECK_FALSE(schedule.DueFrame(2000 * kMs).has_value());
    CHECK_FALSE(schedule.TicksUntilWake(1019 * kMs, false).has_value());
}

TEST_CASE("k = 3: three Generated Frames, then the Unique Frame, a quarter of the interval apart") {
    FrameSchedule schedule(kSecond);
    REQUIRE(TwoFrames(schedule, 1000 * kMs, 32 * kMs).mayGenerate);
    schedule.Begin(3);
    CHECK(schedule.Generated() == 3);
    CHECK(schedule.DueFrame(1000 * kMs) == 0);
    schedule.Presented(1000 * kMs);
    for (int frame = 1; frame <= 3; frame++) {
        const int64_t due = (1000 + 8 * frame) * kMs;
        CHECK_FALSE(schedule.DueFrame(due - 1).has_value());
        CHECK(schedule.TicksUntilWake(due - 3 * kMs, false) == 3 * kMs);
        CHECK(schedule.DueFrame(due) == frame);
        CHECK(schedule.Presented(due) == 0);
    }
    CHECK_FALSE(schedule.InProgress());
    const TimedFrameStats stats = schedule.TakeStats();
    CHECK(stats.uniqueFrames == 2u);
    CHECK(stats.withGenerated == 1u);
    CHECK(stats.presented == 5u);  // the first Unique Frame alone, then four
    CHECK(stats.timed == 3u);
    CHECK(stats.droppedEarly == 0u);
    CHECK(stats.skippedLate == 0u);
    CHECK(stats.latenessWorstTicks == 0);
}

TEST_CASE("a step that does not divide evenly keeps the last frame on the whole interval's fraction") {
    FrameSchedule schedule(kSecond);
    TwoFrames(schedule, 1000 * kMs, 33333);
    schedule.Begin(3);
    schedule.Presented(0);
    CHECK_FALSE(schedule.DueFrame(8332).has_value());
    CHECK(schedule.DueFrame(8333) == 1);
    schedule.Presented(8333);
    schedule.Presented(16666);
    CHECK_FALSE(schedule.DueFrame(24998).has_value());  // 3 * 33333 / 4, not three rounded steps
    CHECK(schedule.DueFrame(24999) == 3);
}

TEST_CASE("a late present does not move the later due times, and its lateness is counted") {
    FrameSchedule schedule(kSecond);
    TwoFrames(schedule, 1000 * kMs, 32 * kMs);
    schedule.Begin(3);
    schedule.Presented(1000 * kMs);
    CHECK(schedule.DueFrame(1011 * kMs) == 1);
    CHECK(schedule.Presented(1011 * kMs) == 3 * kMs);  // due at 1008
    CHECK_FALSE(schedule.DueFrame(1016 * kMs - 1).has_value());
    CHECK(schedule.DueFrame(1016 * kMs) == 2);  // still 1000 + 16, not 1011 + 8
    CHECK(schedule.Presented(1016 * kMs + 500) == 500);
    CHECK(schedule.TicksUntilWake(1020 * kMs, false) == 4 * kMs);
    CHECK(schedule.DueFrame(1024 * kMs) == 3);
    CHECK(schedule.Presented(1024 * kMs) == 0);
    const TimedFrameStats stats = schedule.TakeStats();
    CHECK(stats.timed == 3u);
    CHECK(stats.latenessWorstTicks == 3 * kMs);
    CHECK(stats.latenessSumTicks == 3 * kMs + 500);
}

TEST_CASE("a present a little before its due time has no lateness") {
    FrameSchedule schedule(kSecond);
    TwoFrames(schedule, 1000 * kMs, 32 * kMs);
    schedule.Begin(1);
    schedule.Presented(1000 * kMs);
    CHECK(schedule.DueFrame(1016 * kMs) == 1);
    CHECK(schedule.Presented(1016 * kMs - 200) == 0);
    CHECK(schedule.TakeStats().latenessSumTicks == 0);
}

TEST_CASE("lateness never bunches frames: one whose successor is due already is skipped, never the Unique Frame") {
    FrameSchedule schedule(kSecond);
    TwoFrames(schedule, 1000 * kMs, 32 * kMs);
    schedule.TakeStats();
    schedule.Begin(3);
    schedule.Presented(1000 * kMs);
    // Woken only at 1017 (a long resize): frame 1 (1008) is past and frame 2 (1016) is due.
    CHECK(schedule.DueFrame(1017 * kMs) == 2);
    CHECK(schedule.Presented(1017 * kMs) == 1 * kMs);
    CHECK_FALSE(schedule.DueFrame(1023 * kMs).has_value());  // frame 3 keeps its own time
    // Far too late for everything: the Unique Frame is still shown.
    CHECK(schedule.DueFrame(1500 * kMs) == 3);
    schedule.Presented(1500 * kMs);
    const TimedFrameStats stats = schedule.TakeStats();
    CHECK(stats.skippedLate == 1u);
    CHECK(stats.presented == 3u);
    CHECK(stats.droppedEarly == 0u);

    // All of it late: only the Unique Frame is shown.
    REQUIRE(schedule.UniqueFrameArrived(1032 * kMs, 3, 1920, 1080).mayGenerate);
    schedule.Begin(3);
    schedule.Presented(1532 * kMs);
    CHECK(schedule.DueFrame(1600 * kMs) == 3);
    CHECK(schedule.TakeStats().skippedLate == 2u);
}

TEST_CASE("the next Unique Frame arriving early drops the rest of the old sequence and starts its own") {
    FrameSchedule schedule(kSecond);
    TwoFrames(schedule, 1000 * kMs, 32 * kMs);
    schedule.Begin(3);
    schedule.Presented(1000 * kMs);
    schedule.Presented(1008 * kMs);
    schedule.TakeStats();
    // Frames 2 and 3 (the Unique Frame itself) were still to come.
    const FrameSchedule::Arrival arrival = schedule.UniqueFrameArrived(1014 * kMs, 3, 1920, 1080);
    CHECK(arrival.mayGenerate);
    CHECK(arrival.intervalTicks == 14 * kMs);
    CHECK_FALSE(schedule.InProgress());  // nothing of the old one can be shown any more
    CHECK_FALSE(schedule.DueFrame(1024 * kMs).has_value());
    schedule.Begin(1);
    CHECK(schedule.DueFrame(1015 * kMs) == 0);  // the new one's first frame, at once: never back in time
    schedule.Presented(1015 * kMs);
    CHECK_FALSE(schedule.DueFrame(1021 * kMs).has_value());
    CHECK(schedule.DueFrame(1022 * kMs) == 1);
    const TimedFrameStats stats = schedule.TakeStats();
    CHECK(stats.droppedEarly == 2u);
    CHECK(stats.presented == 1u);
    CHECK(stats.uniqueFrames == 1u);
}

TEST_CASE("a Unique Frame arriving before the old sequence's first present drops all of it") {
    FrameSchedule schedule(kSecond);
    TwoFrames(schedule, 1000 * kMs, 32 * kMs);
    schedule.Begin(1);
    schedule.TakeStats();
    schedule.UniqueFrameArrived(1016 * kMs, 3, 1920, 1080);
    CHECK(schedule.TakeStats().droppedEarly == 2u);
}

TEST_CASE("the first Unique Frame after a start is shown at once, alone") {
    FrameSchedule schedule(kSecond);
    const FrameSchedule::Arrival arrival = schedule.UniqueFrameArrived(500 * kMs, 1, 1920, 1080);
    CHECK_FALSE(arrival.mayGenerate);
    CHECK(arrival.forgetEarlier);
    schedule.Begin(3);  // whatever a generator answered
    CHECK(schedule.Generated() == 0);
    CHECK(schedule.DueFrame(500 * kMs) == 0);  // the Unique Frame itself
    schedule.Presented(500 * kMs);
    CHECK_FALSE(schedule.InProgress());
    CHECK_FALSE(schedule.HoldsRedraw(500 * kMs));
    CHECK(schedule.TakeStats().withGenerated == 0u);
}

TEST_CASE("after Forget (a device rebuild) the next Unique Frame is shown at once, alone") {
    FrameSchedule schedule(kSecond);
    TwoFrames(schedule, 1000 * kMs, 32 * kMs);
    schedule.Begin(3);
    schedule.Presented(1000 * kMs);
    schedule.Forget();
    CHECK_FALSE(schedule.InProgress());  // the sequence in progress is over
    CHECK_FALSE(schedule.DueFrame(1008 * kMs).has_value());
    CHECK_FALSE(schedule.HoldsRedraw(1008 * kMs));
    const FrameSchedule::Arrival arrival = schedule.UniqueFrameArrived(1032 * kMs, 3, 1920, 1080);
    CHECK_FALSE(arrival.mayGenerate);
    CHECK(arrival.forgetEarlier);
    schedule.Begin(0);
    schedule.Presented(1032 * kMs);
    CHECK(schedule.UniqueFrameArrived(1064 * kMs, 4, 1920, 1080).mayGenerate);  // and the one after it as usual
}

TEST_CASE("a change of picture size: at once, alone, the earlier frame forgotten") {
    FrameSchedule schedule(kSecond);
    TwoFrames(schedule, 1000 * kMs, 32 * kMs);
    schedule.Begin(1);
    FrameSchedule::Arrival arrival = schedule.UniqueFrameArrived(1032 * kMs, 3, 1280, 720);
    CHECK_FALSE(arrival.mayGenerate);
    CHECK(arrival.forgetEarlier);
    schedule.Begin(1);
    CHECK(schedule.Generated() == 0);
    arrival = schedule.UniqueFrameArrived(1064 * kMs, 4, 1280, 1080);  // only the height
    CHECK_FALSE(arrival.mayGenerate);
    CHECK(arrival.forgetEarlier);
    schedule.Begin(1);
    CHECK(schedule.UniqueFrameArrived(1096 * kMs, 5, 1280, 1080).mayGenerate);
}

TEST_CASE("a frame sequence number going backwards (a Feed restart): at once, alone, the earlier frame forgotten") {
    FrameSchedule schedule(kSecond);
    schedule.UniqueFrameArrived(1000 * kMs, 500, 1920, 1080);
    schedule.Begin(0);
    const FrameSchedule::Arrival arrival = schedule.UniqueFrameArrived(1032 * kMs, 1, 1920, 1080);
    CHECK_FALSE(arrival.mayGenerate);
    CHECK(arrival.forgetEarlier);
    schedule.Begin(0);
    CHECK(schedule.UniqueFrameArrived(1064 * kMs, 2, 1920, 1080).mayGenerate);
}

TEST_CASE("a gap over 150 ms (a pause): at once, alone, the earlier frame forgotten") {
    FrameSchedule schedule(kSecond);
    schedule.UniqueFrameArrived(1000 * kMs, 1, 1920, 1080);
    schedule.Begin(0);
    FrameSchedule::Arrival arrival = schedule.UniqueFrameArrived(1150 * kMs + 1, 10, 1920, 1080);
    CHECK_FALSE(arrival.mayGenerate);
    CHECK(arrival.forgetEarlier);
    CHECK(arrival.intervalTicks == 0);
    schedule.Begin(1);
    CHECK(schedule.Generated() == 0);
    // Exactly 150 ms is not a pause, but still too long an interval to generate for.
    arrival = schedule.UniqueFrameArrived(1300 * kMs + 1, 19, 1920, 1080);
    CHECK_FALSE(arrival.mayGenerate);
    CHECK_FALSE(arrival.forgetEarlier);
}

TEST_CASE("an interval under 6 ms or over 100 ms: at once, alone, the earlier frame kept") {
    FrameSchedule schedule(kSecond);
    schedule.UniqueFrameArrived(1000 * kMs, 1, 1920, 1080);
    schedule.Begin(0);

    FrameSchedule::Arrival arrival = schedule.UniqueFrameArrived(1006 * kMs - 1, 2, 1920, 1080);
    CHECK_FALSE(arrival.mayGenerate);
    CHECK_FALSE(arrival.forgetEarlier);
    schedule.Begin(3);
    CHECK(schedule.Generated() == 0);
    CHECK(schedule.DueFrame(1006 * kMs) == 0);
    schedule.Presented(1006 * kMs);
    CHECK_FALSE(schedule.InProgress());

    arrival = schedule.UniqueFrameArrived(1012 * kMs - 1, 3, 1920, 1080);  // 6 ms: the shortest used
    CHECK(arrival.mayGenerate);
    schedule.Begin(0);

    arrival = schedule.UniqueFrameArrived(1112 * kMs - 1, 4, 1920, 1080);  // 100 ms: the longest used
    CHECK(arrival.mayGenerate);
    schedule.Begin(0);

    arrival = schedule.UniqueFrameArrived(1212 * kMs, 5, 1920, 1080);  // just over
    CHECK_FALSE(arrival.mayGenerate);
    CHECK_FALSE(arrival.forgetEarlier);
    CHECK(arrival.intervalTicks == 100 * kMs + 1);

    arrival = schedule.UniqueFrameArrived(1211 * kMs, 6, 1920, 1080);  // a time going backwards
    CHECK_FALSE(arrival.mayGenerate);
    CHECK(arrival.intervalTicks == 0);
}

TEST_CASE("the generator's answer: 1 or 3 Generated Frames, never two, none when it made none") {
    FrameSchedule schedule(kSecond);
    TwoFrames(schedule, 1000 * kMs, 32 * kMs);
    schedule.Begin(2);
    CHECK(schedule.Generated() == 1);
    schedule.Begin(7);
    CHECK(schedule.Generated() == 3);
    schedule.Begin(0);
    CHECK(schedule.Generated() == 0);
    schedule.Begin(-1);
    CHECK(schedule.Generated() == 0);
    CHECK(schedule.DueFrame(1000 * kMs) == 0);  // the Unique Frame, at once
}

TEST_CASE("a failed present moves the sequence on without counting a frame presented") {
    FrameSchedule schedule(kSecond);
    TwoFrames(schedule, 1000 * kMs, 32 * kMs);
    schedule.TakeStats();
    schedule.Begin(1);
    schedule.Presented(1000 * kMs, false);
    CHECK(schedule.DueFrame(1016 * kMs) == 1);
    schedule.Presented(1016 * kMs);
    CHECK_FALSE(schedule.InProgress());
    CHECK(schedule.TakeStats().presented == 1u);
}

// What keeps a text or a resize from adding a present between two frames.
TEST_CASE("a drawing asked for by something else is held while a sequence is in progress") {
    FrameSchedule schedule(kSecond);
    TwoFrames(schedule, 1000 * kMs, 32 * kMs);
    schedule.Begin(3);
    CHECK(schedule.HoldsRedraw(1000 * kMs));  // even before the first present
    schedule.Presented(1000 * kMs);
    CHECK(schedule.HoldsRedraw(1003 * kMs));
    // The wake-up is the next frame's, with or without a drawing waiting.
    CHECK(schedule.TicksUntilWake(1003 * kMs, true) == 5 * kMs);
    CHECK(schedule.TicksUntilWake(1003 * kMs, false) == 5 * kMs);
    CHECK(schedule.TicksUntilWake(1009 * kMs, true) == 0);  // overdue: now
}

TEST_CASE("after a sequence of several frames a drawing waits for the next Unique Frame, one step past its expected time at most") {
    FrameSchedule schedule(kSecond);
    TwoFrames(schedule, 1000 * kMs, 32 * kMs);
    schedule.Begin(3);
    schedule.Presented(1000 * kMs);
    schedule.Presented(1008 * kMs);
    schedule.Presented(1016 * kMs);
    schedule.Presented(1024 * kMs);
    REQUIRE_FALSE(schedule.InProgress());
    // The next Unique Frame is expected at 1032; held until 1040.
    CHECK(schedule.HoldsRedraw(1025 * kMs));
    CHECK(schedule.HoldsRedraw(1040 * kMs - 1));
    CHECK_FALSE(schedule.HoldsRedraw(1040 * kMs));  // a still picture: the drawing goes out alone
    CHECK(schedule.TicksUntilWake(1030 * kMs, true) == 10 * kMs);
    CHECK(schedule.TicksUntilWake(1045 * kMs, true) == 0);
    CHECK_FALSE(schedule.TicksUntilWake(1030 * kMs, false).has_value());  // nothing waits: no wake-up

    // The next Unique Frame takes the drawing with its first frame.
    schedule.UniqueFrameArrived(1032 * kMs, 3, 1920, 1080);
    schedule.Begin(3);
    CHECK(schedule.HoldsRedraw(1033 * kMs));
    CHECK(schedule.DueFrame(1033 * kMs) == 0);
}

TEST_CASE("a Unique Frame shown alone holds no drawing afterwards") {
    FrameSchedule schedule(kSecond);
    TwoFrames(schedule, 1000 * kMs, 32 * kMs);
    schedule.Begin(1);
    schedule.Presented(1000 * kMs);
    schedule.Presented(1016 * kMs);
    REQUIRE(schedule.HoldsRedraw(1017 * kMs));
    schedule.UniqueFrameArrived(1300 * kMs, 3, 1920, 1080);  // after a pause: alone
    schedule.Begin(1);
    CHECK(schedule.HoldsRedraw(1300 * kMs));  // until it is presented
    schedule.Presented(1300 * kMs);
    CHECK_FALSE(schedule.HoldsRedraw(1300 * kMs));
    CHECK_FALSE(schedule.TicksUntilWake(1300 * kMs, true).has_value());
}

TEST_CASE("the counts are since the previous taking, and add up") {
    FrameSchedule schedule(kSecond);
    TwoFrames(schedule, 1000 * kMs, 32 * kMs);
    schedule.Begin(1);
    schedule.Presented(1000 * kMs);
    schedule.Presented(1017 * kMs);
    TimedFrameStats total = schedule.TakeStats();
    CHECK(total.uniqueFrames == 2u);
    CHECK(total.presented == 3u);
    CHECK(schedule.TakeStats().presented == 0u);

    TimedFrameStats more;
    more.uniqueFrames = 1;
    more.withGenerated = 1;
    more.presented = 4;
    more.droppedEarly = 2;
    more.skippedLate = 1;
    more.timed = 3;
    more.latenessSumTicks = 900;
    more.latenessWorstTicks = 400;
    total.Add(more);
    CHECK(total.uniqueFrames == 3u);
    CHECK(total.withGenerated == 2u);
    CHECK(total.presented == 7u);
    CHECK(total.droppedEarly == 2u);
    CHECK(total.skippedLate == 1u);
    CHECK(total.timed == 4u);
    CHECK(total.latenessSumTicks == 1 * kMs + 900);
    CHECK(total.latenessWorstTicks == 1 * kMs);  // the larger of the two
}

TEST_CASE("a new Feed generation forgets the earlier frame even when sequence numbers match") {
    FrameSchedule schedule(kSecond);
    schedule.UniqueFrameArrived(100 * kMs, 1, 1920, 1080, 4);
    const auto restarted = schedule.UniqueFrameArrived(117 * kMs, 1, 1920, 1080, 5);
    CHECK(restarted.forgetEarlier);
    CHECK_FALSE(restarted.mayGenerate);
}

TEST_CASE("only an output stamped with the current input can be shown") {
    CHECK(gp::CurrentGeneratedOutput(42, 42));
    CHECK_FALSE(gp::CurrentGeneratedOutput(42, 41));
    CHECK_FALSE(gp::CurrentGeneratedOutput(42, 43));
}

TEST_CASE("both quarters must be ready to show more than the middle") {
    CHECK(gp::ReadyGeneratedFrames(true, true, true) == 3);
    CHECK(gp::ReadyGeneratedFrames(true, true, false) == 1);
    CHECK(gp::ReadyGeneratedFrames(true, false, true) == 1);
    CHECK(gp::ReadyGeneratedFrames(true, false, false) == 1);
    CHECK(gp::ReadyGeneratedFrames(false, false, false) == 0);
}

TEST_CASE("quarter interpolation restarts from the current real frame after a gap in its input") {
    CHECK(gp::CanGenerateQuarters(true, true, false));
    CHECK_FALSE(gp::CanGenerateQuarters(false, true, false)); // entering 4x from 2x
    CHECK_FALSE(gp::CanGenerateQuarters(true, false, false)); // no middle for this interval
    CHECK_FALSE(gp::CanGenerateQuarters(true, true, true));   // Feed restart or pause
}

#include "core/presentation_info.h"
TEST_CASE("superseded real endpoints do not count as successful real presents") {
    FrameSchedule schedule(kSecond);
    TwoFrames(schedule, 1000 * kMs, 32 * kMs);
    schedule.Begin(3);
    gp::PresentedFrames all, real;
    for (const int ms : {1000,1008}) {
        const auto due = schedule.DueFrame(ms * kMs);
        REQUIRE(due.has_value());
        all.Presented(ms);
        if (*due == schedule.Generated()) real.Presented(ms);
        schedule.Presented(ms * kMs);
    }
    // An early arrival discards the pending real endpoint of the old sequence.
    schedule.UniqueFrameArrived(1014 * kMs, 3, 1920, 1080);
    CHECK(real.RateAt(2000) == 0);
    CHECK(all.RateAt(2000) == 1);
    CHECK(schedule.TakeStats().droppedEarly == 2);
}
TEST_CASE("a new size can use all quarter frames on its second picture when the driver is ready") {
    FrameSchedule schedule(kSecond);
    TwoFrames(schedule, 1000 * kMs, 32 * kMs);
    CHECK(schedule.UniqueFrameArrived(1032 * kMs, 3, 1280, 720).forgetEarlier);
    schedule.Begin(0);
    schedule.Presented(1032 * kMs);
    REQUIRE(schedule.UniqueFrameArrived(1064 * kMs, 4, 1280, 720).mayGenerate);
    schedule.Begin(3);
    CHECK(schedule.Generated() == 3);
    CHECK(schedule.DueFrame(1064 * kMs) == 0);
}

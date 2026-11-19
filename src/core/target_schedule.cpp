#include "core/target_schedule.h"

#include <algorithm>

#include "core/target_rate.h"

namespace gp {

TargetSchedule::TargetSchedule(int64_t ticksPerSecond) : arrivals_(ticksPerSecond), ticksPerSecond_(ticksPerSecond) {}

FrameSchedule::Arrival TargetSchedule::UniqueFrameArrived(int64_t arrivalTicks, uint64_t sequence, uint32_t width,
                                                          uint32_t height, uint32_t feedGeneration) {
    if (begun_ && shown_ < generated_) stats_.notShown++;  // its own picture never reached the screen
    begun_ = false;
    shown_ = -1;
    arrivalTicks_ = arrivalTicks;
    arrival_ = arrivals_.UniqueFrameArrived(arrivalTicks, sequence, width, height, feedGeneration);
    return arrival_;
}

void TargetSchedule::Begin(int generated, int rateInUse, int64_t startTicks, int64_t takeWaitTicks) {
    const bool measured = arrival_.intervalTicks > 0;
    // Q17 from the interval; a Unique Frame without one (a start, a pause)
    // keeps what the one before said.
    if (measured) realOnly_ = RealFramesOnly(arrival_.intervalTicks, rateInUse, ticksPerSecond_);
    const bool none = realOnly_ || !arrival_.mayGenerate || generated < 1;
    generated_ = none ? 0 : generated >= 7 ? 7 : generated >= 3 ? 3 : 1;
    intervalTicks_ = arrival_.intervalTicks;
    readyTicks_ = startTicks;
    // The next Unique Frame is due an interval after this one and taken at
    // the latest takeWaitTicks after that; the present displayed from then
    // on belongs to it. From a sixteenth of a step before (arrival jitter:
    // more would show the Unique Frame a vsync early, then again, more
    // often), this one's own picture is shown.
    lastChanceTicks_ = arrivalTicks_ + intervalTicks_ + takeWaitTicks -
                       (generated_ > 0 ? intervalTicks_ / (16 * (generated_ + 1)) : 0);
    begun_ = true;
    shown_ = -1;
    stats_.uniqueFrames++;
    if (measured || stats_.ladder == 0) {
        stats_.ladder = generated_ + 1;
        stats_.realOnly = realOnly_;
    }
}

void TargetSchedule::Forget() {
    arrivals_.Forget();
    arrival_ = {};
    begun_ = false;
    generated_ = 0;
    shown_ = -1;
}

std::optional<int> TargetSchedule::FrameFor(int64_t vsyncTicks) {
    stats_.vsyncs++;
    if (!begun_ || shown_ >= generated_) {
        stats_.repeats++;
        return std::nullopt;
    }
    int frame = 0;
    if (generated_ > 0 && intervalTicks_ > 0) {
        // Steps of interval / (generated + 1) since the ladder was ready, to
        // the nearest (a half step or more rounds up). Before then: the first.
        const int64_t since = std::max<int64_t>(vsyncTicks - readyTicks_, 0);
        const int64_t steps = (2 * since * (generated_ + 1) + intervalTicks_) / (2 * intervalTicks_);
        frame = static_cast<int>(std::min<int64_t>(steps, generated_));
        // The last present before the next Unique Frame takes over: the
        // Unique Frame itself, never dropped for a Generated Frame (Q17).
        if (vsyncTicks >= lastChanceTicks_) frame = generated_;
    }
    if (frame <= shown_) {
        stats_.repeats++;
        return std::nullopt;
    }
    shown_ = frame;
    if (frame == generated_) {
        stats_.uniqueShown++;
    } else {
        stats_.generatedShown++;
    }
    return frame;
}

TargetStats TargetSchedule::TakeStats() {
    arrivals_.TakeStats();  // its own counters are not used
    TargetStats taken = stats_;
    stats_ = {};
    stats_.ladder = taken.ladder;
    stats_.realOnly = taken.realOnly;
    return taken;
}

}  // namespace gp

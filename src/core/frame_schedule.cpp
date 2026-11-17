#include "core/frame_schedule.h"

#include <algorithm>

namespace gp {

int GeneratedFramesFor(int timesShown) {
    if (timesShown == 2) return 1;
    if (timesShown == 4) return 3;
    return 0;
}

void TimedFrameStats::Add(const TimedFrameStats& more) {
    uniqueFrames += more.uniqueFrames;
    withGenerated += more.withGenerated;
    presented += more.presented;
    droppedEarly += more.droppedEarly;
    skippedLate += more.skippedLate;
    timed += more.timed;
    latenessSumTicks += more.latenessSumTicks;
    latenessWorstTicks = std::max(latenessWorstTicks, more.latenessWorstTicks);
}

FrameSchedule::FrameSchedule(int64_t ticksPerSecond) : ticksPerSecond_(ticksPerSecond) {}

FrameSchedule::Arrival FrameSchedule::UniqueFrameArrived(int64_t arrivalTicks, uint64_t sequence, uint32_t width,
                                                         uint32_t height, uint32_t feedGeneration) {
    // Never queue, never go back: what the old sequence had left is not shown.
    if (InProgress()) stats_.droppedEarly += static_cast<uint32_t>(generated_ - next_ + 1);
    generated_ = 0;
    next_ = 1;
    anchored_ = false;
    holdUntil_ = 0;

    Arrival arrival;
    const int64_t gap = arrivalTicks - previousArrival_;
    if (!havePrevious_ || feedGeneration != previousFeedGeneration_ ||
        width != previousWidth_ || height != previousHeight_ || sequence < previousSequence_ ||
        gap > ticksPerSecond_ * kMaxGapMs / 1000) {
        arrival.forgetEarlier = true;
    } else {
        arrival.intervalTicks = std::max<int64_t>(gap, 0);
        arrival.mayGenerate = gap >= ticksPerSecond_ * kMinIntervalMs / 1000 &&
                              gap <= ticksPerSecond_ * kMaxIntervalMs / 1000;
    }
    havePrevious_ = true;
    previousFeedGeneration_ = feedGeneration;
    previousArrival_ = arrivalTicks;
    previousSequence_ = sequence;
    previousWidth_ = width;
    previousHeight_ = height;
    arrival_ = arrival;
    stats_.uniqueFrames++;
    return arrival;
}

void FrameSchedule::Begin(int generated) {
    if (!arrival_.mayGenerate || generated < 1) {
        generated = 0;
    } else {
        generated = generated >= 3 ? 3 : 1;  // never two
    }
    generated_ = generated;
    next_ = 0;
    anchored_ = false;
    intervalTicks_ = arrival_.intervalTicks;
    holdUntil_ = 0;
    if (generated_ > 0) stats_.withGenerated++;
}

void FrameSchedule::Forget() {
    havePrevious_ = false;
    arrival_ = {};
    generated_ = 0;
    next_ = 1;
    anchored_ = false;
    holdUntil_ = 0;
}

int64_t FrameSchedule::DueAt(int frame) const {
    return firstPresent_ + intervalTicks_ * frame / (generated_ + 1);
}

std::optional<int> FrameSchedule::DueFrame(int64_t nowTicks) {
    if (!InProgress()) return std::nullopt;
    if (!anchored_) return next_;  // the first: at once
    while (next_ < generated_ && DueAt(next_ + 1) <= nowTicks) {
        next_++;
        stats_.skippedLate++;
    }
    if (DueAt(next_) > nowTicks) return std::nullopt;
    return next_;
}

int64_t FrameSchedule::Presented(int64_t nowTicks, bool wentThrough) {
    if (!InProgress()) return 0;
    int64_t lateness = 0;
    if (!anchored_) {
        anchored_ = true;
        firstPresent_ = nowTicks;
    } else {
        lateness = std::max<int64_t>(nowTicks - DueAt(next_), 0);
        stats_.timed++;
        stats_.latenessSumTicks += lateness;
        stats_.latenessWorstTicks = std::max(stats_.latenessWorstTicks, lateness);
    }
    if (wentThrough) stats_.presented++;
    next_++;
    if (!InProgress() && generated_ > 0) {
        // The next Unique Frame is expected one interval after this one's
        // sequence began; one step more allows for its arrival being uneven.
        holdUntil_ = DueAt(generated_ + 2);
    }
    return lateness;
}

bool FrameSchedule::HoldsRedraw(int64_t nowTicks) const {
    return InProgress() || (holdUntil_ != 0 && nowTicks < holdUntil_);
}

std::optional<int64_t> FrameSchedule::TicksUntilWake(int64_t nowTicks, bool redrawWaiting) const {
    if (InProgress()) {
        if (!anchored_) return 0;
        return std::max<int64_t>(DueAt(next_) - nowTicks, 0);
    }
    if (redrawWaiting && holdUntil_ != 0) return std::max<int64_t>(holdUntil_ - nowTicks, 0);
    return std::nullopt;
}

TimedFrameStats FrameSchedule::TakeStats() {
    const TimedFrameStats taken = stats_;
    stats_ = {};
    return taken;
}

}  // namespace gp

#include "core/target_schedule.h"

#include <algorithm>

namespace gp {

TargetSchedule::TargetSchedule(int64_t ticksPerSecond) : arrivals_(ticksPerSecond) {}

FrameSchedule::Arrival TargetSchedule::UniqueFrameArrived(int64_t arrivalTicks, uint64_t sequence, uint32_t width,
                                                          uint32_t height, uint32_t feedGeneration) {
    if (begun_ && shown_ < generated_) stats_.notShown++;  // its own picture never reached the screen
    begun_ = false;
    shown_ = -1;
    anchored_ = false;
    arrival_ = arrivals_.UniqueFrameArrived(arrivalTicks, sequence, width, height, feedGeneration);
    return arrival_;
}

void TargetSchedule::Begin(int generated, int64_t refreshTicks) {
    const bool realOnly = !arrival_.mayGenerate || generated < 1 || arrival_.intervalTicks <= refreshTicks;
    generated_ = realOnly ? 0 : generated >= 3 ? 3 : 1;
    intervalTicks_ = arrival_.intervalTicks;
    begun_ = true;
    shown_ = -1;
    anchored_ = false;
    stats_.uniqueFrames++;
    stats_.ladder = generated_ + 1;
}

void TargetSchedule::Forget() {
    arrivals_.Forget();
    arrival_ = {};
    begun_ = false;
    generated_ = 0;
    shown_ = -1;
    anchored_ = false;
}

std::optional<int> TargetSchedule::FrameFor(int64_t vsyncTicks) {
    stats_.vsyncs++;
    if (!begun_ || shown_ >= generated_) {
        stats_.repeats++;
        return std::nullopt;
    }
    int frame = 0;
    if (!anchored_) {
        // The first vsync of this Unique Frame shows its first ladder frame
        // and sets the time the others are counted from.
        anchored_ = true;
        firstVsync_ = vsyncTicks;
    } else if (generated_ > 0 && intervalTicks_ > 0) {
        // Steps of interval / (generated + 1) since the first, to the nearest
        // (a half step or more rounds up).
        const int64_t since = std::max<int64_t>(vsyncTicks - firstVsync_, 0);
        const int64_t steps = (2 * since * (generated_ + 1) + intervalTicks_) / (2 * intervalTicks_);
        frame = static_cast<int>(std::min<int64_t>(steps, generated_));
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
    return taken;
}

}  // namespace gp

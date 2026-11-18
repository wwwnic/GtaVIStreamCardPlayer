// Which frame each refresh shows while Frame Generation follows a Target Rate
// (ADR 0004). Beside FrameSchedule, which 2x and 4x keep using unchanged.
//
// The frames available for Unique Frame N are the ladder: its Generated
// Frames between N-1 and N (3 at 4x: first quarter, middle, last quarter; 1
// at 2x: the middle) and N itself, at positions 1/4, 2/4, 3/4, 1 (or 1/2, 1)
// of the interval between the last two Unique Frames. The Presenter presents
// once per vsync; for each it asks which ladder frame is nearest in time to
// that vsync's display time. The times are those 4x would show them at: the
// first ladder frame when the ladder is ready, the others one step apart
// from then. Counted from the ready time (not from the first vsync), so with
// evenly spaced Unique Frames the times run on without a seam from one
// Unique Frame to the next and no step is skipped. Never backwards, and never
// a frame of an older Unique Frame once a newer one has begun: the first
// vsync of a ladder shows at least its first frame.
//
// Pure logic. Times are in the unit of Frame::arrivalQpc.
#pragma once

#include <cstdint>
#include <optional>

#include "core/frame_schedule.h"

namespace gp {

// Counted since the previous TargetSchedule::TakeStats.
struct TargetStats {
    uint32_t vsyncs = 0;          // FrameFor calls: one per vsync presented for
    uint32_t uniqueFrames = 0;    // Unique Frames begun
    uint32_t generatedShown = 0;  // vsyncs that showed a Generated Frame new to the screen
    uint32_t uniqueShown = 0;     // vsyncs that showed a Unique Frame itself for the first time
    uint32_t repeats = 0;         // vsyncs that showed the previous frame again
    uint32_t notShown = 0;        // Unique Frames whose own picture was overtaken before shown
    int ladder = 0;               // frames in the latest ladder: 4, 2 or 1 (real frames only)
};

class TargetSchedule {
public:
    explicit TargetSchedule(int64_t ticksPerSecond);

    // A new Unique Frame was taken. The same answer and the same rules as
    // FrameSchedule::UniqueFrameArrived (a start, a pause over kMaxGapMs,
    // another size forget the earlier frame; an interval outside
    // kMinIntervalMs..kMaxIntervalMs does not generate). Ends the sequence in
    // progress. Follow it with Begin.
    FrameSchedule::Arrival UniqueFrameArrived(int64_t arrivalTicks, uint64_t sequence, uint32_t width, uint32_t height,
                                              uint32_t feedGeneration = 0);

    // Its ladder is ready: `generated` Generated Frames (3 or more counts as
    // 3, 1 or 2 as 1, anything else as 0). None either when the arrival said
    // not to generate, or when a refresh (`refreshTicks`, the time between
    // vsyncs) is not shorter than the interval: the rate in use is not above
    // the Game Rate, so real frames only. readyTicks: when the ladder was
    // ready (its Generated Frames made), the time of its first frame.
    void Begin(int generated, int64_t refreshTicks, int64_t readyTicks);

    // Forgets the earlier Unique Frame and ends the sequence (a device
    // rebuild, generation stopped).
    void Forget();

    // For the vsync displayed at vsyncTicks: the ladder frame to show, 0 first,
    // Generated() the Unique Frame itself. None: nothing nearer than what was
    // shown last, so show that again (also before any Unique Frame).
    std::optional<int> FrameFor(int64_t vsyncTicks);

    // Generated Frames in the current ladder.
    int Generated() const { return generated_; }
    // The ladder frame shown last; -1 when none of this Unique Frame yet.
    int Shown() const { return shown_; }

    TargetStats TakeStats();

private:
    FrameSchedule arrivals_;  // its arrival rules only; never begun
    FrameSchedule::Arrival arrival_;
    bool begun_ = false;
    int generated_ = 0;
    int shown_ = -1;
    int64_t intervalTicks_ = 0;
    int64_t readyTicks_ = 0;
    TargetStats stats_;
};

}  // namespace gp

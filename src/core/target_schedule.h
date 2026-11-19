// Which frame each refresh shows while Frame Generation follows a Target Rate
// (ADR 0004). Beside FrameSchedule, which 2x and 4x keep using unchanged.
//
// The frames available for Unique Frame N are the ladder: its Generated
// Frames between N-1 and N (7 on the x8 ladder: the eighths; 3 at 4x: first
// quarter, middle, last quarter; 1 at 2x: the middle) and N itself, at
// positions 1/8 ... 8/8, 1/4, 2/4, 3/4, 1 (or 1/2, 1)
// of the interval between the last two Unique Frames. The Presenter presents
// once per vsync; for each it asks which ladder frame is nearest in time to
// that vsync's display time. The times are those 4x would show them at: the
// first ladder frame at the start Begin is given (when the ladder is ready,
// or earlier: LadderStartTicks), the others one step apart from then.
// Counted from that start (not from the first vsync), so with evenly spaced
// Unique Frames the times run on without a seam from one Unique Frame to the
// next. Never backwards, and never a frame of an older Unique Frame once a
// newer one has begun: the first vsync of a ladder shows at least its first
// frame. Never a real frame dropped either (Q17): fewer vsyncs than ladder
// frames fall on each Unique Frame (2.4 for a 60 fps game at 144 Hz), so
// the nearest pick alone would pass over the Unique Frame itself about half
// the time; the last vsync before the next Unique Frame is due shows it.
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
    // Frames in the latest ladder: 8, 4, 2 or 1 (none). A Unique Frame after
    // a start or a pause (no interval, so no ladder) does not change it.
    int ladder = 0;
    bool realOnly = false;        // the latest ladder: real frames only (Q17)
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

    // Its ladder is ready: `generated` Generated Frames (7 or more counts as
    // 7, the x8 ladder of eighths; 3 to 6 as 3; 1 or 2 as 1; anything else
    // as 0). None either when the arrival said not to generate, or when the
    // rate in use is not above the Game Rate (RealFramesOnly in
    // target_rate.h, the one rule): real frames only. A Unique Frame with no
    // interval (a start, a pause) keeps the real-frames-only answer of the
    // one before. startTicks: when its first frame is due on screen.
    // takeWaitTicks: how long after arriving a Unique Frame is taken at the
    // latest (0: at once; on the x8 ladder the time between presents, as it
    // waits for the next present).
    void Begin(int generated, int rateInUse, int64_t startTicks, int64_t takeWaitTicks = 0);

    // Real frames only (Q17): the Presenter presents only for new frames.
    bool RealOnly() const { return realOnly_; }

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
    int64_t ticksPerSecond_;
    FrameSchedule::Arrival arrival_;
    int64_t arrivalTicks_ = 0;
    bool begun_ = false;
    bool realOnly_ = false;
    int generated_ = 0;
    int shown_ = -1;
    int64_t intervalTicks_ = 0;
    int64_t readyTicks_ = 0;
    int64_t lastChanceTicks_ = 0;  // from this display time on: the Unique Frame itself
    TargetStats stats_;
};

}  // namespace gp

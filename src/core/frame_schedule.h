// When the Presenter shows each frame once it shows more than one per Unique
// Frame (ADR 0003): for Unique Frame N with k Generated Frames between N-1 and
// N, the k Generated Frames first, in order, then N itself, one step apart
// (the interval between the last two Unique Frames, divided by k + 1).
//
// Pure logic. The Presenter's thread keeps one of these, passes the clock in
// and only waits and presents. Times are in the unit of Frame::arrivalQpc.
#pragma once

#include <cstdint>
#include <optional>

namespace gp {

constexpr bool CurrentGeneratedOutput(uint64_t submitted, uint64_t returned) {
    return submitted == returned;
}

// No Generated Frames for a Unique Frame whose interval is outside these: a
// shorter one cannot be real, a longer one would hold the picture back too long.
inline constexpr int kMinIntervalMs = 6;
inline constexpr int kMaxIntervalMs = 100;
// A longer gap between Unique Frames is a pause (a still picture, a restart):
// the frame before it is forgotten too.
inline constexpr int kMaxGapMs = 150;

// How many Generated Frames go with a Unique Frame shown `timesShown` times
// (the Generation Factor): 1 for 2, 3 for 4, otherwise 0.
int GeneratedFramesFor(int timesShown);

// Counted since the previous FrameSchedule::TakeStats.
struct TimedFrameStats {
    uint32_t uniqueFrames = 0;      // Unique Frames given to the schedule
    uint32_t withGenerated = 0;     // of them, shown as a sequence of more than one frame
    uint32_t presented = 0;         // frames presented, Generated Frames included
    uint32_t droppedEarly = 0;      // not shown: the next Unique Frame came before their time
    uint32_t skippedLate = 0;       // not shown: the time of the frame after them had come already
    uint32_t timed = 0;             // presents that had a due time (every frame of a sequence but its first)
    int64_t latenessSumTicks = 0;   // over those: how long after its due time each was presented
    int64_t latenessWorstTicks = 0;

    void Add(const TimedFrameStats& more);
};

class FrameSchedule {
public:
    // ticksPerSecond: the unit of every time given (the QPC frequency).
    explicit FrameSchedule(int64_t ticksPerSecond);

    // What may be shown with a new Unique Frame.
    struct Arrival {
        // False: show it at once, alone (see UniqueFrameArrived).
        bool mayGenerate = false;
        // True: the Unique Frame before it must not be used either (a start,
        // a pause, another picture size): a generator starts again from this one.
        bool forgetEarlier = false;
        // Between the arrivals of the last two Unique Frames; 0 when there is
        // no earlier one.
        int64_t intervalTicks = 0;
    };

    // A new Unique Frame was taken to be shown. Never queues: the frames of
    // the sequence in progress that were not shown yet are dropped (counted).
    // Not to generate: the first Unique Frame (after a start or Forget), one
    // of another picture size, a frame sequence number going backwards or a
    // gap over kMaxGapMs (those also forget the earlier frame), an interval
    // under kMinIntervalMs or over kMaxIntervalMs.
    // Follow it with Begin.
    Arrival UniqueFrameArrived(int64_t arrivalTicks, uint64_t sequence, uint32_t width, uint32_t height,
                               uint32_t feedGeneration = 0);

    // Starts the sequence for that Unique Frame: `generated` Generated Frames
    // (1 or 3; 2 counts as 1, more as 3, anything else and whenever the
    // arrival said not to generate as 0), then the Unique Frame itself. Its
    // first frame is due at once.
    void Begin(int generated);

    // Forgets the earlier Unique Frame and ends the sequence in progress (a
    // device rebuild, no generator any more): the next Unique Frame is shown
    // at once, alone.
    void Forget();

    // The frame of the sequence to present now, counted from 0; none when
    // nothing is due. A frame whose successor is due already is skipped, so
    // lateness never bunches frames; the Unique Frame itself is never skipped.
    std::optional<int> DueFrame(int64_t nowTicks);
    // Generated Frames in the sequence in progress; frame number Generated()
    // is the Unique Frame itself.
    int Generated() const { return generated_; }
    // A sequence has frames not shown yet.
    bool InProgress() const { return next_ <= generated_; }

    // The frame DueFrame gave was presented at nowTicks (wentThrough false:
    // the present failed; the sequence moves on all the same). The first
    // present of a sequence sets the time the others are counted from, so a
    // late present never moves a later due time. Returns how late it was (0
    // for the first, and when early).
    int64_t Presented(int64_t nowTicks, bool wentThrough = true);

    // A drawing asked for by something else (a text that appears or ends, a
    // resize) is not presented by itself while this is true: it goes out with
    // the next frame of the schedule, so it cannot bunch frames. True while a
    // sequence is in progress and, after a sequence of more than one frame,
    // until one step past the time the next Unique Frame is expected (then
    // the picture is still, and the drawing goes out alone).
    bool HoldsRedraw(int64_t nowTicks) const;

    // How long the Presenter's thread may sleep: until the next frame is due
    // or, when a held drawing is waiting, until it is held no longer. 0: do
    // not sleep. None: nothing here needs a wake-up.
    std::optional<int64_t> TicksUntilWake(int64_t nowTicks, bool redrawWaiting) const;

    TimedFrameStats TakeStats();

private:
    int64_t DueAt(int frame) const;

    const int64_t ticksPerSecond_;

    // The latest Unique Frame.
    bool havePrevious_ = false;
    int64_t previousArrival_ = 0;
    uint32_t previousFeedGeneration_ = 0;
    uint64_t previousSequence_ = 0;
    uint32_t previousWidth_ = 0, previousHeight_ = 0;
    Arrival arrival_;         // what UniqueFrameArrived answered, for Begin

    // The sequence in progress, or the last one.
    int generated_ = 0;
    int next_ = 1;            // the next frame to present; past generated_: all shown
    bool anchored_ = false;   // the first frame was presented
    int64_t firstPresent_ = 0;
    int64_t intervalTicks_ = 0;
    int64_t holdUntil_ = 0;   // HoldsRedraw after the sequence; 0: no hold

    TimedFrameStats stats_;
};

}  // namespace gp

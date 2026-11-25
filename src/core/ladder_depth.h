// How deep Target's ladder goes (ADR 0004): x4 (first quarter, middle, last
// quarter, the Unique Frame) or x8 (the eighths between them too, a third FRC
// stage). Chosen from the recent Game Rate (about the last second) and the
// rate in use: x8 when more than kEighthsAbove refreshes fall on each Unique
// Frame, x4 when fewer than kQuartersBelow; in between it keeps what it had,
// so a steady Game Rate never flips it. At 144 Hz: about 51 fps and below x8,
// about 58 fps and above x4.
//
// Also the rules of the third stage, beside those of the second in
// frame_schedule.h. Pure logic. Times are in the unit of Frame::arrivalQpc.
#pragma once

#include <cstdint>
#include <deque>
#include <optional>

namespace gp {

inline constexpr double kEighthsAbove = 2.8;   // refreshes per Unique Frame
inline constexpr double kQuartersBelow = 2.5;

// Measures the frames emitted by the Processor, before pending frames can be
// replaced. Repeated pictures removed by the active override never enter here.
class InputFrameRate {
public:
    explicit InputFrameRate(int64_t ticksPerSecond) : ticksPerSecond_(ticksPerSecond) {}
    // Arrival timestamps are in capture-clock ticks. A pause is excluded from
    // the one-second window. Returns 0 until half a second has been measured.
    double Arrived(int64_t arrivalTicks);
    void Reset();

private:
    struct Interval {
        int64_t endTicks;  // the arrival that ended it
        int64_t ticks;
    };
    int64_t ticksPerSecond_;
    std::deque<Interval> intervals_;  // the last second's
    int64_t measuredTicks_ = 0;       // their sum
    std::optional<int64_t> last_;     // the latest arrival
};

class LadderDepth {
public:
    explicit LadderDepth(int64_t ticksPerSecond) : ticksPerSecond_(ticksPerSecond) {}
    int Update(double inputRate, int64_t presentSlotTicks);
    int Depth() const { return depth_; }
    double GameRate() const { return gameRate_; }
    void Reset() { depth_ = 4; gameRate_ = 0; }

private:
    int64_t ticksPerSecond_;
    double gameRate_ = 0;
    int depth_ = 4;
};

// What the third FRC stage does with Unique Frame N. It is fed the quarter
// stream (N-1, first quarter, middle, last quarter, N) only while x8 is
// wanted. Generate: its history is whole and N's quarters are ready.
// Restart: it missed a frame of its stream (it was idle, N's quarters are
// missing, the earlier frame was forgotten): its history is cleared and
// starts again from N, and N uses the x4 ladder. Idle: x4 is wanted; it is
// not fed, so its history is lost.
enum class EighthStep { Generate, Restart, Idle };
constexpr EighthStep ThirdStageStep(bool wantEighths, bool havePrevious, int quartersReady, bool forgetEarlier) {
    if (!wantEighths) return EighthStep::Idle;
    return havePrevious && quartersReady == 3 && !forgetEarlier ? EighthStep::Generate : EighthStep::Restart;
}

// All or fall back: the x8 ladder (7 Generated Frames) only with all four
// eighths ready; otherwise what the quarters gave (3, 1 or 0).
constexpr int ReadyLadderFrames(int quartersReady, int eighthsReady) {
    return quartersReady == 3 && eighthsReady == 4 ? 7 : quartersReady;
}

// When a ladder's first frame is due on screen (TargetSchedule::Begin's
// startTicks). readyTicks: when it could first be displayed (its Generated
// Frames made, plus a refresh). presentSlotTicks: the time between presents
// at the rate in use. The ladder's frames are shown by the presents made
// until the next Unique Frame (about an interval after this one's
// arrivalTicks) is taken; their display times span an interval from a slot
// after the take. Counted from the arrival, not the take, so the steps stay
// evenly spaced from one ladder to the next, the start is at most:
//  - taken at once (x4, 2x): a slot and half a step after arrival, so those
//    display times fall on the ladder's frames one each, centred, and no
//    later than when 4x would show them (ready plus generation time is
//    later than this at 60 fps: frames would be held back longer than the
//    vsyncs that show them, and repeated);
//  - taken just after the next present (the x8 ladder: its seven FRC calls
//    then fit before the next vblank): a slot and 1.25 steps after arrival,
//    so the Unique Frame is due at the latest at the present that takes the
//    next one. A ladder's first eighth may be passed over instead.
int64_t LadderStartTicks(int64_t arrivalTicks, int64_t readyTicks, int64_t intervalTicks, int generated,
                         int64_t presentSlotTicks, bool takenAfterPresent);

}  // namespace gp

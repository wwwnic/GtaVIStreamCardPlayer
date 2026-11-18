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

namespace gp {

inline constexpr double kEighthsAbove = 2.8;   // refreshes per Unique Frame
inline constexpr double kQuartersBelow = 2.5;

class LadderDepth {
public:
    explicit LadderDepth(int64_t ticksPerSecond);

    // A Unique Frame arrived (forgetEarlier: the arrival rules forgot the
    // one before, a start, a pause, another size). refreshTicks: the time
    // between vsyncs. Answers the depth for this Unique Frame: 4 or 8. A
    // pause keeps the depth (x8 resumes at once) and measures the rate again;
    // until about half a second is measured, the depth does not change.
    int Arrived(int64_t arrivalTicks, bool forgetEarlier, int64_t refreshTicks);

    int Depth() const { return depth_; }
    // The recent Game Rate, 0 while not known.
    double GameRate() const;

    // Starts over at x4 (generation stopped).
    void Reset();

private:
    int64_t ticksPerSecond_;
    std::deque<int64_t> arrivals_;  // the last second's
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

}  // namespace gp

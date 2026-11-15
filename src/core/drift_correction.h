// Drift correction: the Capture Card and the output device run on different
// clocks, so the sound buffer slowly fills or drains. This holds its fill
// at a target by nudging the playback rate by a tiny, inaudible fraction: a
// correction in proportion to the fill error, plus a slow trim that learns
// the steady clock difference so the fill does not rest off the target.
#pragma once

namespace gp {

class DriftCorrection {
public:
    // Largest change to the playback rate: 0.1%, about 1.7 cents of pitch,
    // far below what anyone hears. Real clock drift is a few hundred parts
    // per million at most.
    static constexpr double kMaxCorrection = 0.001;

    // Starts over from this fill (seconds of sound in the buffer), for
    // example after playback (re)starts.
    void Reset(double fillSeconds);

    // Starts over from this fill but keeps the clock difference learnt: for
    // a restart between the same two clocks at another target (Audio Delay).
    void Resume(double fillSeconds);

    // One fill reading, `elapsedSeconds` after the previous one. Returns the
    // playback rate: 1 is normal, above 1 drains the buffer faster.
    // The target may change between calls (Audio Delay will raise it).
    double Update(double fillSeconds, double targetSeconds, double elapsedSeconds);

private:
    double smoothedFill_ = 0.0;
    double trim_ = 0.0;  // the steady clock difference learnt since Reset
    double rate_ = 1.0;
};

}  // namespace gp

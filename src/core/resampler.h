// Stereo resampler: turns sound at one rate into sound at another, and lets
// drift correction change the ratio by tiny amounts from one period to the
// next without a click. Four-point cubic (Catmull-Rom) interpolation.
#pragma once

#include <cstddef>
#include <vector>

namespace gp {

class Resampler {
public:
    Resampler() { Reset(); }

    // Forgets the stream so far (after a gap in the sound).
    void Reset();

    // How many input frames the next Process call needs to produce
    // `outFrames` frames when each output frame advances `step` input frames
    // (input rate / output rate, times the drift correction).
    size_t InputNeeded(size_t outFrames, double step) const;

    // Takes exactly InputNeeded(outFrames, step) frames (interleaved left,
    // right) and writes `outFrames` frames.
    void Process(const float* in, size_t inFrames, float* out, size_t outFrames, double step);

private:
    std::vector<float> pending_;  // input frames still needed, interleaved
    double position_ = 0.0;       // where the next output frame sits in pending_
};

}  // namespace gp

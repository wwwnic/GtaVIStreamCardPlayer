// Sound playout: everything between the capture side and the output side
// that is not Windows. Captured stereo frames go in at the Capture Card's
// rate; frames for the output device come out at its rate. In between: the
// sound ring, priming to the target fill, drift correction, resampling, and
// recovery from underruns and overruns. Safe to Push from one thread and
// Pull from another.
#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

#include "core/drift_correction.h"
#include "core/resampler.h"
#include "core/sound_ring.h"

namespace gp {

class SoundPlayout {
public:
    // The fill: sound left waiting in the buffer just after each output
    // period has taken its share. Its target is the one number to tune for
    // glitch-free sound at the lowest delay. It must outlast one late capture
    // packet (10 ms on the GC553 Pro), or the next period runs dry; 15 ms
    // leaves 5 ms of margin. (The prototype started playback with 10 ms but
    // had no drift correction and ran only seconds.) Audio Delay (ticket 07)
    // will add to this.
    static constexpr double kTargetFillSeconds = 0.015;

    // More than this above the target, counting the sound a period is about
    // to take, is an overrun: the buffer drops back to the target rather than
    // letting delay accumulate.
    static constexpr double kOverrunMarginSeconds = 0.040;

    // The longest target the buffer has room for (target plus Audio Delay).
    static constexpr double kLongestTargetSeconds = 1.0;

    SoundPlayout(uint32_t inputRate, uint32_t outputRate);

    // The output device changed. Playback re-primes at the new rate.
    void SetOutputRate(uint32_t outputRate);

    // Captured frames, interleaved left, right, at the input rate.
    void Push(const float* stereo, size_t frames);

    // Fills exactly `frames` output frames: sound when there is enough,
    // otherwise silence. Playback starts once the target fill would remain
    // after the period.
    void Pull(float* stereo, size_t frames);

    struct Stats {
        double fillAverageSeconds = 0.0;  // read after each Pull
        double fillMinSeconds = 0.0;
        double fillMaxSeconds = 0.0;
        double rate = 1.0;  // latest playback rate from drift correction
        unsigned underruns = 0;
        unsigned overruns = 0;
        uint64_t framesPushed = 0;
        uint64_t framesPulled = 0;
    };
    // Since the previous call.
    Stats TakeStats();

private:
    double TargetSeconds() const { return kTargetFillSeconds; }
    size_t TargetFrames() const;
    void RecordFill(double seconds);

    std::mutex mutex_;  // guards everything below
    const uint32_t inputRate_;
    uint32_t outputRate_;
    SoundRing ring_;
    Resampler resampler_;
    DriftCorrection correction_;
    bool playing_ = false;
    std::vector<float> scratch_;
    Stats stats_;
    double fillSum_ = 0.0;
    uint64_t fillReadings_ = 0;
};

}  // namespace gp

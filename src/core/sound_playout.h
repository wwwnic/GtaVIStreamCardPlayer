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
    // leaves 5 ms of margin. That holds while the output takes 10 ms periods
    // in step with the packets; with the card's clock the slower one, or
    // with shorter output periods, a packet up to three quarters late is
    // still covered, a whole one is not (see the late-packet tests). (The
    // prototype started playback with 10 ms but
    // had no drift correction and ran only seconds.) The Audio Delay adds to
    // this.
    static constexpr double kTargetFillSeconds = 0.015;

    // More than this above the target, counting the sound a period is about
    // to take, is an overrun: the buffer drops back to the target rather than
    // letting delay accumulate. (An output that takes more than about 15 ms
    // at a time gets a wider margin, from the size of its periods: twice
    // what a period takes, plus a quarter of this for a capture packet.)
    static constexpr double kOverrunMarginSeconds = 0.040;

    // The longest target the buffer has room for (target plus Audio Delay).
    static constexpr double kLongestTargetSeconds = 1.0;

    SoundPlayout(uint32_t inputRate, uint32_t outputRate);

    // The output device changed. Playback re-primes at the new rate.
    void SetOutputRate(uint32_t outputRate);

    // Sample gain (volume; 0 is mute). Any thread. A change fades over one
    // output period, so it never clicks.
    void SetGain(float gain);

    // Audio Delay: this much more sound is held in the buffer, so it plays
    // later. Any thread. While playing, a change fades the sound out over one
    // period, then playback re-primes at the new target (quiet while a longer
    // fill gathers, extra sound dropped for a shorter one) and fades back in.
    // Drift correction then holds the fill at the new target, keeping the
    // clock difference it has learnt.
    void SetAudioDelay(double seconds);

    // The fill the buffer holds (or is moving to): base target plus Audio Delay.
    double TargetFillSeconds();

    // Captured frames, interleaved left, right, at the input rate.
    void Push(const float* stereo, size_t frames);

    // Fills exactly `frames` output frames: sound when there is enough,
    // otherwise silence. Playback starts once the target fill would remain
    // after the period; older sound beyond that is dropped at the start.
    // Each start (the first, after an underrun, an Audio Delay change or a
    // new output rate) fades in from silence over its first period.
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
    double TargetSeconds() const { return kTargetFillSeconds + delaySeconds_; }
    size_t TargetFrames() const;
    void RecordFill(double seconds);

    std::mutex mutex_;  // guards everything below
    const uint32_t inputRate_;
    uint32_t outputRate_;
    SoundRing ring_;
    Resampler resampler_;
    DriftCorrection correction_;
    bool playing_ = false;
    size_t periodNeed_ = 0;  // input frames the latest Pull asked for
    std::vector<float> scratch_;
    Stats stats_;
    float gain_ = 1.0f;         // wanted
    float appliedGain_ = 0.0f;  // reached at the end of the previous period; 0 before each start
    double delaySeconds_ = 0.0;         // the Audio Delay the target uses now
    double pendingDelaySeconds_ = 0.0;  // wanted; applied when playback re-primes
    bool retarget_ = false;             // fade out and re-prime at the next Pull
    bool keepDrift_ = false;            // that re-prime keeps the learnt clock difference
    double fillSum_ = 0.0;
    uint64_t fillReadings_ = 0;
};

}  // namespace gp

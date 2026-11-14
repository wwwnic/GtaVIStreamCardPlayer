#include "core/sound_playout.h"

#include <algorithm>
#include <cmath>

namespace gp {

SoundPlayout::SoundPlayout(uint32_t inputRate, uint32_t outputRate)
    : inputRate_(inputRate),
      outputRate_(outputRate),
      ring_(static_cast<size_t>((kLongestTargetSeconds + kOverrunMarginSeconds) * inputRate)) {}

void SoundPlayout::SetOutputRate(uint32_t outputRate) {
    std::lock_guard lock(mutex_);
    outputRate_ = outputRate;
    playing_ = false;
}

namespace {

// Scales the period by a gain going in a straight line from `from` to `to`,
// reaching `to` on the last frame.
void ApplyGain(float* stereo, size_t frames, float from, float to) {
    if (from == 1.0f && to == 1.0f) return;
    for (size_t i = 0; i < frames; i++) {
        const float gain = from + (to - from) * static_cast<float>(i + 1) / static_cast<float>(frames);
        stereo[2 * i] *= gain;
        stereo[2 * i + 1] *= gain;
    }
}

}  // namespace

void SoundPlayout::SetGain(float gain) {
    std::lock_guard lock(mutex_);
    gain_ = std::max(gain, 0.0f);
}

void SoundPlayout::SetAudioDelay(double seconds) {
    std::lock_guard lock(mutex_);
    pendingDelaySeconds_ = std::clamp(seconds, 0.0, kLongestTargetSeconds - kTargetFillSeconds);
    if (!playing_) {
        delaySeconds_ = pendingDelaySeconds_;  // priming uses it straight away
    } else {
        retarget_ = pendingDelaySeconds_ != delaySeconds_;
    }
}

double SoundPlayout::TargetFillSeconds() {
    std::lock_guard lock(mutex_);
    return kTargetFillSeconds + pendingDelaySeconds_;
}

void SoundPlayout::Push(const float* stereo, size_t frames) {
    std::lock_guard lock(mutex_);
    stats_.framesPushed += frames;
    const size_t limit = TargetFrames() + static_cast<size_t>(kOverrunMarginSeconds * inputRate_);
    if (ring_.Fill() + frames > limit) {
        // Overrun: drop the oldest sound so the fill is back at the target.
        stats_.overruns++;
        const size_t target = TargetFrames();
        if (frames >= target) {
            ring_.Clear();
            stereo += 2 * (frames - target);
            frames = target;
        } else {
            ring_.Discard(ring_.Fill() + frames - target);
        }
        correction_.Reset(static_cast<double>(target) / inputRate_);
    }
    ring_.Write(stereo, frames);
}

void SoundPlayout::Pull(float* stereo, size_t frames) {
    std::fill(stereo, stereo + 2 * frames, 0.0f);
    if (frames == 0) return;
    std::lock_guard lock(mutex_);
    stats_.framesPulled += frames;
    const double nominalStep = static_cast<double>(inputRate_) / outputRate_;
    if (!playing_) {
        delaySeconds_ = pendingDelaySeconds_;
        retarget_ = false;
        resampler_.Reset();
        const size_t start = TargetFrames() + resampler_.InputNeeded(frames, nominalStep);
        if (ring_.Fill() < start) {
            RecordFill(static_cast<double>(ring_.Fill()) / inputRate_);
            return;  // still priming
        }
        ring_.Discard(ring_.Fill() - start);  // start at the target, not with extra delay
        playing_ = true;
        correction_.Reset(TargetSeconds());
        stats_.rate = 1.0;
    }
    const double step = nominalStep * stats_.rate;
    const size_t needed = resampler_.InputNeeded(frames, step);
    if (needed > ring_.Fill()) {
        // Underrun: play silence and wait for the target fill again, so the
        // sound resumes with its normal margin.
        stats_.underruns++;
        playing_ = false;
        RecordFill(static_cast<double>(ring_.Fill()) / inputRate_);
        return;
    }
    scratch_.resize(2 * needed);
    ring_.Read(scratch_.data(), needed);
    resampler_.Process(scratch_.data(), needed, stereo, frames, step);
    if (retarget_) {
        // Audio Delay changed: fade this period out, drop any sound beyond
        // the new target, and re-prime; the restart fades in from silence.
        ApplyGain(stereo, frames, appliedGain_, 0.0f);
        appliedGain_ = 0.0f;
        delaySeconds_ = pendingDelaySeconds_;
        retarget_ = false;
        playing_ = false;
        if (ring_.Fill() > TargetFrames()) ring_.Discard(ring_.Fill() - TargetFrames());
        RecordFill(static_cast<double>(ring_.Fill()) / inputRate_);
        return;
    }
    ApplyGain(stereo, frames, appliedGain_, gain_);
    appliedGain_ = gain_;
    const double fill = static_cast<double>(ring_.Fill()) / inputRate_;
    RecordFill(fill);
    stats_.rate = correction_.Update(fill, TargetSeconds(), static_cast<double>(frames) / outputRate_);
}

SoundPlayout::Stats SoundPlayout::TakeStats() {
    std::lock_guard lock(mutex_);
    Stats taken = stats_;
    taken.fillAverageSeconds = fillReadings_ ? fillSum_ / static_cast<double>(fillReadings_) : 0.0;
    stats_ = Stats{};
    stats_.rate = taken.rate;
    fillSum_ = 0.0;
    fillReadings_ = 0;
    return taken;
}

size_t SoundPlayout::TargetFrames() const {
    return static_cast<size_t>(std::lround(TargetSeconds() * inputRate_));
}

void SoundPlayout::RecordFill(double seconds) {
    if (fillReadings_ == 0) {
        stats_.fillMinSeconds = stats_.fillMaxSeconds = seconds;
    } else {
        stats_.fillMinSeconds = std::min(stats_.fillMinSeconds, seconds);
        stats_.fillMaxSeconds = std::max(stats_.fillMaxSeconds, seconds);
    }
    fillSum_ += seconds;
    fillReadings_++;
}

}  // namespace gp

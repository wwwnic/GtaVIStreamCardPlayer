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
        resampler_.Reset();
        if (ring_.Fill() < TargetFrames() + resampler_.InputNeeded(frames, nominalStep)) {
            RecordFill(static_cast<double>(ring_.Fill()) / inputRate_);
            return;  // still priming
        }
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

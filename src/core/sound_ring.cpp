#include "core/sound_ring.h"

#include <algorithm>

namespace gp {

SoundRing::SoundRing(size_t capacityFrames) : samples_(2 * capacityFrames), capacity_(capacityFrames) {}

size_t SoundRing::Write(const float* stereo, size_t frames) {
    if (capacity_ == 0) return frames;
    size_t dropped = 0;
    if (frames > capacity_) {
        // Only the newest frames can be kept.
        dropped = frames - capacity_;
        stereo += 2 * dropped;
        frames = capacity_;
    }
    const size_t room = capacity_ - fill_;
    if (frames > room) {
        dropped += frames - room;
        Discard(frames - room);
    }
    size_t end = (start_ + fill_) % capacity_;
    for (size_t done = 0; done < frames;) {
        const size_t run = std::min(frames - done, capacity_ - end);
        std::copy_n(stereo + 2 * done, 2 * run, samples_.begin() + 2 * end);
        done += run;
        end = (end + run) % capacity_;
    }
    fill_ += frames;
    return dropped;
}

size_t SoundRing::Read(float* stereo, size_t frames) {
    frames = std::min(frames, fill_);
    for (size_t done = 0; done < frames;) {
        const size_t run = std::min(frames - done, capacity_ - start_);
        std::copy_n(samples_.begin() + 2 * start_, 2 * run, stereo + 2 * done);
        done += run;
        start_ = (start_ + run) % capacity_;
    }
    fill_ -= frames;
    return frames;
}

void SoundRing::Discard(size_t frames) {
    frames = std::min(frames, fill_);
    if (capacity_) start_ = (start_ + frames) % capacity_;
    fill_ -= frames;
}

}  // namespace gp

// The sound ring: a fixed-size queue of stereo float frames between the
// capture side and the playback side. Not thread-safe by itself.
#pragma once

#include <cstddef>
#include <vector>

namespace gp {

class SoundRing {
public:
    explicit SoundRing(size_t capacityFrames);

    size_t Capacity() const { return capacity_; }
    size_t Fill() const { return fill_; }

    // Appends frames (interleaved left, right). When they do not all fit, the
    // oldest frames are dropped to make room; returns how many were dropped.
    size_t Write(const float* stereo, size_t frames);

    // Takes up to `frames` of the oldest frames; returns how many it took
    // (fewer than asked when the ring runs dry).
    size_t Read(float* stereo, size_t frames);

    // Drops up to `frames` of the oldest frames.
    void Discard(size_t frames);

    void Clear() { Discard(fill_); }

private:
    std::vector<float> samples_;
    size_t capacity_;
    size_t start_ = 0;  // frame index of the oldest frame
    size_t fill_ = 0;
};

}  // namespace gp

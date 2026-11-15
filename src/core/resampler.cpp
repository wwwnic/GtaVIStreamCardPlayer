#include "core/resampler.h"

#include <algorithm>
#include <cmath>

namespace gp {

// pending_ always keeps one frame before the current position, because the
// interpolation between frames i and i+1 also looks at i-1 and i+2.
void Resampler::Reset() {
    pending_.assign(2, 0.0f);  // silence before the stream starts
    position_ = 1.0;
}

size_t Resampler::InputNeeded(size_t outFrames, double step) const {
    if (outFrames == 0) return 0;
    const double last = position_ + static_cast<double>(outFrames - 1) * step;
    // The last output frame's four points, and (for a step above 4, such as
    // 48 kHz to 8 kHz) the frames skipped on the way to the next period, so
    // that one frame before the next position is always kept.
    const double next = position_ + static_cast<double>(outFrames) * step;
    const size_t required = std::max(static_cast<size_t>(std::floor(last)) + 3, static_cast<size_t>(std::floor(next)));
    const size_t have = pending_.size() / 2;
    return required > have ? required - have : 0;
}

void Resampler::Process(const float* in, size_t inFrames, float* out, size_t outFrames, double step) {
    const size_t needed = InputNeeded(outFrames, step);
    pending_.insert(pending_.end(), in, in + 2 * inFrames);
    if (inFrames < needed) pending_.resize(pending_.size() + 2 * (needed - inFrames), 0.0f);  // caller short: pad silence

    for (size_t k = 0; k < outFrames; k++) {
        const double p = position_ + static_cast<double>(k) * step;
        const size_t i = static_cast<size_t>(p);
        const float t = static_cast<float>(p - static_cast<double>(i));
        for (size_t channel = 0; channel < 2; channel++) {
            const float y0 = pending_[2 * (i - 1) + channel];
            const float y1 = pending_[2 * i + channel];
            const float y2 = pending_[2 * (i + 1) + channel];
            const float y3 = pending_[2 * (i + 2) + channel];
            out[2 * k + channel] =
                y1 + 0.5f * t * (y2 - y0 + t * (2.0f * y0 - 5.0f * y1 + 4.0f * y2 - y3 + t * (3.0f * (y1 - y2) + y3 - y0)));
        }
    }

    position_ += static_cast<double>(outFrames) * step;
    const size_t used = static_cast<size_t>(position_) - 1;
    pending_.erase(pending_.begin(), pending_.begin() + 2 * used);
    position_ -= static_cast<double>(used);
}

}  // namespace gp

// Actual Presenter state, shared with the counter and Help > About.
#pragma once

#include <cstdint>
#include <deque>

namespace gp {

struct PresentationInfo {
    int runningFactor = 0;  // 0 while off, starting or failed
    int shownRate = 0;
    bool sharpUpscale = false;
    uint32_t feedWidth = 0, feedHeight = 0;
    int shownWidth = 0, shownHeight = 0;
};

// Successful presents only, including repaint and Generated Frames. No refresh cap.
// Caller supplies monotonic milliseconds and owns thread synchronisation.
class PresentedFrames {
public:
    void Presented(int64_t now) {
        Prune(now);
        times_.push_back(now);
    }
    int RateAt(int64_t now) {
        Prune(now);
        return static_cast<int>((times_.size() + 1) / 2);
    }
    void Reset() { times_.clear(); }
private:
    void Prune(int64_t now) {
        while (!times_.empty() && times_.front() <= now - 2000) times_.pop_front();
    }
    std::deque<int64_t> times_;
};

}  // namespace gp

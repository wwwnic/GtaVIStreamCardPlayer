// Actual Presenter state, shared with the counter and Help > About.
#pragma once

#include <algorithm>
#include <cstdint>
#include <deque>
#include <optional>

namespace gp {

struct PresentationInfo {
    int runningFactor = 0;  // 0 while off, starting or failed
    int shownRate = 0;
    bool sharpUpscale = false;
    uint32_t feedWidth = 0, feedHeight = 0;
    int shownWidth = 0, shownHeight = 0;
    int realRate = 0; // successful real endpoint presents, excluding generated frames/repaints
    int targetRate = 0;  // Target running: the rate in use (the monitor's refresh); 0 otherwise
};

// Successful presents only, including repaint and Generated Frames. No refresh cap.
// Caller supplies monotonic milliseconds and owns thread synchronisation.
class PresentedFrames {
public:
    static constexpr int64_t kWindowMs = 2000;
    static constexpr int64_t kShortestWindowMs = 250;

    void Presented(int64_t now) {
        Prune(now);
        times_.push_back(now);
    }
    // Presents per second over the last two seconds. Right after Reset the
    // window is only as long as the time since, so a new present rate (the
    // factor changed) is not mixed with the old one.
    int RateAt(int64_t now) {
        Prune(now);
        int64_t span = kWindowMs;
        if (since_ && now - *since_ < kWindowMs) span = std::max(now - *since_, kShortestWindowMs);
        const int64_t count = static_cast<int64_t>(times_.size());
        return static_cast<int>((count * 1000 + span / 2) / span);
    }
    // Forgets every present so far; the window restarts at now.
    void Reset(int64_t now) {
        times_.clear();
        since_ = now;
    }
private:
    void Prune(int64_t now) {
        while (!times_.empty() && times_.front() <= now - kWindowMs) times_.pop_front();
    }
    std::deque<int64_t> times_;
    std::optional<int64_t> since_;
};

}  // namespace gp

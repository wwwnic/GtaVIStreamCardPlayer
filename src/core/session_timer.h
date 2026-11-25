#pragma once

#include <cstdint>
#include <optional>

namespace gp {

// Uses monotonic milliseconds supplied by the caller. Paused time is excluded.
class SessionTimer {
public:
    explicit SessionTimer(uint64_t now) : startedMs_(now) {}

    uint64_t ElapsedMs(uint64_t now) const { return pausedAt_.value_or(now) - startedMs_; }
    bool Paused() const { return pausedAt_.has_value(); }

    void TogglePause(uint64_t now) {
        if (pausedAt_) {
            startedMs_ += now - *pausedAt_;
            pausedAt_.reset();
        } else {
            pausedAt_ = now;
        }
    }

private:
    uint64_t startedMs_;
    std::optional<uint64_t> pausedAt_;
};

}  // namespace gp

// Hand-off between threads that never queues: holds at most one value, and a
// newer value replaces an older one that was not taken yet.
#pragma once

#include <mutex>
#include <optional>
#include <utility>

namespace gp {

template <typename T>
class LatestSlot {
public:
    // Stores value. Returns true if it replaced a value nobody took.
    bool Put(T value) {
        std::lock_guard lock(mutex_);
        const bool replaced = value_.has_value();
        value_ = std::move(value);
        return replaced;
    }

    // Removes and returns the held value, or empty if there is none.
    std::optional<T> Take() {
        std::lock_guard lock(mutex_);
        std::optional<T> taken = std::move(value_);
        value_.reset();
        return taken;
    }

private:
    std::mutex mutex_;
    std::optional<T> value_;
};

}  // namespace gp

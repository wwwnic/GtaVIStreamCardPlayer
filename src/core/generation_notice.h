// Owned driver outcomes delivered from the presenting thread to the window.
#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace gp {

// FallbackToQuadruple: Target's third FRC failed; Target stays on the x4 ladder.
enum class GenerationOutcome { Failed, FallbackToDouble, FallbackToQuadruple };
struct GenerationNotice {
    GenerationOutcome outcome;
    std::string reason;
};
struct RequestedGenerationNotice {
    uint64_t request;
    GenerationNotice notice;
};
// The wake-up message carries no pointer. This mailbox owns every reason even
// when posting fails, a request becomes stale or the window is destroyed.
class GenerationNotices {
public:
    void Publish(uint64_t request, GenerationNotice notice) {
        std::lock_guard lock(mutex_);
        pending_.push_back({request, std::move(notice)});
    }
    std::vector<RequestedGenerationNotice> Take() {
        std::lock_guard lock(mutex_);
        return std::exchange(pending_, {});
    }
private:
    std::mutex mutex_;
    std::vector<RequestedGenerationNotice> pending_;
};

}  // namespace gp

#include "core/frame.h"

#include <mutex>
#include <utility>

namespace gp {

namespace {
// Enough for the frames in flight plus the few a Processor may keep.
constexpr size_t kMaxIdleFrames = 8;
}  // namespace

struct FramePool::Shared {
    std::mutex mutex;
    std::vector<std::unique_ptr<Frame>> idle;
};

size_t FrameByteSize(PixelFormat format, uint32_t width, uint32_t height) {
    const size_t pixels = static_cast<size_t>(width) * height;
    return format == PixelFormat::YUY2 ? pixels * 2 : pixels + pixels / 2;
}

FramePool::FramePool() : shared_(std::make_shared<Shared>()) {}

std::shared_ptr<Frame> FramePool::Acquire(size_t byteSize) {
    std::unique_ptr<Frame> frame;
    {
        std::lock_guard lock(shared_->mutex);
        if (!shared_->idle.empty()) {
            frame = std::move(shared_->idle.back());
            shared_->idle.pop_back();
        }
    }
    if (!frame) frame = std::make_unique<Frame>();

    // Reset everything but keep the pixel buffer's memory.
    std::vector<uint8_t> pixels = std::move(frame->pixels);
    *frame = Frame{};
    pixels.resize(byteSize);
    frame->pixels = std::move(pixels);

    // When the last holder lets go, the frame returns to the pool if the pool
    // still exists.
    std::weak_ptr<Shared> pool = shared_;
    return std::shared_ptr<Frame>(frame.release(), [pool](Frame* released) {
        std::unique_ptr<Frame> owned(released);
        if (std::shared_ptr<Shared> alive = pool.lock()) {
            std::lock_guard lock(alive->mutex);
            if (alive->idle.size() < kMaxIdleFrames) alive->idle.push_back(std::move(owned));
        }
    });
}

}  // namespace gp

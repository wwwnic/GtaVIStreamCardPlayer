#include "doctest.h"

#include <memory>

#include "core/frame.h"

using gp::Frame;
using gp::FrameByteSize;
using gp::FramePool;
using gp::PixelFormat;

TEST_CASE("a 1080p YUY2 frame is 2 bytes per pixel") {
    CHECK(FrameByteSize(PixelFormat::YUY2, 1920, 1080) == 4147200);
}

TEST_CASE("a 1080p NV12 frame is 1.5 bytes per pixel") {
    CHECK(FrameByteSize(PixelFormat::NV12, 1920, 1080) == 3110400);
}

TEST_CASE("the pool hands out a frame with a pixel buffer of the requested size") {
    FramePool pool;
    std::shared_ptr<Frame> frame = pool.Acquire(1000);
    REQUIRE(frame.get() != nullptr);
    CHECK(frame->pixels.size() == 1000);
}

TEST_CASE("the pool reuses the pixel buffer of a frame nobody holds any more") {
    FramePool pool;
    std::shared_ptr<Frame> first = pool.Acquire(1000);
    const uint8_t* firstPixels = first->pixels.data();
    first.reset();
    std::shared_ptr<Frame> second = pool.Acquire(1000);
    CHECK(second->pixels.data() == firstPixels);
}

TEST_CASE("the pool never hands out a buffer that is still held") {
    FramePool pool;
    std::shared_ptr<Frame> first = pool.Acquire(1000);
    std::shared_ptr<Frame> second = pool.Acquire(1000);
    CHECK(second->pixels.data() != first->pixels.data());
}

TEST_CASE("a reused frame starts with its details reset") {
    FramePool pool;
    std::shared_ptr<Frame> first = pool.Acquire(1000);
    first->sequence = 42;
    first->arrivalQpc = 99;
    first.reset();
    std::shared_ptr<Frame> second = pool.Acquire(500);
    CHECK(second->sequence == 0);
    CHECK(second->arrivalQpc == 0);
    CHECK(second->pixels.size() == 500);
}

TEST_CASE("a frame stays usable after its pool is gone") {
    std::shared_ptr<Frame> frame;
    {
        FramePool pool;
        frame = pool.Acquire(16);
    }
    frame->pixels[15] = 5;
    CHECK(frame->pixels[15] == 5);
    frame.reset();  // must not touch the dead pool
}

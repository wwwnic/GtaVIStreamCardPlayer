#include "doctest.h"

#include "core/luma_summary.h"

using gp::Frame;
using gp::LumaSummary;
using gp::PixelFormat;
using gp::SummarizeLuma;

namespace {

Frame Yuy2(uint32_t width, uint32_t height, uint8_t y) {
    Frame frame;
    frame.width = width;
    frame.height = height;
    frame.format = PixelFormat::YUY2;
    frame.pixels.assign(gp::FrameByteSize(PixelFormat::YUY2, width, height), 128);
    for (size_t i = 0; i < frame.pixels.size(); i += 2) frame.pixels[i] = y;
    return frame;
}

}  // namespace

TEST_CASE("a uniform picture has equal minimum and maximum luma") {
    const LumaSummary summary = SummarizeLuma(Yuy2(64, 36, 16));
    CHECK(summary.min == 16);
    CHECK(summary.max == 16);
    CHECK(summary.mean == 16);
}

TEST_CASE("chroma bytes of a YUY2 frame are not taken for luma") {
    const LumaSummary summary = SummarizeLuma(Yuy2(64, 36, 200));
    CHECK(summary.max == 200);
    CHECK(summary.min == 200);
}

TEST_CASE("a picture with a bright half shows the spread") {
    Frame frame = Yuy2(64, 36, 16);
    for (uint32_t row = 0; row < 18; row++) {
        for (uint32_t x = 0; x < 64; x++) frame.pixels[row * 128 + x * 2] = 235;
    }
    const LumaSummary summary = SummarizeLuma(frame);
    CHECK(summary.min == 16);
    CHECK(summary.max == 235);
    CHECK(summary.mean > 100);
    CHECK(summary.mean < 150);
}

TEST_CASE("NV12 luma is read from the Y plane only") {
    Frame frame;
    frame.width = 64;
    frame.height = 36;
    frame.format = PixelFormat::NV12;
    frame.pixels.assign(gp::FrameByteSize(PixelFormat::NV12, 64, 36), 90);
    for (size_t i = 64 * 36; i < frame.pixels.size(); i++) frame.pixels[i] = 255;
    const LumaSummary summary = SummarizeLuma(frame);
    CHECK(summary.min == 90);
    CHECK(summary.max == 90);
}

TEST_CASE("an empty or short frame gives no summary") {
    Frame frame;
    CHECK(SummarizeLuma(frame).mean == -1);
    frame.width = 64;
    frame.height = 36;
    frame.pixels.resize(10);
    CHECK(SummarizeLuma(frame).mean == -1);
}

#include "doctest.h"

#include <memory>
#include <vector>

#include "core/processor.h"

using gp::Frame;
using gp::FramePtr;
using gp::TimedFrame;

namespace {

FramePtr FrameArrivingAt(int64_t qpc) {
    auto frame = std::make_shared<Frame>();
    frame->arrivalQpc = qpc;
    return frame;
}

}  // namespace

TEST_CASE("the pass-through Processor emits each captured frame once, unchanged") {
    auto processor = gp::MakePassThroughProcessor();
    FramePtr captured = FrameArrivingAt(1000);
    std::vector<TimedFrame> out;
    processor->Process(captured, out);
    REQUIRE(out.size() == 1);
    CHECK(out[0].frame.get() == captured.get());
}

TEST_CASE("the pass-through Processor asks for frames to be shown as soon as possible") {
    auto processor = gp::MakePassThroughProcessor();
    std::vector<TimedFrame> out;
    processor->Process(FrameArrivingAt(1000), out);
    REQUIRE(out.size() == 1);
    // A show time no later than arrival can never be in the future.
    CHECK(out[0].showAtQpc <= 1000);
}

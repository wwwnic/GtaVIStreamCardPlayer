#include "core/processor.h"

namespace gp {

namespace {

class PassThroughProcessor final : public Processor {
public:
    void Process(FramePtr captured, std::vector<TimedFrame>& out) override {
        const int64_t arrival = captured->arrivalQpc;
        out.push_back({std::move(captured), arrival});
    }
};

}  // namespace

std::unique_ptr<Processor> MakePassThroughProcessor() {
    return std::make_unique<PassThroughProcessor>();
}

}  // namespace gp

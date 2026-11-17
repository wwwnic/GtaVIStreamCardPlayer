#include "platform/frame_generator.h"

#include "core/frame_schedule.h"

namespace gp {

namespace {

class RepeatingGenerator final : public FrameGenerator {
public:
    explicit RepeatingGenerator(int timesShown) : generated_(GeneratedFramesFor(timesShown)) {}

    int Generate(const Input& frame) override {
        pictureView_ = frame.pictureView;
        return frame.mayGenerate ? generated_ : 0;
    }
    // The Unique Frame's own picture, each time.
    ID3D11ShaderResourceView* Generated(int) override { return pictureView_; }
    void ReleaseDevice() override { pictureView_ = nullptr; }

private:
    const int generated_;
    ID3D11ShaderResourceView* pictureView_ = nullptr;  // the Presenter's; not kept past the next Generate
};

}  // namespace

std::shared_ptr<FrameGenerator> MakeRepeatingGenerator(int timesShown) {
    return std::make_shared<RepeatingGenerator>(timesShown);
}

}  // namespace gp

#pragma once

#include <functional>
#include <memory>

namespace gp {

class FrameGenerator;
bool AmfRuntimeInstalled();
// On the presenting thread: true when 4x fell back to 2x, false when generation failed.
std::shared_ptr<FrameGenerator> MakeAmfGenerator(bool four, std::function<void(bool)> onFailure);

}  // namespace gp

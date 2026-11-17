#pragma once

#include <functional>
#include <memory>

namespace gp {

class FrameGenerator;
bool AmfRuntimeInstalled();
// The callback runs on the presenting thread, only when returning kCannotBeUsed.
std::shared_ptr<FrameGenerator> MakeAmfGenerator(std::function<void()> onFailure);

}  // namespace gp

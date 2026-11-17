#pragma once

#include <functional>
#include <memory>

#include "core/generation_notice.h"

namespace gp {

class FrameGenerator;
bool AmfRuntimeInstalled();
// On the presenting thread: an owned, typed failure or fallback with the driver reason.
std::shared_ptr<FrameGenerator> MakeAmfGenerator(bool four, std::function<void(GenerationNotice)> onFailure);

}  // namespace gp

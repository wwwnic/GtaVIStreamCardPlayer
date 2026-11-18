#pragma once

#include <functional>
#include <memory>

#include "core/generation_notice.h"

namespace gp {

class FrameGenerator;
bool AmfRuntimeInstalled();
// On the presenting thread: an owned, typed failure or fallback with the driver reason.
// eighths (Target only, with four): also a third FRC for the x8 ladder, fed
// only while Input::eighths asks for it.
std::shared_ptr<FrameGenerator> MakeAmfGenerator(bool four, std::function<void(GenerationNotice)> onFailure,
                                                 bool eighths = false);

}  // namespace gp

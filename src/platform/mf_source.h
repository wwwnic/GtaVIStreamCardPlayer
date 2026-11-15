// The Media Foundation implementation of Source.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "core/source.h"

namespace gp {

// Captures through the Media Foundation Source Reader with converters
// disabled, so frames arrive in the Capture Card's own uncompressed format.
// Requires COM to be initialised on the calling thread.
std::unique_ptr<Source> MakeMediaFoundationSource();

// The video capture devices present now, name and identifier (for the menu).
// Needs a Source to exist (Media Foundation started) and COM on the calling thread.
std::vector<CaptureCardEntry> ListCaptureDevices();

}  // namespace gp

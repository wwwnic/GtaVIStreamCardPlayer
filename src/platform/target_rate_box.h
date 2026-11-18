// View > Target rate > Custom (ticket 04): the small modal box where the
// user types a Target Rate. Plain Win32, a dialog built in memory (no
// resource file). Title "Target rate", one number field pre-filled, OK and
// Cancel; Enter accepts, Esc cancels. What is not a whole number 1..1000
// (core/target_rate.h, TargetRateTyped) disables OK and shows "1 to 1000".
// Owned by the Viewer's window, so it shows above it in fullscreen too; the
// Presenter keeps presenting on its own thread meanwhile.
// Window's thread only.
#pragma once

#include <windows.h>

#include <optional>

namespace gp {

// Blocks until OK or Cancel. The number after OK; nothing after Cancel.
std::optional<int> AskTargetRate(HWND owner, int start);

}  // namespace gp

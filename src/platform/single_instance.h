// One GamePlayer at a time: the Capture Card can be held by one program
// only, so a second copy could never show anything.
#pragma once

#include <windows.h>

namespace gp {

// True when no other GamePlayer runs; this process then holds the claim
// until it exits. Otherwise brings the running one's window forward
// (restored if minimised) and returns false: the caller should exit.
bool ClaimSingleInstance(const wchar_t* windowClass);

// Posted to the running window when another launch brought it forward.
UINT AnotherLaunchMessage();

}  // namespace gp

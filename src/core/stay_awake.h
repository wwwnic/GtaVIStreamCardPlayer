// When to keep the PC and display awake: while a Feed plays, because
// controller input on the Console is not PC activity. Released a few quiet
// seconds after frames stop, so a brief gap does not flap the request.
#pragma once

#include <cstdint>
#include <string>

namespace gp {

class StayAwake {
public:
    static constexpr int kReleaseAfterQuietSeconds = 3;
    enum class Change { None, Take, Release };

    // Call once a second: did any frame arrive during that second?
    Change Second(bool framesArrived);
    bool Held() const { return held_; }

private:
    bool held_ = false;
    int quietSeconds_ = 0;
};

// Windows' system-wide execution state flags (EXECUTION_STATE) as words, for
// the log: "SYSTEM|DISPLAY", or "none".
std::string ExecutionStateText(uint32_t state);

// The display's state as Windows reports it (GUID_CONSOLE_DISPLAY_STATE:
// 0 off, 1 on, 2 dimmed), as a log line. Off or dimmed while the hold is
// held is what the hold exists to prevent, so the line says so.
std::string DisplayStateLine(uint32_t displayState, bool held);

}  // namespace gp

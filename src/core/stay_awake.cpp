#include "core/stay_awake.h"

#include <cstdio>

namespace gp {

StayAwake::Change StayAwake::Second(bool framesArrived) {
    if (framesArrived) {
        quietSeconds_ = 0;
        if (held_) return Change::None;
        held_ = true;
        return Change::Take;
    }
    if (!held_) return Change::None;
    if (++quietSeconds_ < kReleaseAfterQuietSeconds) return Change::None;
    held_ = false;
    quietSeconds_ = 0;
    return Change::Release;
}

std::string ExecutionStateText(uint32_t state) {
    static const struct { uint32_t flag; const char* name; } kFlags[] = {
        {0x00000001u, "SYSTEM"}, {0x00000002u, "DISPLAY"}, {0x00000004u, "USER_PRESENT"},
        {0x00000040u, "AWAYMODE"}, {0x80000000u, "CONTINUOUS"},
    };
    std::string text;
    uint32_t rest = state;
    for (const auto& f : kFlags) {
        if (!(state & f.flag)) continue;
        if (!text.empty()) text += '|';
        text += f.name;
        rest &= ~f.flag;
    }
    if (rest) {
        char hex[16];
        snprintf(hex, sizeof hex, "0x%X", rest);
        if (!text.empty()) text += '|';
        text += hex;
    }
    return text.empty() ? "none" : text;
}

std::string DisplayStateLine(uint32_t displayState, bool held) {
    const char* name = displayState == 0 ? "off" : displayState == 1 ? "on" : displayState == 2 ? "dimmed" : nullptr;
    if (!name) return "Display: state " + std::to_string(displayState);
    std::string line = std::string("Display: ") + name;
    if (held && displayState != 1) line += " while stay awake is held (should not happen)";
    return line;
}

}  // namespace gp

// Night mode: lifts the dark parts of the picture so a night scene or a
// dark room reads better. Bright parts (peak above kNightModeKnee) are left
// exactly as they were. The curve itself runs in the Presenter's shaders
// (platform/shadow_curve.h); this is the choice and its strength.
#pragma once

#include <optional>
#include <string_view>

namespace gp {

enum class NightMode { Off, Low, High };

// Above this peak brightness (display-encoded, 0..1) nothing changes.
inline constexpr float kNightModeKnee = 0.65f;

// How far dark values move toward their square root: 0 none, 1 all the way.
inline float NightModeStrength(NightMode mode) {
    switch (mode) {
    case NightMode::Low: return 0.45f;
    case NightMode::High: return 1.0f;
    case NightMode::Off: break;
    }
    return 0.0f;
}

// The N key and the menu item: off, low, high, off again.
inline NightMode NextNightMode(NightMode mode) {
    switch (mode) {
    case NightMode::Off: return NightMode::Low;
    case NightMode::Low: return NightMode::High;
    case NightMode::High: break;
    }
    return NightMode::Off;
}

// The word in the settings file, the log and the menu.
inline const char* NightModeWord(NightMode mode) {
    switch (mode) {
    case NightMode::Low: return "low";
    case NightMode::High: return "high";
    case NightMode::Off: break;
    }
    return "off";
}

inline std::optional<NightMode> NightModeOf(std::string_view word) {
    if (word == "off") return NightMode::Off;
    if (word == "low") return NightMode::Low;
    if (word == "high") return NightMode::High;
    return std::nullopt;
}

}  // namespace gp

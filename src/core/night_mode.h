// Night mode: lifts the dark parts of the picture so a night scene or a
// dark room reads better. Bright parts (peak above kNightModeKnee) are left
// exactly as they were. The curve itself runs in the Presenter's shaders
// (platform/shadow_curve.h); this is the choice and its strength.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string_view>

namespace gp {

// Auto follows the scene: the darker the picture, the stronger the lift.
enum class NightMode { Off, Low, High, Auto };

// Above this peak brightness (display-encoded, 0..1) nothing changes.
inline constexpr float kNightModeKnee = 0.65f;

// How far dark values move toward their square root: 0 none, 1 all the way.
// Auto has no fixed strength (AutoNightMode below): 0 here.
inline float NightModeStrength(NightMode mode) {
    switch (mode) {
    case NightMode::Low: return 0.45f;
    case NightMode::High: return 1.0f;
    case NightMode::Off:
    case NightMode::Auto: break;
    }
    return 0.0f;
}

// The N key and the menu item: off, low, high, auto, off again.
inline NightMode NextNightMode(NightMode mode) {
    switch (mode) {
    case NightMode::Off: return NightMode::Low;
    case NightMode::Low: return NightMode::High;
    case NightMode::High: return NightMode::Auto;
    case NightMode::Auto: break;
    }
    return NightMode::Off;
}

// The word in the settings file, the log and the menu.
inline const char* NightModeWord(NightMode mode) {
    switch (mode) {
    case NightMode::Low: return "low";
    case NightMode::High: return "high";
    case NightMode::Auto: return "auto";
    case NightMode::Off: break;
    }
    return "off";
}

inline std::optional<NightMode> NightModeOf(std::string_view word) {
    if (word == "off") return NightMode::Off;
    if (word == "low") return NightMode::Low;
    if (word == "high") return NightMode::High;
    if (word == "auto") return NightMode::Auto;
    return std::nullopt;
}

// Auto night mode's scene brightness, as the mean luma code (0-255, limited
// range: black 16, white 235) of a coarse grid (core/luma_summary.h).
// At or below kAutoNightDark the lift is full (High); at or above
// kAutoNightBright it is off; in between it slides evenly. The numbers to
// tune, together with the time constant.
inline constexpr float kAutoNightDark = 45.0f;
inline constexpr float kAutoNightBright = 95.0f;
// The scene brightness is smoothed over about this long, so a muzzle flash,
// a headlight or a cut to a loading screen does not make the picture pump.
inline constexpr float kAutoNightSmoothingMs = 2500.0f;

class AutoNightMode {
public:
    // sceneLuma: the latest frame's mean luma, below 0 when unknown (no
    // picture): the strength is then held. nowMs: monotonic milliseconds.
    // Answers the strength to use now, 0..1.
    float Update(int sceneLuma, uint64_t nowMs) {
        if (sceneLuma < 0) return strength_;
        const float luma = static_cast<float>(sceneLuma);
        if (smoothed_ < 0) {
            smoothed_ = luma;  // the first look is taken as it is
        } else if (nowMs > lastMs_) {
            const float alpha = 1.0f - std::exp(-static_cast<float>(nowMs - lastMs_) / kAutoNightSmoothingMs);
            smoothed_ += (luma - smoothed_) * alpha;
        }
        lastMs_ = nowMs;
        strength_ = std::clamp((kAutoNightBright - smoothed_) / (kAutoNightBright - kAutoNightDark), 0.0f, 1.0f);
        return strength_;
    }
    float Strength() const { return strength_; }
    float SceneLuma() const { return smoothed_; }  // smoothed; below 0 before the first look
    void Reset() { *this = AutoNightMode{}; }

private:
    float smoothed_ = -1.0f;
    uint64_t lastMs_ = 0;
    float strength_ = 0.0f;
};

}  // namespace gp

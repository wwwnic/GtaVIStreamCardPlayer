// Everything the menu bar overrides, in one plain structure with the
// zero-configuration defaults. Ticket 09 saves and loads exactly this
// (core/settings_file.h).
#pragma once

#include <optional>
#include <string>

#include "core/audio_controls.h"
#include "core/capture_choice.h"
#include "core/night_mode.h"
#include "core/target_rate.h"
#include "core/unique_frames.h"

namespace gp {

enum class GenerationAvailability { Available, MissingRuntime, Failed };
// The Frame Generation choice. Double and Quadruple are Generation Factors;
// Target follows a Target Rate instead (ADR 0004): one frame per refresh of
// the monitor the window is on, picked from the frames 4x makes.
enum class GenerationFactor { Off, Double, Quadruple, Target };
constexpr GenerationFactor NextGeneration(GenerationFactor factor) {
    switch (factor) {
    case GenerationFactor::Off: return GenerationFactor::Double;
    case GenerationFactor::Double: return GenerationFactor::Quadruple;
    case GenerationFactor::Quadruple: return GenerationFactor::Target;
    default: return GenerationFactor::Off;
    }
}

constexpr const char* GenerationName(GenerationFactor factor) {
    switch (factor) {
    case GenerationFactor::Double: return "2x";
    case GenerationFactor::Quadruple: return "4x";
    case GenerationFactor::Target: return "Target";
    default: return "off";
    }
}

// The confirmation line for G: "Frame generation 4x", "Frame generation
// Target (144)" with the rate in use.
inline std::string GenerationLine(GenerationFactor factor, int targetRate) {
    std::string line = std::string("Frame generation ") + GenerationName(factor);
    if (factor == GenerationFactor::Target) line += " (" + std::to_string(targetRate) + ")";
    return line;
}

struct ViewerSettings {
    GenerationFactor generation = GenerationFactor::Off;
    int targetRate = kTargetRateMonitor;   // Target only: Monitor, else 1..1000 per second
    int customTargetRate = kNoCustomTargetRate;  // last typed in the Custom box, kept under a preset
    std::wstring captureCard;              // friendly name; empty: automatic (the GC553 Pro, else the first)
    std::wstring captureCardId;            // its symbolic link, tried before the name; empty: not known yet
    std::optional<Resolution> resolution;  // empty: the default, 1080p60
    GameRateOverride frameRate = GameRateOverride::Auto;
    AudioSettings audio;                   // volume, mute, Audio Delay
    bool alwaysOnTop = false;              // windowed only
    bool sharpUpscale = false;
    NightMode nightMode = NightMode::Off;  // the N key: lifts the dark parts of the picture
    bool vsync = false;                    // off: present on arrival, tearing allowed
    bool fpsCounter = false;               // the F key; drawn only into frames presented anyway
    bool halfHourClock = true;
    bool halfHourSession = false;
    bool showSessionClock = false;

    bool operator==(const ViewerSettings&) const = default;
};

inline bool ToggleSharpUpscale(ViewerSettings& settings, bool repeated) {
    if (repeated) return false;
    settings.sharpUpscale = !settings.sharpUpscale;
    return true;
}

}  // namespace gp

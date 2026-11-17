// Everything the menu bar overrides, in one plain structure with the
// zero-configuration defaults. Ticket 09 saves and loads exactly this
// (core/settings_file.h).
#pragma once

#include <optional>
#include <string>

#include "core/audio_controls.h"
#include "core/capture_choice.h"
#include "core/unique_frames.h"

namespace gp {

enum class GenerationAvailability { Available, MissingRuntime, Failed };
enum class GenerationFactor { Off, Double, Quadruple };
constexpr GenerationFactor NextGeneration(GenerationFactor factor) {
    switch (factor) {
    case GenerationFactor::Off: return GenerationFactor::Double;
    case GenerationFactor::Double: return GenerationFactor::Quadruple;
    default: return GenerationFactor::Off;
    }
}

constexpr const char* GenerationName(GenerationFactor factor) {
    switch (factor) {
    case GenerationFactor::Double: return "2x";
    case GenerationFactor::Quadruple: return "4x";
    default: return "off";
    }
}

struct ViewerSettings {
    GenerationFactor generation = GenerationFactor::Off;
    std::wstring captureCard;              // friendly name; empty: automatic (the GC553 Pro, else the first)
    std::wstring captureCardId;            // its symbolic link, tried before the name; empty: not known yet
    std::optional<Resolution> resolution;  // empty: the default, 1080p60
    GameRateOverride frameRate = GameRateOverride::Auto;
    AudioSettings audio;                   // volume, mute, Audio Delay
    bool alwaysOnTop = false;              // windowed only
    bool vsync = false;                    // off: present on arrival, tearing allowed
    bool fpsCounter = false;               // the F key; drawn only into frames presented anyway

    bool operator==(const ViewerSettings&) const = default;
};

}  // namespace gp

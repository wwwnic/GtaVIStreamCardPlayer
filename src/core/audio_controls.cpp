#include "core/audio_controls.h"

#include <algorithm>

namespace gp {

namespace {

std::string VolumeLine(int percent) { return "Volume " + std::to_string(percent) + "%"; }

AudioKeyResult StepVolume(AudioSettings settings, int step) {
    settings.muted = false;  // changing the volume while muted brings the sound back
    settings.volumePercent = ClampVolume(settings.volumePercent + step);
    return {settings, VolumeLine(settings.volumePercent)};
}

AudioKeyResult StepDelay(AudioSettings settings, int step) {
    settings.audioDelayMs = ClampAudioDelay(settings.audioDelayMs + step);
    std::string line = "Audio delay ";
    if (settings.audioDelayMs > 0) line += "+";
    line += std::to_string(settings.audioDelayMs) + " ms";
    if (settings.audioDelayMs == 0) line += " (minimum)";
    if (settings.audioDelayMs == AudioSettings::kMaxDelayMs) line += " (maximum)";
    return {settings, line};
}

}  // namespace

int ClampVolume(int percent) { return std::clamp(percent, 0, 100); }

int ClampAudioDelay(int ms) { return std::clamp(ms, 0, AudioSettings::kMaxDelayMs); }

double GainOf(const AudioSettings& settings) {
    return settings.muted ? 0.0 : ClampVolume(settings.volumePercent) / 100.0;
}

AudioKeyResult PressAudioKey(AudioSettings settings, AudioKey key) {
    switch (key) {
    case AudioKey::VolumeUp:
        return StepVolume(settings, AudioSettings::kVolumeStep);
    case AudioKey::VolumeDown:
        return StepVolume(settings, -AudioSettings::kVolumeStep);
    case AudioKey::ToggleMute:
        settings.muted = !settings.muted;
        return {settings, settings.muted ? "Muted" : "Unmuted, volume " + std::to_string(settings.volumePercent) + "%"};
    case AudioKey::DelayEarlier:
        return StepDelay(settings, -AudioSettings::kDelayStepMs);
    case AudioKey::DelayLater:
        return StepDelay(settings, AudioSettings::kDelayStepMs);
    case AudioKey::DelayReset:
        settings.audioDelayMs = 0;
        return {settings, "Audio delay 0 ms (reset)"};
    }
    return {settings, ""};
}

}  // namespace gp

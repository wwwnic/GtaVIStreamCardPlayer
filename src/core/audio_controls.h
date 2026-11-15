// Audio controls: the Viewer's own volume, mute and Audio Delay, and what
// each audio key (Up, Down, M, J, K) does to them.
#pragma once

#include <string>

namespace gp {

struct AudioSettings {
    static constexpr int kVolumeStep = 5;  // percentage points
    static constexpr int kDelayStepMs = 50;
    static constexpr int kMaxDelayMs = 500;

    int volumePercent = 100;  // 0-100; kept while muted
    bool muted = false;
    int audioDelayMs = 0;  // 0-500; positive plays the sound later
};

// Volume to 0-100, Audio Delay to 0-500 ms (any value, for settings and menu).
int ClampVolume(int percent);
int ClampAudioDelay(int ms);

// The sample gain the settings give: 0 when muted, else the volume as a fraction.
double GainOf(const AudioSettings& settings);

// DelayReset has no key; it is the Audio menu's "Reset Audio Delay to 0".
enum class AudioKey { VolumeUp, VolumeDown, ToggleMute, DelayEarlier, DelayLater, DelayReset };

struct AudioKeyResult {
    AudioSettings settings;
    std::string feedback;  // the confirmation line for the Video Area
};

AudioKeyResult PressAudioKey(AudioSettings settings, AudioKey key);

}  // namespace gp

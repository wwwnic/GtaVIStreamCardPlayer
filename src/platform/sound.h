// Sound: plays the Feed's sound through the Windows default output.
#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "core/audio_controls.h"

namespace gp {

// Captures the Capture Card's audio endpoint and renders it to the Windows
// default output device, both WASAPI shared mode and event-driven, with the
// sound playout (ring, drift correction, resampling) in between. Follows
// changes of the default output while running. Logs what it chose and,
// once a second for ten seconds then once a minute, the buffer fill.
class Sound {
public:
    Sound();
    ~Sound();
    Sound(const Sound&) = delete;
    Sound& operator=(const Sound&) = delete;

    // Finds the audio endpoint whose name contains `cardName` and starts
    // playing. Returns false (reason in the debug log) when there is no sound
    // to capture; a missing output device is not a failure, the sound starts
    // when one appears. Requires COM (multithreaded) on the calling thread.
    bool Start(const std::wstring& cardName);

    // Stops and releases both endpoints. Safe to call when not started.
    void Stop();

    // False once capture has failed by itself (the card was unplugged, say).
    bool Capturing() const;

    // Sound arriving from the card since the previous call (no-signal
    // investigation, logged once a second).
    struct Probe {
        uint32_t frames = 0;        // audio frames captured
        uint32_t silentFrames = 0;  // of those, flagged silent by Windows
        float peak = 0.0f;          // largest sample magnitude, 0 to 1
    };
    Probe TakeProbe();

    // Audio controls, as plain values for the keys, the menu (08) and the
    // settings (09). The Viewer's own level on its own samples, never the
    // Windows volume. Call from one thread (the window's). May be set before
    // Start; kept across Stop. Out-of-range values are clamped.
    int Volume() const { return settings_.volumePercent; }  // 0-100%
    void SetVolume(int percent);
    bool Muted() const { return settings_.muted; }  // keeps the volume
    void SetMuted(bool muted);
    int AudioDelayMs() const { return settings_.audioDelayMs; }  // 0-500; later sound
    void SetAudioDelayMs(int ms);
    // All three at once: the sound goes straight to the new gain, never
    // through a mix of old and new values (muted 5% to unmuted 0% is never
    // briefly 5%). One log line.
    void SetAudio(const AudioSettings& audio);

private:
    void ApplySettings();

    struct Impl;
    std::unique_ptr<Impl> impl_;
    AudioSettings settings_;
};

}  // namespace gp

// Sound: plays the Feed's sound through the Windows default output.
#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace gp {

// Captures the Capture Card's audio endpoint and renders it to the Windows
// default output device, both WASAPI shared mode and event-driven, with the
// sound playout (ring, drift correction, resampling) in between. Follows
// changes of the default output while running. Logs what it chose and,
// once a second for ten seconds then once a minute, the buffer fill.
//
// Later (ticket 07): set volume, set mute, set Audio Delay.
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

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace gp

// Zero-configuration choices: which capture device and which of its modes.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

#include "core/frame.h"

namespace gp {

// One mode the Capture Card offers, in the order the card lists them.
struct CaptureMode {
    std::optional<PixelFormat> format;  // empty: a format the Viewer does not use (MJPEG, RGB, P010...)
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t fpsNumerator = 0;
    uint32_t fpsDenominator = 1;
};

// Index of the device to open: the GC553 Pro (by name) when present,
// otherwise the first device. Empty when there are no devices.
std::optional<size_t> ChooseCaptureDevice(std::span<const std::wstring> deviceNames);

// Index of the mode to capture in: uncompressed 1920x1080 at the 60 Hz
// Signal Rate. YUY2 is preferred over NV12; within a format the rate closest
// to 60 wins (60 over 59.94). Empty when the card offers no such mode.
std::optional<size_t> ChooseCaptureMode(std::span<const CaptureMode> modes);

// Which audio capture endpoint carries the Feed sound.
struct SoundEndpointChoice {
    size_t index = 0;
    bool byCardName = false;  // false: no endpoint matched the card, a fallback was used
};

// Among endpoints whose name contains the Capture Card's name, one with
// "HDMI" in its name wins, else the first. If none contains the card's name,
// the first whose name suggests a capture device (never a microphone or a
// virtual device). An endpoint named
// "Not To Use ..." is never chosen. Names compare ignoring letter case.
// Empty when nothing qualifies.
std::optional<SoundEndpointChoice> ChooseSoundEndpoint(std::span<const std::wstring> endpointNames,
                                                       const std::wstring& cardName);

}  // namespace gp

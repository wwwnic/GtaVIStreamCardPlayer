// Zero-configuration choices: which capture device and which of its modes.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "core/frame.h"

namespace gp {

// One mode the Capture Card offers, in the order the card lists them.
struct CaptureMode {
    std::optional<PixelFormat> format;  // empty: a format the Viewer does not use (MJPEG, RGB, P010...)
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t fpsNumerator = 0;
    uint32_t fpsDenominator = 1;
    bool motionJpeg = false;  // an MJPG mode (format is then empty)

    double Fps() const { return fpsDenominator ? static_cast<double>(fpsNumerator) / fpsDenominator : 0.0; }
};

struct Resolution {
    uint32_t width = 0;
    uint32_t height = 0;
    bool operator==(const Resolution&) const = default;
};

// Index of the device to open: the GC553 Pro (by name) when present, else
// another GC553, otherwise the first device. Empty when there are no devices.
std::optional<size_t> ChooseCaptureDevice(std::span<const std::wstring> deviceNames);
// The device named `chosen` (the menu's Capture Card) when present; when it
// is not, or `chosen` is empty, the automatic choice above.
std::optional<size_t> ChooseCaptureDevice(std::span<const std::wstring> deviceNames, const std::wstring& chosen);
// A remembered Capture Card (ticket 09): the device whose identifier
// (symbolic link, `deviceIds` parallel to `deviceNames`) is `chosenId`, else
// the one named `chosenName`, else the automatic choice.
std::optional<size_t> ChooseCaptureDevice(std::span<const std::wstring> deviceNames,
                                          std::span<const std::wstring> deviceIds, const std::wstring& chosenName,
                                          const std::wstring& chosenId);

// Index of the mode to capture in: uncompressed 1920x1080 at the 60 Hz
// Signal Rate. YUY2 is preferred over NV12; within a format the rate closest
// to 60 wins (60 over 59.94). Empty when the card offers no such mode.
std::optional<size_t> ChooseCaptureMode(std::span<const CaptureMode> modes);
// With a chosen resolution (the menu's Resolution): its OfferedResolutions
// mode; when the card does not offer it, or none is chosen, the default above.
std::optional<size_t> ChooseCaptureMode(std::span<const CaptureMode> modes, std::optional<Resolution> chosen);

// One entry of the Resolution menu: a size and the mode that captures it.
struct OfferedResolution {
    Resolution resolution;
    size_t modeIndex = 0;  // into the card's mode list
    double fps = 0.0;      // that mode's rate (60 when the card has it)
};

// The sizes the Viewer offers to capture in, smallest first: uncompressed
// (YUY2, NV12) modes at 60 fps or below, never an MJPEG mode nor an
// uncompressed one listed with an MJPEG twin of the same size and rate (on
// the GC553 Pro those are full range BT.601, probably MJPEG-derived). Per size
// the rate nearest 60 wins (never above), then YUY2 over NV12.
std::vector<OfferedResolution> OfferedResolutions(std::span<const CaptureMode> modes);

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

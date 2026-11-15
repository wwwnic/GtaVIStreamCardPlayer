#include "core/capture_choice.h"

#include <algorithm>
#include <cmath>
#include <cwctype>

namespace gp {

namespace {

bool ContainsIgnoringCase(const std::wstring& text, std::wstring_view lowerNeedle) {
    std::wstring lower = text;
    for (wchar_t& c : lower) c = static_cast<wchar_t>(std::towlower(c));
    return lower.find(lowerNeedle) != std::wstring::npos;
}

// Lower is better; empty means the Viewer cannot use this mode.
std::optional<double> ModeCost(const CaptureMode& mode) {
    if (!mode.format || mode.width != 1920 || mode.height != 1080 || mode.fpsDenominator == 0) return std::nullopt;
    double fps = static_cast<double>(mode.fpsNumerator) / mode.fpsDenominator;
    double offBy = std::fabs(fps - 60.0);
    if (offBy >= 1.0) return std::nullopt;
    // Format decides first; the rate only breaks ties within a format.
    double formatCost = (*mode.format == PixelFormat::YUY2) ? 0.0 : 10.0;
    return formatCost + offBy;
}

}  // namespace

std::optional<size_t> ChooseCaptureDevice(std::span<const std::wstring> deviceNames) {
    if (deviceNames.empty()) return std::nullopt;
    for (size_t i = 0; i < deviceNames.size(); i++) {
        if (ContainsIgnoringCase(deviceNames[i], L"gc553")) return i;
    }
    return 0;
}

std::optional<size_t> ChooseCaptureMode(std::span<const CaptureMode> modes) {
    std::optional<size_t> best;
    double bestCost = 0.0;
    for (size_t i = 0; i < modes.size(); i++) {
        std::optional<double> cost = ModeCost(modes[i]);
        if (cost && (!best || *cost < bestCost)) {
            best = i;
            bestCost = *cost;
        }
    }
    return best;
}

std::optional<size_t> ChooseCaptureDevice(std::span<const std::wstring> deviceNames, const std::wstring& chosen) {
    if (!chosen.empty()) {
        for (size_t i = 0; i < deviceNames.size(); i++) {
            if (deviceNames[i] == chosen) return i;
        }
    }
    return ChooseCaptureDevice(deviceNames);
}

namespace {

bool HasMjpegTwin(std::span<const CaptureMode> modes, const CaptureMode& mode) {
    for (const CaptureMode& other : modes) {
        if (other.motionJpeg && other.width == mode.width && other.height == mode.height &&
            other.fpsNumerator == mode.fpsNumerator && other.fpsDenominator == mode.fpsDenominator) {
            return true;
        }
    }
    return false;
}

// Lower is better; empty means the mode is never offered. Rates within 1 fps
// of 60 count as 60, so YUY2 59.94 beats NV12 60 as in the default choice.
std::optional<double> OfferCost(std::span<const CaptureMode> modes, const CaptureMode& mode) {
    if (!mode.format || mode.width == 0 || mode.height == 0 || mode.fpsDenominator == 0) return std::nullopt;
    const double fps = mode.Fps();
    if (fps <= 0.0 || fps > 60.5 || HasMjpegTwin(modes, mode)) return std::nullopt;
    const double below = 60.0 - fps;
    const double band = below < 1.0 ? 0.0 : below;
    const double formatCost = (*mode.format == PixelFormat::YUY2) ? 0.0 : 10.0;
    return band * 100.0 + formatCost + std::fabs(below);
}

}  // namespace

std::vector<OfferedResolution> OfferedResolutions(std::span<const CaptureMode> modes) {
    std::vector<OfferedResolution> offered;
    std::vector<double> costs;
    for (size_t i = 0; i < modes.size(); i++) {
        const std::optional<double> cost = OfferCost(modes, modes[i]);
        if (!cost) continue;
        const Resolution size{modes[i].width, modes[i].height};
        size_t at = 0;
        while (at < offered.size() && !(offered[at].resolution == size)) at++;
        if (at == offered.size()) {
            offered.push_back({size, i, modes[i].Fps()});
            costs.push_back(*cost);
        } else if (*cost < costs[at]) {
            offered[at] = {size, i, modes[i].Fps()};
            costs[at] = *cost;
        }
    }
    std::stable_sort(offered.begin(), offered.end(), [](const OfferedResolution& a, const OfferedResolution& b) {
        const uint64_t areaA = uint64_t{a.resolution.width} * a.resolution.height;
        const uint64_t areaB = uint64_t{b.resolution.width} * b.resolution.height;
        return areaA != areaB ? areaA < areaB : a.resolution.width < b.resolution.width;
    });
    return offered;
}

std::optional<size_t> ChooseCaptureMode(std::span<const CaptureMode> modes, std::optional<Resolution> chosen) {
    if (chosen) {
        for (const OfferedResolution& offer : OfferedResolutions(modes)) {
            if (offer.resolution == *chosen) return offer.modeIndex;
        }
    }
    return ChooseCaptureMode(modes);
}

std::optional<SoundEndpointChoice> ChooseSoundEndpoint(std::span<const std::wstring> endpointNames,
                                                       const std::wstring& cardName) {
    std::wstring card = cardName;
    for (wchar_t& c : card) c = static_cast<wchar_t>(std::towlower(c));
    std::optional<size_t> firstMatch;
    for (size_t i = 0; i < endpointNames.size(); i++) {
        const std::wstring& name = endpointNames[i];
        if (ContainsIgnoringCase(name, L"not to use")) continue;
        if (card.empty() || !ContainsIgnoringCase(name, card)) continue;
        if (ContainsIgnoringCase(name, L"hdmi")) return SoundEndpointChoice{i, true};
        if (!firstMatch) firstMatch = i;
    }
    if (firstMatch) return SoundEndpointChoice{*firstMatch, true};
    // Names that suggest a capture device rather than a microphone.
    const std::wstring_view kCaptureHints[] = {L"hdmi", L"capture", L"gc553", L"live gamer", L"avermedia",
                                               L"elgato", L"cam link"};
    for (size_t i = 0; i < endpointNames.size(); i++) {
        const std::wstring& name = endpointNames[i];
        if (ContainsIgnoringCase(name, L"not to use")) continue;
        if (ContainsIgnoringCase(name, L"microphone") || ContainsIgnoringCase(name, L"virtual")) continue;
        for (std::wstring_view hint : kCaptureHints) {
            if (ContainsIgnoringCase(name, hint)) return SoundEndpointChoice{i, false};
        }
    }
    return std::nullopt;
}

}  // namespace gp

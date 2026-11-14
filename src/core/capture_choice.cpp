#include "core/capture_choice.h"

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

}  // namespace gp

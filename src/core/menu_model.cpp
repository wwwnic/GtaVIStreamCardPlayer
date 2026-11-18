#include "core/menu_model.h"

#include <cstring>
#include <cwchar>

namespace gp {

namespace {

// 60 -> "60", 59.94 -> "59.94", 29.97 -> "29.97".
std::wstring FpsText(double fps) {
    wchar_t text[32];
    swprintf(text, sizeof text / sizeof text[0], L"%.2f", fps);
    std::wstring result = text;
    while (!result.empty() && result.back() == L'0') result.pop_back();
    if (!result.empty() && result.back() == L'.') result.pop_back();
    return result;
}

std::wstring SizeText(uint32_t width, uint32_t height) {
    return std::to_wstring(width) + L"x" + std::to_wstring(height);
}

}  // namespace

std::vector<MenuItem> CaptureCardItems(std::span<const CaptureCardEntry> present, const CaptureCardEntry& inUse) {
    std::vector<MenuItem> items;
    for (size_t i = 0; i < present.size(); i++) {
        const CaptureCardEntry& card = present[i];
        const bool byId = !card.id.empty() && !inUse.id.empty();
        const bool checked = byId ? card.id == inUse.id : !inUse.name.empty() && card.name == inUse.name;
        size_t same = 0, number = 0;
        for (size_t j = 0; j < present.size(); j++) {
            if (present[j].name != card.name) continue;
            same++;
            if (j <= i) number = same;
        }
        const std::wstring label = same > 1 ? card.name + L" (" + std::to_wstring(number) + L")" : card.name;
        items.push_back({label, checked, true});
    }
    if (items.empty()) items.push_back({L"No capture card found", false, false});
    return items;
}

std::vector<MenuItem> ResolutionItems(std::span<const OfferedResolution> offered, std::optional<Resolution> chosen) {
    std::vector<MenuItem> items;
    items.push_back({L"Default (1080p)", true, true});
    for (const OfferedResolution& offer : offered) {
        const bool checked = chosen && *chosen == offer.resolution;
        if (checked) items[0].checked = false;
        items.push_back({SizeText(offer.resolution.width, offer.resolution.height) + L" (" + FpsText(offer.fps) +
                             L" fps)",
                         checked, true});
    }
    return items;
}

std::vector<MenuItem> FrameRateItems(GameRateOverride setting) {
    return {
        {L"Auto (drop Repeated Frames)", setting == GameRateOverride::Auto, true},
        {L"30 fps", setting == GameRateOverride::Fps30, true},
        {L"60 fps (every frame)", setting == GameRateOverride::Fps60, true},
    };
}

std::vector<MenuItem> TargetRateItems(int targetRate, int customRate, int refresh) {
    std::vector<MenuItem> items;
    items.push_back({L"Monitor (" + std::to_wstring(refresh) + L")", targetRate == kTargetRateMonitor, true});
    for (int rate : kTargetRatePresets) items.push_back({std::to_wstring(rate), targetRate == rate, true});
    const bool custom = targetRate != kTargetRateMonitor && !IsTargetRatePreset(targetRate);
    items.push_back({customRate == kNoCustomTargetRate ? std::wstring(L"Custom…")
                                                       : L"Custom (" + std::to_wstring(customRate) + L")",
                     custom, true});
    return items;
}

AudioMenu AudioItems(const AudioSettings& audio) {
    AudioMenu menu;
    menu.mute = {L"Mute\tM", audio.muted, true};
    menu.volume = {L"Volume: " + std::to_wstring(audio.volumePercent) + L"%" + (audio.muted ? L" (muted)" : L""), false,
                   false};
    menu.audioDelay = {L"Audio Delay: " + std::wstring(audio.audioDelayMs > 0 ? L"+" : L"") +
                           std::to_wstring(audio.audioDelayMs) + L" ms",
                       false, false};
    menu.resetDelay = {L"Reset Audio Delay to 0", false, audio.audioDelayMs != 0};
    return menu;
}

ViewMenu ViewItems(const ViewerSettings& settings, bool fullscreenOn, GenerationAvailability availability) {
    ViewMenu menu;
    menu.fullscreen = {L"Fullscreen\tF11", fullscreenOn, true};
    menu.alwaysOnTop = {L"Always on top", settings.alwaysOnTop, true};
    const bool available = availability == GenerationAvailability::Available;
    const bool generating = available && settings.generation != GenerationFactor::Off;
    const std::string factor = GenerationName(settings.generation);
    menu.sharpUpscale = {L"Sharp upscale\tU", settings.sharpUpscale};
    menu.generation = {L"Frame generation " + std::wstring(factor.begin(), factor.end()) + L"\tG", generating, available};
    if (availability == GenerationAvailability::MissingRuntime)
        menu.generation.label = L"Frame generation (needs an AMD graphics card)\tG";
    if (availability == GenerationAvailability::Failed)
        menu.generation.label = L"Frame generation not available\tG";
    // Target forces V-sync on (ADR 0004); the user's own setting is kept for after.
    const bool target = generating && settings.generation == GenerationFactor::Target;
    menu.vsync = {L"V-sync", settings.vsync || target, !generating};
    menu.fpsCounter = {L"FPS counter\tF", settings.fpsCounter, true};
    return menu;
}

std::wstring HotkeysText() {
    return L"F11\tFullscreen on / off\n"
           L"Esc\tLeave fullscreen\n"
           L"M\tMute / unmute\n"
           L"Up / Down\tVolume up / down\n"
           L"J / K\tAudio Delay 50 ms less / more\n"
           L"U\tSharp upscale\n"
           L"G\tFrame generation off / 2x / 4x / Target\n"
           L"F\tFPS counter (frames shown per second)\n"
           L"\nAlt opens the menu (windowed only).";
}

std::wstring AboutText(const AboutInfo& info) {
    std::wstring text = std::wstring(L"GamePlayer ") + kViewerVersion + L"\n";
    text += L"Shows the Console's picture and sound from the Capture Card.\n\n";
    text += L"Capture Card: " + (info.device.empty() ? std::wstring(L"none") : info.device) +
            (!info.device.empty() && !info.mode ? L" (not playing)" : L"") + L"\n";
    text += L"Capture mode: ";
    if (info.mode) {
        const CaptureMode& mode = *info.mode;
        const wchar_t* format = !mode.format ? L"other" : *mode.format == PixelFormat::YUY2 ? L"YUY2" : L"NV12";
        text += SizeText(mode.width, mode.height) + L" " + format + L" at " + FpsText(mode.Fps()) + L" fps";
    } else {
        text += L"none";
    }
    text += L"\nSignal Rate: " + std::to_wstring(info.rates.signalRate) + L" fps (frames arriving)";
    text += L"\nGame Rate: " + std::to_wstring(info.rates.gameRate) + L" fps (Unique Frames)";
    if (info.frameRate != GameRateOverride::Auto) {
        const std::wstring setting(NameOf(info.frameRate), NameOf(info.frameRate) + std::strlen(NameOf(info.frameRate)));
        const bool generating = info.presentation.runningFactor > 1;
        const int realRate = generating ? info.presentation.realRate : info.rates.presentedRate;
        text += std::wstring(generating ? L"\nGame: " : L"\nShown: ") + std::to_wstring(realRate) +
                L" fps (Frame rate set to " + setting + L")";
    }
    if (info.presentation.runningFactor > 1)
        text += L"\nShown: " + std::to_wstring(info.presentation.shownRate) + L" fps (all frames presented)";
    text += L"\nFrame Generation: ";
    if (info.presentation.runningFactor > 1 && info.presentation.targetRate > 0) {
        text += L"Target (" + std::to_wstring(info.presentation.targetRate) + L")";
        if (!info.generationReason.empty()) text += L" (4x frames unavailable: " + info.generationReason + L")";
    } else if (info.presentation.runningFactor > 1) {
        text += std::to_wstring(info.presentation.runningFactor) + L"x";
        if (!info.generationReason.empty()) text += L" (4x unavailable: " + info.generationReason + L")";
    } else {
        text += L"off";
        if (info.generationAvailability == GenerationAvailability::MissingRuntime)
            text += L" (needs an AMD graphics card and driver)";
        else if (info.generationAvailability == GenerationAvailability::Failed)
            text += info.generationReason.empty() ? L" (not available this session)" : L" (" + info.generationReason + L")";
        else if (info.generationChosen) text += L" (starting)";
        else if (!info.generationReason.empty()) text += L" (4x unavailable: " + info.generationReason + L")";
    }
    text += L"\nSharp Upscale: ";
    const auto& p = info.presentation;
    if (p.sharpUpscale) {
        text += L"on (" + SizeText(p.feedWidth, p.feedHeight) + L" -> " +
                SizeText(p.shownWidth, p.shownHeight) + L")";
    } else {
        text += info.sharpUpscaleChosen ? L"off (not in use for this picture)" : L"off";
    }
    return text;
}

}  // namespace gp

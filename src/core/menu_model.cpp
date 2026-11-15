#include "core/menu_model.h"

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

std::vector<MenuItem> CaptureCardItems(std::span<const std::wstring> present, const std::wstring& inUse) {
    std::vector<MenuItem> items;
    for (const std::wstring& name : present) items.push_back({name, !inUse.empty() && name == inUse, true});
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

ViewMenu ViewItems(const ViewerSettings& settings, bool fullscreenOn) {
    ViewMenu menu;
    menu.fullscreen = {L"Fullscreen\tF11", fullscreenOn, true};
    menu.alwaysOnTop = {L"Always on top", settings.alwaysOnTop, true};
    menu.vsync = {L"V-sync", settings.vsync, true};
    return menu;
}

std::wstring HotkeysText() {
    return L"F11\tFullscreen on / off\n"
           L"Esc\tLeave fullscreen\n"
           L"M\tMute / unmute\n"
           L"Up / Down\tVolume up / down\n"
           L"J / K\tAudio Delay 50 ms less / more\n"
           L"F\tFPS counter (Game Rate / Signal Rate)\n"
           L"\nAlt opens the menu (windowed only).";
}

std::wstring AboutText(const AboutInfo& info) {
    std::wstring text = std::wstring(L"GamePlayer ") + kViewerVersion + L"\n";
    text += L"Shows the Console's picture and sound from the Capture Card.\n\n";
    text += L"Capture Card: " + (info.device.empty() ? std::wstring(L"none") : info.device) + L"\n";
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
    return text;
}

}  // namespace gp

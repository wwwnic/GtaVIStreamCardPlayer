// What the menu bar shows (ticket 08): labels, check marks and greyed items
// for the current settings and state, and the two Help texts. Pure logic; the
// window builds the native menu from it each time a menu opens.
#pragma once

#include <optional>
#include <span>
#include <string>
#include <vector>

#include "core/capture_choice.h"
#include "core/unique_frames.h"
#include "core/viewer_settings.h"

namespace gp {

inline constexpr wchar_t kViewerVersion[] = L"0.8";

struct MenuItem {
    std::wstring label;  // a tab separates the hotkey shown on the right
    bool checked = false;
    bool enabled = true;
};

// Device > Capture Card: the present devices, the one in use checked. With
// none present, one greyed line saying so.
std::vector<MenuItem> CaptureCardItems(std::span<const std::wstring> present, const std::wstring& inUse);

// Device > Resolution: "Default (1080p)" first, then `offered` in order
// ("1280x720 (60 fps)"). `chosen` is checked; the default when none is chosen
// or the card does not offer it.
std::vector<MenuItem> ResolutionItems(std::span<const OfferedResolution> offered, std::optional<Resolution> chosen);

// Device > Frame rate: Auto, 30, 60, in that order.
std::vector<MenuItem> FrameRateItems(GameRateOverride setting);

struct AudioMenu {
    MenuItem mute;        // checked while muted
    MenuItem volume;      // greyed, shows the value
    MenuItem audioDelay;  // greyed, shows the value
    MenuItem resetDelay;  // greyed at 0
};
AudioMenu AudioItems(const AudioSettings& audio);

struct ViewMenu {
    MenuItem fullscreen;
    MenuItem alwaysOnTop;
    MenuItem vsync;
    MenuItem fpsCounter;  // in step with the F key
};
ViewMenu ViewItems(const ViewerSettings& settings, bool fullscreenOn);

// Help > Hotkeys.
std::wstring HotkeysText();

// Help > About: name, version and what is in use now.
struct AboutInfo {
    std::wstring device;              // empty: none
    std::optional<CaptureMode> mode;  // empty: none
    FrameRates rates;
};
std::wstring AboutText(const AboutInfo& info);

}  // namespace gp

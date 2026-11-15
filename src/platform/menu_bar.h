// The menu bar (ticket 08): a native Win32 menu (ADR 0001) with Device,
// Audio, View and Help. What it shows comes from core/menu_model.h and is
// refreshed every time a menu opens, so it always agrees with the hotkeys.
// No accelerator table: the hotkeys stay with the window (main.cpp).
// Window's thread only.
#pragma once

#include <windows.h>

#include <optional>
#include <string>
#include <vector>

#include "core/capture_choice.h"
#include "core/viewer_settings.h"

namespace gp {

// WM_COMMAND ids. The two lists use a range each.
enum MenuCommand : UINT {
    kMenuCaptureCardFirst = 1000,  // up to kMenuListMax entries
    kMenuResolutionDefault = 1100,
    kMenuResolutionFirst = 1101,
    kMenuRateAuto = 1200,
    kMenuRate30,
    kMenuRate60,
    kMenuVolumeUp = 1300,
    kMenuVolumeDown,
    kMenuMute,
    kMenuDelayLater,
    kMenuDelayEarlier,
    kMenuDelayReset,
    kMenuVolumeShown,  // greyed value lines, never sent as commands
    kMenuDelayShown,
    kMenuFullscreen = 1400,
    kMenuAlwaysOnTop,
    kMenuVsync,
    kMenuFpsCounter,
    kMenuHotkeys = 1500,
    kMenuAbout,
};
inline constexpr UINT kMenuListMax = 99;

class MenuBar {
public:
    MenuBar();
    ~MenuBar();  // destroys the menus unless a window still has them
    MenuBar(const MenuBar&) = delete;
    MenuBar& operator=(const MenuBar&) = delete;

    HMENU Handle() const { return bar_; }

    struct State {
        ViewerSettings settings;
        bool fullscreen = false;
        CaptureCardEntry deviceInUse;    // the one opened last, playing or not
        std::vector<CaptureMode> modes;  // the card's mode list
    };
    // WM_INITMENUPOPUP: brings every check mark and value up to date; the
    // Capture Card list is read from the system as it opens.
    void Opening(HMENU popup, const State& state);

    // WM_COMMAND from a list: the Capture Card or resolution it stands for.
    // Resolution: an inner empty optional is "Default (1080p)".
    std::optional<CaptureCardEntry> CaptureCardFor(UINT id) const;
    std::optional<std::optional<Resolution>> ResolutionFor(UINT id) const;

private:
    HMENU bar_ = nullptr;
    HMENU device_ = nullptr, captureCard_ = nullptr, resolution_ = nullptr, rate_ = nullptr;
    HMENU audio_ = nullptr, view_ = nullptr, help_ = nullptr;
    std::vector<CaptureCardEntry> cards_;       // as listed in captureCard_
    std::vector<OfferedResolution> offered_;    // as listed in resolution_, after the default
};

}  // namespace gp

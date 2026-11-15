// Remembered settings (ticket 09): what is kept between runs, as the text of
// one small human-readable file (key=value lines, UTF-8). Pure logic; the
// file itself is read and written at the edge (src/app/main.cpp).
//
// Reading never fails: a missing, empty, damaged or other-version text, and
// any invalid value, gives the default for what is affected. Unknown keys are
// ignored. Fullscreen is never remembered.
#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "core/viewer_settings.h"
#include "core/window_shape.h"

namespace gp {

inline constexpr int kSettingsVersion = 1;

// The windowed position to reopen at: the normal (not maximised, not
// fullscreen) window rectangle in screen pixels, and whether it was maximised.
struct SavedWindow {
    WindowRect normal;
    bool maximised = false;
    bool operator==(const SavedWindow&) const = default;
};

struct RememberedSettings {
    ViewerSettings viewer;
    std::optional<SavedWindow> window;  // empty: the default size on the primary monitor
    bool operator==(const RememberedSettings&) const = default;
};

std::string SettingsToText(const RememberedSettings& settings);
RememberedSettings SettingsFromText(std::string_view text);

// When to write the file: shortly after a change, not on every pixel of a
// drag. Told once a second what the settings are now, as text.
class SettingsSaveTiming {
public:
    // `onDisk`: the file's text as read at start-up ("" when there was none),
    // so a damaged or older file is replaced soon after launch.
    explicit SettingsSaveTiming(std::string onDisk) : onDisk_(std::move(onDisk)) {}

    // True when `now` differs from the file and was the same a second ago.
    bool Second(const std::string& now);
    // At exit: true when `now` differs from the file.
    bool Differs(const std::string& now) const { return now != onDisk_; }
    // After a successful write.
    void Written(const std::string& text) { onDisk_ = text; }

private:
    std::string onDisk_;
    std::optional<std::string> previous_;
};

}  // namespace gp

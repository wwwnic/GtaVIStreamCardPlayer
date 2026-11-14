// Which monitor fullscreen goes to: the one the window is on. No picker.
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace gp {

// Screen rectangle, Win32 style: right and bottom are exclusive.
struct ScreenRect {
    int left = 0, top = 0, right = 0, bottom = 0;
    bool operator==(const ScreenRect&) const = default;
};

struct Monitor {
    std::wstring name;  // the display device name, stable across resolution changes
    ScreenRect bounds;
};

// The monitor containing most of the window. If the window touches none,
// the nearest one. Nothing when there are no monitors.
std::optional<size_t> MonitorForWindow(const ScreenRect& window, const std::vector<Monitor>& monitors);

// The monitor with this name, if it is still connected.
std::optional<size_t> FindMonitor(const std::wstring& name, const std::vector<Monitor>& monitors);

}  // namespace gp

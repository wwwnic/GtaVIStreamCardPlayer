#include "core/monitor_choice.h"

#include <algorithm>
#include <cstdint>

namespace gp {

namespace {

int64_t OverlapArea(const ScreenRect& a, const ScreenRect& b) {
    const int64_t width = std::min(a.right, b.right) - std::max(a.left, b.left);
    const int64_t height = std::min(a.bottom, b.bottom) - std::max(a.top, b.top);
    return width > 0 && height > 0 ? width * height : 0;
}

// Squared length of the gap between two rectangles; 0 when they touch.
int64_t GapSquared(const ScreenRect& a, const ScreenRect& b) {
    const int64_t dx = std::max({0, a.left - b.right, b.left - a.right});
    const int64_t dy = std::max({0, a.top - b.bottom, b.top - a.bottom});
    return dx * dx + dy * dy;
}

}  // namespace

std::optional<size_t> MonitorForWindow(const ScreenRect& window, const std::vector<Monitor>& monitors) {
    std::optional<size_t> best;
    int64_t bestOverlap = 0;
    for (size_t i = 0; i < monitors.size(); i++) {
        const int64_t overlap = OverlapArea(window, monitors[i].bounds);
        if (overlap > bestOverlap) {
            bestOverlap = overlap;
            best = i;
        }
    }
    if (best) return best;
    int64_t bestGap = INT64_MAX;
    for (size_t i = 0; i < monitors.size(); i++) {
        const int64_t gap = GapSquared(window, monitors[i].bounds);
        if (gap < bestGap) {
            bestGap = gap;
            best = i;
        }
    }
    return best;
}

std::optional<size_t> FindMonitor(const std::wstring& name, const std::vector<Monitor>& monitors) {
    for (size_t i = 0; i < monitors.size(); i++) {
        if (monitors[i].name == name) return i;
    }
    return std::nullopt;
}

}  // namespace gp

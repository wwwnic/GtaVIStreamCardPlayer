#include "doctest.h"

#include "core/monitor_choice.h"

using gp::FindMonitor;
using gp::Monitor;
using gp::MonitorForWindow;
using gp::ScreenRect;

namespace {

// Two 1080p monitors side by side; the second is left of the primary.
const std::vector<Monitor> kTwoMonitors = {
    {L"\\\\.\\DISPLAY1", {0, 0, 1920, 1080}},
    {L"\\\\.\\DISPLAY2", {-1920, 0, 0, 1080}},
};

}  // namespace

TEST_CASE("a window wholly on one monitor goes fullscreen on that monitor") {
    CHECK(MonitorForWindow({100, 100, 1380, 820}, kTwoMonitors) == 0u);
    CHECK(MonitorForWindow({-1800, 100, -520, 820}, kTwoMonitors) == 1u);
}

TEST_CASE("a window across two monitors goes to the one holding most of it") {
    CHECK(MonitorForWindow({-1000, 100, 280, 820}, kTwoMonitors) == 1u);  // 1000 px left, 280 right
    CHECK(MonitorForWindow({-280, 100, 1000, 820}, kTwoMonitors) == 0u);
}

TEST_CASE("a window touching no monitor goes to the nearest one") {
    CHECK(MonitorForWindow({2500, 100, 3780, 820}, kTwoMonitors) == 0u);
    CHECK(MonitorForWindow({-4000, 2000, -3000, 2500}, kTwoMonitors) == 1u);
}

TEST_CASE("with no monitors there is no fullscreen target") {
    CHECK(MonitorForWindow({0, 0, 100, 100}, {}) == std::nullopt);
}

TEST_CASE("a monitor is found again by name, with its new bounds, after a resolution change") {
    const std::vector<Monitor> after = {{L"\\\\.\\DISPLAY2", {-1280, 0, 0, 720}}, {L"\\\\.\\DISPLAY1", {0, 0, 2560, 1440}}};
    CHECK(FindMonitor(L"\\\\.\\DISPLAY1", after) == 1u);
}

TEST_CASE("an unplugged monitor is not found") {
    const std::vector<Monitor> after = {{L"\\\\.\\DISPLAY1", {0, 0, 1920, 1080}}};
    CHECK(FindMonitor(L"\\\\.\\DISPLAY2", after) == std::nullopt);
}

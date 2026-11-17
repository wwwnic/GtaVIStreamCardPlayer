#include "doctest.h"

#include "core/window_shape.h"

using gp::KeepVideoArea16x9;
using gp::SizingEdge;
using gp::WindowRect;

namespace {

// A window frame like Windows 11 at 100%: 16 wide, 39 high (title bar and borders).
const int kFrameW = 16;
const int kFrameH = 39;
const int kMinW = 480;

int VideoW(const WindowRect& r) { return r.right - r.left - kFrameW; }
int VideoH(const WindowRect& r) { return r.bottom - r.top - kFrameH; }

}  // namespace

TEST_CASE("dragging the right edge sets the height from the width, keeping the top-left corner") {
    // Video Area proposed 1600 x 720: width leads, 1600 x 900.
    const WindowRect r = KeepVideoArea16x9({100, 50, 100 + 1600 + kFrameW, 50 + 720 + kFrameH}, SizingEdge::Right,
                                           kFrameW, kFrameH, kMinW);
    CHECK(r == WindowRect{100, 50, 100 + 1600 + kFrameW, 50 + 900 + kFrameH});
}

TEST_CASE("dragging the left edge keeps the right edge and the top where they are") {
    const WindowRect r = KeepVideoArea16x9({300, 50, 2000, 50 + 720 + kFrameH}, SizingEdge::Left, kFrameW, kFrameH, kMinW);
    // 2000 - 300 - 16 = 1684 wide, rounded to 1680 (a multiple of 16) -> 945 high.
    CHECK(r == WindowRect{2000 - 1680 - kFrameW, 50, 2000, 50 + 945 + kFrameH});
}

TEST_CASE("dragging the bottom edge sets the width from the height, keeping the top-left corner") {
    const WindowRect r = KeepVideoArea16x9({100, 50, 100 + 1280 + kFrameW, 50 + 900 + kFrameH}, SizingEdge::Bottom,
                                           kFrameW, kFrameH, kMinW);
    CHECK(r == WindowRect{100, 50, 100 + 1600 + kFrameW, 50 + 900 + kFrameH});
}

TEST_CASE("dragging the top edge keeps the bottom edge and the left where they are") {
    const WindowRect r = KeepVideoArea16x9({100, 400, 100 + 1280 + kFrameW, 1000}, SizingEdge::Top, kFrameW, kFrameH, kMinW);
    // 1000 - 400 - 39 = 561 high: 561 / 9 = 62.3 -> 62 units = 992 x 558.
    CHECK(r == WindowRect{100, 1000 - 558 - kFrameH, 100 + 992 + kFrameW, 1000});
}

TEST_CASE("dragging a corner follows whichever direction the pointer went further, moving only that corner") {
    SUBCASE("wider than 16:9: width leads") {
        const WindowRect r = KeepVideoArea16x9({500, 300, 2000, 1000}, SizingEdge::TopLeft, kFrameW, kFrameH, kMinW);
        // 1484 x 661 proposed: 1484 / 16 = 92.75 -> 93 units = 1488 x 837.
        CHECK(r == WindowRect{2000 - 1488 - kFrameW, 1000 - 837 - kFrameH, 2000, 1000});
    }
    SUBCASE("taller than 16:9: height leads") {
        const WindowRect r = KeepVideoArea16x9({0, 0, 1000, 900}, SizingEdge::BottomRight, kFrameW, kFrameH, kMinW);
        // 984 x 861 proposed: 861 / 9 = 95.7 -> 96 units = 1536 x 864.
        CHECK(r == WindowRect{0, 0, 1536 + kFrameW, 864 + kFrameH});
    }
    SUBCASE("top-right keeps the bottom-left corner") {
        const WindowRect r = KeepVideoArea16x9({100, 100, 100 + 1600 + kFrameW, 1000}, SizingEdge::TopRight, kFrameW,
                                               kFrameH, kMinW);
        CHECK(r == WindowRect{100, 1000 - 900 - kFrameH, 100 + 1600 + kFrameW, 1000});
    }
    SUBCASE("bottom-left keeps the top-right corner") {
        const WindowRect r = KeepVideoArea16x9({1000, 100, 2000, 200}, SizingEdge::BottomLeft, kFrameW, kFrameH, kMinW);
        // 984 wide leads: 984 / 16 = 61.5 -> 62 units = 992 x 558.
        CHECK(r == WindowRect{2000 - 992 - kFrameW, 100, 2000, 100 + 558 + kFrameH});
    }
}

TEST_CASE("the Video Area is always exactly 16:9 in whole pixels") {
    for (int w = 300; w < 2600; w += 7) {
        const WindowRect r = KeepVideoArea16x9({0, 0, w, 500}, SizingEdge::Right, kFrameW, kFrameH, kMinW);
        CHECK(VideoW(r) * 9 == VideoH(r) * 16);
    }
}

TEST_CASE("the Video Area never goes below the minimum width") {
    const WindowRect r = KeepVideoArea16x9({100, 100, 200, 150}, SizingEdge::BottomRight, kFrameW, kFrameH, kMinW);
    CHECK(VideoW(r) == 480);
    CHECK(VideoH(r) == 270);
    CHECK(r.left == 100);
    CHECK(r.top == 100);
}

TEST_CASE("a remembered window on a connected monitor reopens where it was, at its size") {
    const WindowRect primary{0, 0, 2560, 1440};
    const WindowRect left{-1920, 200, 0, 1280};  // a second monitor, left of the primary
    const WindowRect monitors[] = {primary, left};
    const int slack = 8;

    SUBCASE("on the primary monitor, unchanged") {
        const WindowRect saved{100, 50, 100 + 1600 + kFrameW, 50 + 900 + kFrameH};
        CHECK(gp::RestoredWindowRect(saved, monitors, kFrameW, kFrameH, kMinW, slack) == saved);
    }
    SUBCASE("on the second monitor, unchanged") {
        const WindowRect saved{-1800, 300, -1800 + 1280 + kFrameW, 300 + 720 + kFrameH};
        CHECK(gp::RestoredWindowRect(saved, monitors, kFrameW, kFrameH, kMinW, slack) == saved);
    }
    SUBCASE("a Video Area that is not 16:9 (a snap) keeps its size") {
        const WindowRect saved{100, 50, 100 + 1600 + kFrameW, 50 + 1000 + kFrameH};
        CHECK(gp::RestoredWindowRect(saved, monitors, kFrameW, kFrameH, kMinW, slack) == saved);
        const WindowRect leftHalf{-7, 0, 1280 + 7, 1440 + 7};  // snapped to the left half
        CHECK(gp::RestoredWindowRect(leftHalf, monitors, kFrameW, kFrameH, kMinW, slack) == leftHalf);
    }
    SUBCASE("too small: the minimum size") {
        const WindowRect saved{100, 50, 300, 200};
        const auto r = gp::RestoredWindowRect(saved, monitors, kFrameW, kFrameH, kMinW, slack);
        REQUIRE(r);
        CHECK(VideoW(*r) == 480);
        CHECK(VideoH(*r) == 270);
    }
    SUBCASE("a few pixels over an edge (the invisible resize border) is still fine") {
        const WindowRect saved{-7, 0, -7 + 1280 + kFrameW, 720 + kFrameH};
        CHECK(gp::RestoredWindowRect(saved, monitors, kFrameW, kFrameH, kMinW, slack) == saved);
    }
}

TEST_CASE("a remembered window that would not be fully on one monitor is not restored") {
    const WindowRect primary{0, 0, 1920, 1080};
    const WindowRect monitors[] = {primary};
    const int slack = 8;
    SUBCASE("its monitor is gone") {
        const WindowRect saved{-1800, 300, -1800 + 1280 + kFrameW, 300 + 720 + kFrameH};
        CHECK_FALSE(gp::RestoredWindowRect(saved, monitors, kFrameW, kFrameH, kMinW, slack));
    }
    SUBCASE("partly off-screen") {
        const WindowRect saved{1000, 500, 1000 + 1280 + kFrameW, 500 + 720 + kFrameH};
        CHECK_FALSE(gp::RestoredWindowRect(saved, monitors, kFrameW, kFrameH, kMinW, slack));
    }
    SUBCASE("larger than the monitor") {
        const WindowRect saved{0, 0, 2560 + kFrameW, 1440 + kFrameH};
        CHECK_FALSE(gp::RestoredWindowRect(saved, monitors, kFrameW, kFrameH, kMinW, slack));
    }
    SUBCASE("straddling two monitors") {
        const WindowRect two[] = {WindowRect{0, 0, 1920, 1080}, WindowRect{1920, 0, 3840, 1080}};
        const WindowRect saved{1500, 100, 1500 + 1280 + kFrameW, 100 + 720 + kFrameH};
        CHECK_FALSE(gp::RestoredWindowRect(saved, two, kFrameW, kFrameH, kMinW, slack));
    }
    SUBCASE("no monitors at all") {
        const WindowRect saved{0, 0, 1296, 759};
        CHECK_FALSE(gp::RestoredWindowRect(saved, {}, kFrameW, kFrameH, kMinW, slack));
    }
}

TEST_CASE("a taller frame (a menu bar) is left out of the Video Area") {
    const int menuFrameH = kFrameH + 20;
    const WindowRect r = KeepVideoArea16x9({0, 0, 1280 + kFrameW, 500}, SizingEdge::Right, kFrameW, menuFrameH, kMinW);
    CHECK(r == WindowRect{0, 0, 1280 + kFrameW, 720 + menuFrameH});
}

TEST_CASE("moved to a monitor with other scaling, a 16:9 Video Area stays exactly 16:9") {
    // 1280 x 720 at 100% (frame 16 x 39); Windows suggests the window 1.5x
    // bigger at 150%, where the frame is 24 x 59: Video Area 1920 x 1079.
    const int frameW = 24, frameH = 59;
    const WindowRect suggested = {200, 100, 200 + 1944, 100 + 1138};
    const WindowRect r = gp::AfterScalingChange(suggested, 1280, 720, frameW, frameH, 720);
    CHECK(r == WindowRect{200, 100, 200 + 1920 + frameW, 100 + 1080 + frameH});

    SUBCASE("and never below the new monitor's minimum") {
        const WindowRect small = {0, 0, 640 + frameW, 360 + frameH};
        const WindowRect shaped = gp::AfterScalingChange(small, 480, 270, frameW, frameH, 720);
        CHECK(shaped == WindowRect{0, 0, 720 + frameW, 405 + frameH});
    }
}

TEST_CASE("moved to a monitor with other scaling, any other shape (a snap) is left as Windows suggests") {
    const WindowRect suggested = {0, 0, 1440, 1560};
    CHECK(gp::AfterScalingChange(suggested, 944, 1001, 24, 59, 720) == suggested);
    CHECK(gp::AfterScalingChange(suggested, 1281, 720, 24, 59, 720) == suggested);  // not whole 16:9 blocks
    CHECK(gp::AfterScalingChange(suggested, 0, 0, 24, 59, 720) == suggested);
}

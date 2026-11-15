#include "doctest.h"

#include "core/cursor_idle.h"

using gp::CursorIdle;
using gp::CursorPlace;

namespace {

// Resting over the Video Area of the active window, nothing else going on.
CursorPlace OverVideoArea() {
    CursorPlace place;
    place.overVideoArea = true;
    place.windowActive = true;
    return place;
}

}  // namespace

TEST_CASE("windowed, the cursor resting over the Video Area hides after 2 seconds") {
    CursorIdle cursor;
    cursor.MouseAt(10, 10, 0);
    cursor.Tick(1999, OverVideoArea());
    CHECK_FALSE(cursor.Hidden());
    cursor.Tick(2000, OverVideoArea());
    CHECK(cursor.Hidden());
    SUBCASE("and comes back at once when the mouse moves") {
        cursor.MouseAt(11, 10, 2100);
        CHECK_FALSE(cursor.Hidden());
    }
}

TEST_CASE("windowed, the cursor stays shown anywhere but a still Video Area of the active window") {
    const auto stillFor60s = [](CursorPlace place) {
        CursorIdle cursor;
        cursor.MouseAt(10, 10, 0);
        cursor.Tick(60'000, place);
        return cursor.Hidden();
    };
    CursorPlace place = OverVideoArea();
    SUBCASE("over the title bar, menu bar or a border") {
        place.overVideoArea = false;
        CHECK_FALSE(stillFor60s(place));
    }
    SUBCASE("while a menu is open") {
        place.menuOpen = true;
        CHECK_FALSE(stillFor60s(place));
    }
    SUBCASE("while the window is moved or resized") {
        place.movingOrSizing = true;
        CHECK_FALSE(stillFor60s(place));
    }
    SUBCASE("when another window is active") {
        place.windowActive = false;
        CHECK_FALSE(stillFor60s(place));
    }
    SUBCASE("nowhere known (the default place)") { CHECK_FALSE(stillFor60s(CursorPlace{})); }
}

TEST_CASE("windowed, a hidden cursor comes back as soon as it is no longer over a still Video Area") {
    CursorIdle cursor;
    cursor.Tick(5000, OverVideoArea());
    REQUIRE(cursor.Hidden());
    CursorPlace left = OverVideoArea();
    left.overVideoArea = false;  // left the window without a mouse message to it
    cursor.Tick(5250, left);
    CHECK_FALSE(cursor.Hidden());
    SUBCASE("back over the Video Area, the 2 seconds start again (from the last tick away from it)") {
        cursor.Tick(5500, OverVideoArea());
        CHECK_FALSE(cursor.Hidden());
        cursor.Tick(7249, OverVideoArea());
        CHECK_FALSE(cursor.Hidden());
        cursor.Tick(7250, OverVideoArea());
        CHECK(cursor.Hidden());
    }
}

TEST_CASE("after a menu closes over the Video Area, the cursor waits 2 seconds more") {
    CursorIdle cursor;
    CursorPlace menu = OverVideoArea();
    menu.menuOpen = true;
    cursor.Tick(10'000, menu);
    cursor.Tick(10'250, OverVideoArea());
    CHECK_FALSE(cursor.Hidden());
    cursor.Tick(12'250, OverVideoArea());
    CHECK(cursor.Hidden());
}

TEST_CASE("in fullscreen the place does not matter, as before") {
    CursorIdle cursor;
    cursor.SetFullscreen(true, 0);
    cursor.Tick(2000, CursorPlace{});
    CHECK(cursor.Hidden());
}

TEST_CASE("in fullscreen the cursor hides after 2 seconds without movement") {
    CursorIdle cursor;
    cursor.SetFullscreen(true, 1000);
    cursor.Tick(2999);
    CHECK_FALSE(cursor.Hidden());
    cursor.Tick(3000);
    CHECK(cursor.Hidden());
}

TEST_CASE("moving the mouse shows the cursor and restarts the 2 seconds") {
    CursorIdle cursor;
    cursor.SetFullscreen(true, 0);
    cursor.MouseAt(5, 5, 100);
    cursor.Tick(2500);
    CHECK(cursor.Hidden());
    cursor.MouseAt(6, 5, 2600);
    CHECK_FALSE(cursor.Hidden());
    cursor.Tick(4599);
    CHECK_FALSE(cursor.Hidden());
    cursor.Tick(4600);
    CHECK(cursor.Hidden());
}

TEST_CASE("a mouse-move report at the same position is not movement") {
    // Windows repeats WM_MOUSEMOVE when the cursor changes or a window moves under it.
    CursorIdle cursor;
    cursor.SetFullscreen(true, 0);
    cursor.MouseAt(5, 5, 0);
    cursor.Tick(2000);
    CHECK(cursor.Hidden());
    cursor.MouseAt(5, 5, 2100);
    CHECK(cursor.Hidden());
}

TEST_CASE("leaving fullscreen shows the cursor at once and keeps it shown away from the Video Area") {
    CursorIdle cursor;
    cursor.SetFullscreen(true, 0);
    cursor.Tick(5000);
    REQUIRE(cursor.Hidden());
    cursor.SetFullscreen(false, 5000);
    CHECK_FALSE(cursor.Hidden());
    cursor.Tick(60'000);
    CHECK_FALSE(cursor.Hidden());
}

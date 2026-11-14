#include "doctest.h"

#include "core/cursor_idle.h"

using gp::CursorIdle;

TEST_CASE("in windowed mode the cursor is never hidden") {
    CursorIdle cursor;
    cursor.MouseAt(10, 10, 0);
    cursor.Tick(60'000);
    CHECK_FALSE(cursor.Hidden());
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

TEST_CASE("leaving fullscreen shows the cursor at once and keeps it shown") {
    CursorIdle cursor;
    cursor.SetFullscreen(true, 0);
    cursor.Tick(5000);
    REQUIRE(cursor.Hidden());
    cursor.SetFullscreen(false, 5000);
    CHECK_FALSE(cursor.Hidden());
    cursor.Tick(60'000);
    CHECK_FALSE(cursor.Hidden());
}

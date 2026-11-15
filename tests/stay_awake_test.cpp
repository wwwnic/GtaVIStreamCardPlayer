#include "doctest.h"

#include "core/stay_awake.h"

using gp::StayAwake;
using Change = gp::StayAwake::Change;

TEST_CASE("with no Feed the PC is never kept awake") {
    StayAwake awake;
    for (int i = 0; i < 10; i++) CHECK(awake.Second(false) == Change::None);
    CHECK_FALSE(awake.Held());
}

TEST_CASE("the hold is taken in the first second a Feed plays, once") {
    StayAwake awake;
    CHECK(awake.Second(true) == Change::Take);
    CHECK(awake.Held());
    CHECK(awake.Second(true) == Change::None);
    CHECK(awake.Held());
}

TEST_CASE("the hold is released after 3 quiet seconds, not on a short gap") {
    StayAwake awake;
    awake.Second(true);
    CHECK(awake.Second(false) == Change::None);
    CHECK(awake.Second(false) == Change::None);
    CHECK(awake.Second(true) == Change::None);  // a mode change or hiccup
    CHECK(awake.Second(false) == Change::None);
    CHECK(awake.Second(false) == Change::None);
    CHECK(awake.Second(false) == Change::Release);
    CHECK_FALSE(awake.Held());
    CHECK(awake.Second(false) == Change::None);
}

TEST_CASE("execution state flags read as words") {
    CHECK(gp::ExecutionStateText(0) == "none");
    CHECK(gp::ExecutionStateText(0x3) == "SYSTEM|DISPLAY");
    CHECK(gp::ExecutionStateText(0x80000003u) == "SYSTEM|DISPLAY|CONTINUOUS");
    CHECK(gp::ExecutionStateText(0x1 | 0x40) == "SYSTEM|AWAYMODE");
    CHECK(gp::ExecutionStateText(0x100) == "0x100");
}

TEST_CASE("display off or dimmed while held is flagged, otherwise plain") {
    CHECK(gp::DisplayStateLine(1, true) == "Display: on");
    CHECK(gp::DisplayStateLine(0, false) == "Display: off");
    CHECK(gp::DisplayStateLine(0, true) == "Display: off while stay awake is held (should not happen)");
    CHECK(gp::DisplayStateLine(2, true) == "Display: dimmed while stay awake is held (should not happen)");
    CHECK(gp::DisplayStateLine(7, false) == "Display: state 7");
}

TEST_CASE("the hold is taken again when the Feed comes back") {
    StayAwake awake;
    awake.Second(true);
    for (int i = 0; i < 3; i++) awake.Second(false);
    REQUIRE_FALSE(awake.Held());
    CHECK(awake.Second(true) == Change::Take);
}

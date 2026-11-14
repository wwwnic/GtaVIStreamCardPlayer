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

TEST_CASE("the hold is taken again when the Feed comes back") {
    StayAwake awake;
    awake.Second(true);
    for (int i = 0; i < 3; i++) awake.Second(false);
    REQUIRE_FALSE(awake.Held());
    CHECK(awake.Second(true) == Change::Take);
}

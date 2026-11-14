#include "doctest.h"

#include "core/picture_fit.h"

using gp::FitPicture;
using gp::PictureRect;

TEST_CASE("a 16:9 Video Area is filled completely") {
    CHECK(FitPicture(1280, 720) == PictureRect{0, 0, 1280, 720});
}

TEST_CASE("a Video Area taller than 16:9 gets black bars above and below") {
    CHECK(FitPicture(1600, 1000) == PictureRect{0, 50, 1600, 900});
}

TEST_CASE("a Video Area wider than 16:9 gets black bars left and right") {
    CHECK(FitPicture(2000, 900) == PictureRect{200, 0, 1600, 900});
}

TEST_CASE("the picture never exceeds the Video Area when sizes do not divide evenly") {
    // 1000 wide at 16:9 is 562.5 high.
    PictureRect r = FitPicture(1000, 1000);
    CHECK(r.x == 0);
    CHECK(r.width == 1000);
    CHECK((r.height == 562 || r.height == 563));
    CHECK(r.y == (1000 - r.height) / 2);
}

TEST_CASE("an empty Video Area gives an empty picture") {
    CHECK(FitPicture(0, 0) == PictureRect{});
    CHECK(FitPicture(500, 0) == PictureRect{});
}

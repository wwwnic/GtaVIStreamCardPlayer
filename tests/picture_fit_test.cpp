#include "doctest.h"

#include <utility>

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

TEST_CASE("a picture of another shape is fitted in that shape, as the Presenter asks for a capture mode's size") {
    // 720x576 (5:4) in a 16:9 Video Area: bars left and right.
    CHECK(FitPicture(1280, 720, 720, 576) == PictureRect{190, 0, 900, 720});
    // 2560x1080 (21:9) in a 16:9 Video Area: bars above and below.
    CHECK(FitPicture(1280, 720, 2560, 1080) == PictureRect{0, 90, 1280, 540});
    // 1920x1080 is 16:9 whatever the numbers.
    CHECK(FitPicture(1600, 1000, 1920, 1080) == FitPicture(1600, 1000));
}

TEST_CASE("an empty Video Area gives an empty picture") {
    CHECK(FitPicture(0, 0) == PictureRect{});
    CHECK(FitPicture(500, 0) == PictureRect{});
}

TEST_CASE("Sharp Upscale applies only to an enlarged Feed with the choice on") {
    CHECK(gp::UseSharpUpscale(true, 1280, 720, FitPicture(2560, 1440)));
    CHECK_FALSE(gp::UseSharpUpscale(false, 1280, 720, FitPicture(1920, 1080)));
    CHECK_FALSE(gp::UseSharpUpscale(true, 1280, 720, FitPicture(1280, 720)));
    CHECK_FALSE(gp::UseSharpUpscale(true, 1920, 1080, FitPicture(1280, 720)));
    CHECK_FALSE(gp::UseSharpUpscale(true, 0, 0, FitPicture(1280, 720)));
}

TEST_CASE("sharpening alone applies to a Feed shown at exactly its own size") {
    CHECK(gp::UseNativeSharpen(true, 1920, 1080, FitPicture(1920, 1080)));
    CHECK(gp::UseNativeSharpen(true, 1280, 720, FitPicture(1280, 720)));
    CHECK_FALSE(gp::UseNativeSharpen(false, 1920, 1080, FitPicture(1920, 1080)));
    CHECK_FALSE(gp::UseNativeSharpen(true, 1920, 1080, FitPicture(1280, 720)));    // smaller: smooth
    CHECK_FALSE(gp::UseNativeSharpen(true, 1280, 720, FitPicture(1920, 1080)));    // larger: Sharp Upscale
    CHECK_FALSE(gp::UseNativeSharpen(true, 0, 0, FitPicture(0, 0)));
    // Never both at once.
    for (const auto& [w, h] : {std::pair{1920, 1080}, std::pair{2560, 1440}, std::pair{1280, 720}}) {
        const auto shown = FitPicture(w, h);
        CHECK_FALSE((gp::UseNativeSharpen(true, 1920, 1080, shown) && gp::UseSharpUpscale(true, 1920, 1080, shown)));
    }
}

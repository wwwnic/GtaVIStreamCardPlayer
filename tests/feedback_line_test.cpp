#include "doctest.h"

#include "core/feedback_line.h"

using gp::FeedbackLine;

TEST_CASE("nothing is shown before the first confirmation") {
    FeedbackLine line;
    CHECK(line.TextAt(0).empty());
    CHECK_FALSE(line.HideAtMs().has_value());
}

TEST_CASE("a confirmation shows for 1.5 seconds, then is gone") {
    FeedbackLine line;
    line.Show("Volume 80%", 10000);
    CHECK(line.TextAt(10000) == "Volume 80%");
    CHECK(line.TextAt(11499) == "Volume 80%");
    CHECK(line.TextAt(11500).empty());
    CHECK(line.HideAtMs() == 11500);
}

TEST_CASE("a new confirmation replaces the line in place and restarts its time") {
    FeedbackLine line;
    line.Show("Volume 80%", 10000);
    line.Show("Volume 85%", 11000);
    CHECK(line.TextAt(11000) == "Volume 85%");
    CHECK(line.TextAt(12000) == "Volume 85%");  // past the first one's end
    CHECK(line.TextAt(12500).empty());
    CHECK(line.HideAtMs() == 12500);
}

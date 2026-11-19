#include "doctest.h"

#include "core/shown_texts.h"

using gp::ShownTexts;
using gp::TextPlace;
using gp::VideoAreaText;

namespace {
const size_t kTop = static_cast<size_t>(TextPlace::Top);
const size_t kCentre = static_cast<size_t>(TextPlace::Centre);
}  // namespace

TEST_CASE("a notice and a feedback line at the top are both readable, whichever comes first") {
    ShownTexts first;  // the feedback, then the notice
    first.Show(TextPlace::Top, {L"Target rate 120", L""}, 1500, 10000);
    CHECK(first.ShowNotice(TextPlace::Top, L"120 on a 144 Hz monitor", 4000, 10010));
    CHECK(first.All()[kTop] == VideoAreaText{L"Target rate 120", L"120 on a 144 Hz monitor"});
    ShownTexts second;  // the notice, then the feedback
    second.ShowNotice(TextPlace::Top, L"120 on a 144 Hz monitor", 4000, 10000);
    CHECK(second.All()[kTop] == VideoAreaText{L"120 on a 144 Hz monitor", L""});
    CHECK(second.Show(TextPlace::Top, {L"Target rate 120", L""}, 1500, 10010));
    CHECK(second.All()[kTop] == VideoAreaText{L"Target rate 120", L"120 on a 144 Hz monitor"});
    // The feedback ends first: the notice stays alone until its own end.
    CHECK(second.MsUntilNextEnd(10010) == 1500u);
    CHECK(second.EndExpired(11510));
    CHECK(second.All()[kTop] == VideoAreaText{L"120 on a 144 Hz monitor", L""});
    CHECK(second.MsUntilNextEnd(11510) == 2490u);
    CHECK(second.EndExpired(14000));
    CHECK(second.All()[kTop].Empty());
    CHECK_FALSE(second.MsUntilNextEnd(14000).has_value());
}

TEST_CASE("nothing shows and nothing is due before the first text") {
    ShownTexts texts;
    for (const VideoAreaText& text : texts.All()) CHECK(text.Empty());
    CHECK_FALSE(texts.MsUntilNextEnd(0).has_value());
    CHECK_FALSE(texts.EndExpired(100000));
}

TEST_CASE("a brief text (feedback) shows for its time, then is gone, with one drawing each way") {
    ShownTexts texts;
    CHECK(texts.Show(TextPlace::Top, {L"Volume 80%", L""}, 1500, 10000));
    CHECK(texts.All()[kTop].headline == L"Volume 80%");
    CHECK(texts.MsUntilNextEnd(10000) == 1500u);
    CHECK_FALSE(texts.EndExpired(11499));
    CHECK(texts.All()[kTop].headline == L"Volume 80%");
    CHECK(texts.MsUntilNextEnd(11499) == 1u);
    CHECK(texts.EndExpired(11500));
    CHECK(texts.All()[kTop].Empty());
    CHECK_FALSE(texts.MsUntilNextEnd(11500).has_value());
    CHECK_FALSE(texts.EndExpired(20000));  // only once
}

TEST_CASE("a new brief text replaces the old in place and restarts the time") {
    ShownTexts texts;
    texts.Show(TextPlace::Top, {L"Volume 80%", L""}, 1500, 10000);
    CHECK(texts.Show(TextPlace::Top, {L"Volume 85%", L""}, 1500, 11000));
    CHECK_FALSE(texts.EndExpired(12000));  // past the first one's end
    CHECK(texts.All()[kTop].headline == L"Volume 85%");
    CHECK(texts.EndExpired(12500));
}

// A key held at a limit (Up at 100%, J at 0 ms) sends the same line ~30 times
// a second: each present would count as a game frame for Lossless Scaling.
TEST_CASE("the same brief text again only moves its end: no drawing") {
    ShownTexts texts;
    texts.Show(TextPlace::Top, {L"Volume 100%", L""}, 1500, 10000);
    CHECK_FALSE(texts.Show(TextPlace::Top, {L"Volume 100%", L""}, 1500, 10033));
    CHECK_FALSE(texts.Show(TextPlace::Top, {L"Volume 100%", L""}, 1500, 11000));
    CHECK_FALSE(texts.EndExpired(11500));  // the first one's end has passed
    CHECK(texts.MsUntilNextEnd(11500) == 1000u);
    CHECK(texts.EndExpired(12500));
}

TEST_CASE("the same text after it has ended is drawn again") {
    ShownTexts texts;
    texts.Show(TextPlace::Top, {L"Volume 100%", L""}, 1500, 10000);
    texts.EndExpired(11500);
    CHECK(texts.Show(TextPlace::Top, {L"Volume 100%", L""}, 1500, 12000));
}

TEST_CASE("a lasting text stays until replaced, and the same one again draws nothing") {
    ShownTexts texts;
    CHECK(texts.Show(TextPlace::Centre, {L"No signal", L"Is the Console on?"}, 0, 10000));
    CHECK_FALSE(texts.Show(TextPlace::Centre, {L"No signal", L"Is the Console on?"}, 0, 10500));
    CHECK_FALSE(texts.MsUntilNextEnd(10500).has_value());
    CHECK_FALSE(texts.EndExpired(1000000));
    CHECK(texts.All()[kCentre].headline == L"No signal");
    CHECK(texts.Show(TextPlace::Centre, {}, 0, 1000000));  // cleared
    CHECK(texts.All()[kCentre].Empty());
}

TEST_CASE("brief texts at several places end each at their own time") {
    ShownTexts texts;
    texts.Show(TextPlace::Top, {L"Muted", L""}, 1500, 10000);
    texts.Show(TextPlace::Bottom, {L"note", L""}, 500, 10200);
    CHECK(texts.MsUntilNextEnd(10200) == 500u);
    CHECK(texts.EndExpired(10700));
    CHECK(texts.All()[kTop].headline == L"Muted");
    CHECK(texts.MsUntilNextEnd(10700) == 800u);
    CHECK(texts.MsUntilNextEnd(20000) == 0u);  // overdue: due now
}

TEST_CASE("a text for the next frame asks for no drawing and stays") {
    ShownTexts texts;
    texts.ShowWithNextFrame(TextPlace::TopLeft, {L"60 FPS", L""});
    CHECK(texts.All()[static_cast<size_t>(TextPlace::TopLeft)].headline == L"60 FPS");
    CHECK_FALSE(texts.MsUntilNextEnd(0).has_value());
}

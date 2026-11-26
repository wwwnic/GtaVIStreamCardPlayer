#include "doctest.h"

#include <string>

#include "core/video_area_state.h"
#include "core/video_area_text.h"
#include "core/session_timer.h"

using gp::MessageFor;
using gp::SizeText;
using gp::SourceEvent;
using gp::SourceStage;
using gp::TextPlace;
using gp::TextSizes;
using gp::VideoAreaState;
using gp::VideoAreaText;

namespace {
bool Contains(const std::wstring& text, const wchar_t* part) { return text.find(part) != std::wstring::npos; }
}  // namespace

TEST_CASE("Playing shows no message") {
    CHECK(MessageFor(VideoAreaState::Playing, {}).Empty());
}

TEST_CASE("session time shows local time and whole minutes since launch across hour and day boundaries") {
    CHECK(gp::SessionTimeText(L"20:42", 0) == L"20:42 · Session 0m");
    CHECK(gp::SessionTimeText(L"12:00 AM", 60) == L"12:00 AM · Session 1h 0m");
    CHECK(gp::SessionTimeText(L"08:42", 1514) == L"08:42 · Session 25h 14m");
}

TEST_CASE("clock reminders follow local half hours even five minutes into play") {
    gp::HalfHourReminder clock;
    CHECK_FALSE(clock.Due(17 * 60 + 55, true));
    CHECK(clock.Due(18 * 60, true));
    CHECK_FALSE(clock.Due(18 * 60, true));
    CHECK_FALSE(clock.Due(18 * 60 + 29, true));
    CHECK(clock.Due(18 * 60 + 30, true));
}

TEST_CASE("elapsed session reminders follow the session start independently of the local clock") {
    gp::SessionReminder session;
    gp::HalfHourReminder clock;
    CHECK_FALSE(clock.Due(17 * 60 + 55, true));
    CHECK(clock.Due(18 * 60, true));
    CHECK_FALSE(session.Due(5, true));
    CHECK_FALSE(clock.Due(18 * 60 + 25, true));
    CHECK(session.Due(30, true));
    CHECK_FALSE(session.Due(30, true));
    CHECK_FALSE(session.Due(60, false));
    CHECK(session.Due(92, true));
    CHECK_FALSE(session.Due(92, true));
    session.Reset(0);
    CHECK_FALSE(session.Due(0, true));
    CHECK(session.Due(30, true));
    session.Reset(100);  // switched on later in the session
    CHECK_FALSE(session.Due(100, true));
    CHECK(session.Due(120, true));
}

TEST_CASE("pausing preserves partial minutes and defers the next session reminder until play resumes") {
    constexpr uint64_t minute = 60000;
    gp::SessionTimer timer(100);
    gp::SessionReminder reminders;
    const uint64_t justBeforeReminder = 100 + 30 * minute - 1;
    timer.TogglePause(justBeforeReminder);
    CHECK(timer.Paused());
    const uint64_t resume = justBeforeReminder + 90 * minute;
    CHECK(timer.ElapsedMs(resume) == 30 * minute - 1);
    CHECK_FALSE(reminders.Due(timer.ElapsedMs(resume) / minute, true));
    timer.TogglePause(resume);
    CHECK_FALSE(timer.Paused());
    CHECK(reminders.Due(timer.ElapsedMs(resume + 1) / minute, true));
    timer.TogglePause(resume + 1000);
    timer.TogglePause(resume + 2000);
    CHECK(timer.ElapsedMs(resume + 2000) == 30 * minute + 999);
}

TEST_CASE("half-hour reminders missed in the background coalesce and survive midnight") {
    gp::HalfHourReminder clock;
    CHECK_FALSE(clock.Due(23 * 60 + 29, false));
    CHECK_FALSE(clock.Due(23 * 60 + 30, false));
    CHECK_FALSE(clock.Due(1, false));  // a delayed timer tick after midnight
    CHECK(clock.Due(2, true));
    CHECK_FALSE(clock.Due(2, true));
    CHECK_FALSE(clock.Due(3, true));
}

TEST_CASE("every other state has a one-line message") {
    for (VideoAreaState state : {VideoAreaState::Loading, VideoAreaState::NoCaptureCard, VideoAreaState::CardBusy,
                                 VideoAreaState::NoSignal, VideoAreaState::Error}) {
        const VideoAreaText text = MessageFor(state, {});
        CHECK_FALSE(text.headline.empty());
        CHECK(text.headline.find(L'\n') == std::wstring::npos);
    }
}

TEST_CASE("Card busy names the likely culprits") {
    const VideoAreaText text = MessageFor(VideoAreaState::CardBusy, {});
    CHECK(Contains(text.headline, L"another app"));
    CHECK(Contains(text.detail, L"OBS"));
    CHECK(Contains(text.detail, L"AVerMedia"));
}

TEST_CASE("Error gives the technical code and where it happened, in the smaller line") {
    const SourceEvent failure{SourceEvent::Kind::Failed, SourceStage::Reader, 0x80004005u};
    const VideoAreaText text = MessageFor(VideoAreaState::Error, failure);
    CHECK(Contains(text.detail, L"0x80004005"));
    CHECK(Contains(text.detail, L"creating the reader"));
    CHECK_FALSE(Contains(text.headline, L"0x"));
}

TEST_CASE("No signal points at the Console") {
    CHECK(Contains(MessageFor(VideoAreaState::NoSignal, {}).headline, L"signal"));
    CHECK(Contains(MessageFor(VideoAreaState::NoSignal, {}).detail, L"Console"));
}

TEST_CASE("a state message on a 1080p Video Area is large enough to read from a sofa") {
    const TextSizes sizes = SizeText(TextPlace::Centre, 1920, 1080);
    CHECK(sizes.headline >= 60.0f);
    CHECK(sizes.detail < sizes.headline);
    CHECK(sizes.detail >= 30.0f);
}

TEST_CASE("text scales down with a small window but never below a readable minimum") {
    const TextSizes big = SizeText(TextPlace::Centre, 1280, 720);
    const TextSizes small = SizeText(TextPlace::Centre, 320, 180);
    const TextSizes tiny = SizeText(TextPlace::Centre, 120, 60);
    CHECK(small.headline < big.headline);
    CHECK(tiny.headline == doctest::Approx(TextSizes::kMinHeadline));
    CHECK(tiny.detail == doctest::Approx(TextSizes::kMinDetail));
    CHECK(tiny.detail <= tiny.headline);
}

TEST_CASE("text stays inside the Video Area width, with a margin") {
    for (int width : {120, 320, 1280, 3840}) {
        const TextSizes sizes = SizeText(TextPlace::Centre, width, 1080);
        CHECK(sizes.maxWidth > 0.0f);
        CHECK(sizes.maxWidth + 2 * sizes.margin <= static_cast<float>(width) + 0.01f);
    }
}

TEST_CASE("a narrow, tall window does not get letters too big for its width") {
    CHECK(SizeText(TextPlace::Centre, 400, 1400).headline <= 400 / 15.0f);
}

TEST_CASE("notes and feedback at the top or bottom are smaller than a state message") {
    const TextSizes centre = SizeText(TextPlace::Centre, 1920, 1080);
    CHECK(SizeText(TextPlace::Top, 1920, 1080).headline < centre.headline);
    CHECK(SizeText(TextPlace::Bottom, 1920, 1080).headline < centre.headline);
}

TEST_CASE("the FPS counter in the corner is the smallest text, still readable") {
    const TextSizes corner = SizeText(TextPlace::TopLeft, 1920, 1080);
    CHECK(corner.headline < SizeText(TextPlace::Top, 1920, 1080).headline);
    CHECK(SizeText(TextPlace::TopLeft, 320, 180).headline >= TextSizes::kMinHeadline);
}

TEST_CASE("the H key list (headline and 15 lines) fits the smallest Video Area and fullscreen") {
    for (const auto [width, height] : {std::pair{480, 270}, std::pair{1280, 720}, std::pair{1920, 1080}}) {
        CAPTURE(height);
        const auto sizes = SizeText(TextPlace::Help, width, height);
        CHECK(sizes.detail >= TextSizes::kMinDetail);
        // About 1.33 font heights per line (Segoe UI), the gap half a detail line.
        CHECK(1.33f * sizes.headline + 0.5f * sizes.detail + 15 * 1.33f * sizes.detail < height);
        // The longest line: a 6-em key column and about 40 characters of half an em.
        CHECK((6 + 20) * sizes.detail < sizes.maxWidth);
    }
}

TEST_CASE("two counter lines fit the smallest Video Area and fullscreen") {
    for (const auto [width, height] : {std::pair{480,270}, std::pair{1920,1080}}) {
        const auto sizes = SizeText(TextPlace::TopLeft, width, height);
        CHECK(sizes.headline >= 14);
        // Conservative allowance: 1.5 font heights per line, one font width per character.
        CHECK(sizes.margin + 3 * sizes.headline + sizes.detail < height);
        CHECK(9 * sizes.headline < sizes.maxWidth); // "Shown 240"
    }
}

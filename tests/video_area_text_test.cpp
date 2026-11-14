#include "doctest.h"

#include <string>

#include "core/video_area_state.h"
#include "core/video_area_text.h"

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

#include "doctest.h"

#include <vector>

#include "core/target_rate.h"

using gp::PacedPresent;
using gp::PresentPacer;
using gp::RateInUse;

namespace {

// One second of presents at `rate` on a `refresh` monitor: every present
// until the vsyncs covered reach `refresh`.
std::vector<PacedPresent> OneSecond(int rate, int refresh) {
    PresentPacer pacer(rate, refresh);
    std::vector<PacedPresent> presents;
    int vsyncs = 0;
    while (vsyncs < refresh) {
        presents.push_back(pacer.Next());
        vsyncs += presents.back().syncInterval;
    }
    return presents;
}

int Fresh(const std::vector<PacedPresent>& presents) {
    int n = 0;
    for (const PacedPresent& p : presents) n += p.fresh ? 1 : 0;
    return n;
}

int Vsyncs(const std::vector<PacedPresent>& presents) {
    int n = 0;
    for (const PacedPresent& p : presents) n += p.syncInterval;
    return n;
}

// The vsyncs each fresh present stays for, with its fillers.
std::vector<int> Spans(const std::vector<PacedPresent>& presents) {
    std::vector<int> spans;
    for (const PacedPresent& p : presents) {
        if (p.fresh || spans.empty()) spans.push_back(0);
        spans.back() += p.syncInterval;
    }
    return spans;
}

}  // namespace

TEST_CASE("the Custom box takes whole numbers 1 to 1000") {
    using gp::TargetRateTyped;
    CHECK(TargetRateTyped(L"100") == 100);
    CHECK(TargetRateTyped(L"1") == 1);
    CHECK(TargetRateTyped(L"1000") == 1000);
    CHECK(TargetRateTyped(L" 72 ") == 72);  // spaces around are harmless
    CHECK(TargetRateTyped(L"0072") == 72);
    CHECK_FALSE(TargetRateTyped(L"0"));
    CHECK_FALSE(TargetRateTyped(L"1001"));
    CHECK_FALSE(TargetRateTyped(L"2000"));
    CHECK_FALSE(TargetRateTyped(L"99999999999999999999"));
    CHECK_FALSE(TargetRateTyped(L""));
    CHECK_FALSE(TargetRateTyped(L"   "));
    CHECK_FALSE(TargetRateTyped(L"abc"));
    CHECK_FALSE(TargetRateTyped(L"10a"));
    CHECK_FALSE(TargetRateTyped(L"-5"));
    CHECK_FALSE(TargetRateTyped(L"+5"));
    CHECK_FALSE(TargetRateTyped(L"72.5"));
    CHECK_FALSE(TargetRateTyped(L"7 2"));
    CHECK(std::wstring(gp::kTargetRateHint) == L"1 to 1000");
}

TEST_CASE("the Custom box opens with the custom number, else the rate in use") {
    using gp::CustomBoxStart;
    CHECK(CustomBoxStart(100, 100, 144) == 100);
    CHECK(CustomBoxStart(60, 100, 144) == 100);  // kept under a preset
    CHECK(CustomBoxStart(gp::kTargetRateMonitor, gp::kNoCustomTargetRate, 144) == 144);
    CHECK(CustomBoxStart(72, gp::kNoCustomTargetRate, 144) == 72);
    CHECK(CustomBoxStart(120, gp::kNoCustomTargetRate, 60) == 60);
}

TEST_CASE("rate in use: Monitor follows the refresh") {
    CHECK(RateInUse(gp::kTargetRateMonitor, 144) == 144);
    CHECK(RateInUse(gp::kTargetRateMonitor, 59) == 59);
}

TEST_CASE("rate in use: the smaller of the Target Rate and the refresh") {
    CHECK(RateInUse(120, 144) == 120);
    CHECK(RateInUse(72, 144) == 72);
    CHECK(RateInUse(120, 59) == 59);  // the number chosen is kept; the monitor limits it
    CHECK(RateInUse(240, 240) == 240);
}

TEST_CASE("pacing at the refresh: one present per vsync, as before") {
    const std::vector<PacedPresent> second = OneSecond(144, 144);
    CHECK(second.size() == 144);
    CHECK(Fresh(second) == 144);
    for (const PacedPresent& p : second) CHECK(p.syncInterval == 1);
}

TEST_CASE("pacing at 72 on 144: every second vsync") {
    const std::vector<PacedPresent> second = OneSecond(72, 144);
    CHECK(second.size() == 72);
    CHECK(Fresh(second) == 72);
    for (const PacedPresent& p : second) CHECK(p.syncInterval == 2);
}

TEST_CASE("pacing at 120 on 144: one two-vsync present in every five, evenly") {
    const std::vector<PacedPresent> second = OneSecond(120, 144);
    CHECK(second.size() == 120);
    CHECK(Fresh(second) == 120);
    CHECK(Vsyncs(second) == 144);
    std::vector<size_t> longOnes;
    for (size_t i = 0; i < second.size(); i++) {
        CHECK(second[i].syncInterval >= 1);
        CHECK(second[i].syncInterval <= 2);
        if (second[i].syncInterval == 2) longOnes.push_back(i);
    }
    REQUIRE(longOnes.size() == 24);
    for (size_t i = 1; i < longOnes.size(); i++) CHECK(longOnes[i] - longOnes[i - 1] == 5);
}

TEST_CASE("pacing at 60 on 144: two or three vsyncs, never two threes together") {
    const std::vector<PacedPresent> second = OneSecond(60, 144);
    CHECK(second.size() == 60);
    CHECK(Vsyncs(second) == 144);
    int threes = 0;
    for (size_t i = 0; i < second.size(); i++) {
        CHECK((second[i].syncInterval == 2 || second[i].syncInterval == 3));
        if (second[i].syncInterval == 3) {
            threes++;
            if (i > 0) CHECK(second[i - 1].syncInterval == 2);
        }
    }
    CHECK(threes == 24);
}

TEST_CASE("pacing keeps the rate over many seconds") {
    PresentPacer pacer(120, 144);
    int vsyncs = 0, fresh = 0;
    while (vsyncs < 144 * 60) {
        const PacedPresent p = pacer.Next();
        vsyncs += p.syncInterval;
        fresh += p.fresh ? 1 : 0;
    }
    CHECK(vsyncs == 144 * 60);
    CHECK(fresh == 120 * 60);
}

TEST_CASE("pacing a span over 4 vsyncs: the rest with the same picture again") {
    SUBCASE("30 on 144: spans of 4 and 5") {
        const std::vector<PacedPresent> second = OneSecond(30, 144);
        CHECK(Fresh(second) == 30);
        CHECK(Vsyncs(second) == 144);
        for (const PacedPresent& p : second) CHECK(p.syncInterval <= 4);
        for (int span : Spans(second)) CHECK((span == 4 || span == 5));
    }
    SUBCASE("1 on 144: one fresh present a second") {
        const std::vector<PacedPresent> second = OneSecond(1, 144);
        CHECK(Fresh(second) == 1);
        CHECK(second.front().fresh);
        CHECK(Vsyncs(second) == 144);
        CHECK(Spans(second) == std::vector<int>{144});
    }
}

TEST_CASE("real frames only when the rate in use is not above the Game Rate") {
    const int64_t second = 10'000'000;  // QPC ticks per second
    CHECK(gp::RealFramesOnly(second / 60, 60, second));       // 60 at a 60 fps Game Rate
    CHECK(gp::RealFramesOnly(second / 60, 59, second));       // the 59 Hz monitor
    CHECK(gp::RealFramesOnly(second / 60, 48, second));
    CHECK(gp::RealFramesOnly(160'000, 60, second));           // arrival jitter: 16.0 ms
    CHECK(gp::RealFramesOnly(175'000, 60, second));           // and 17.5 ms
    CHECK_FALSE(gp::RealFramesOnly(second / 60, 72, second)); // 72 at 60 fps: generate
    CHECK_FALSE(gp::RealFramesOnly(second / 30, 60, second));
    CHECK_FALSE(gp::RealFramesOnly(second / 30, 144, second));
    CHECK_FALSE(gp::RealFramesOnly(second / 26, 144, second));
}

TEST_CASE("uneven notice: once when an uneven rate starts applying") {
    gp::UnevenNotice notice;
    const std::optional<std::string> first = notice.RateApplies(120, 144);
    REQUIRE(first.has_value());
    CHECK(*first == "120 on a 144 Hz monitor: motion will not be perfectly even");
    CHECK_FALSE(notice.RateApplies(120, 144).has_value());  // not repeated while playing
    CHECK_FALSE(notice.RateApplies(120, 144).has_value());
}

TEST_CASE("uneven notice: never for a rate that divides the refresh") {
    gp::UnevenNotice notice;
    CHECK_FALSE(notice.RateApplies(144, 144).has_value());
    CHECK_FALSE(notice.RateApplies(72, 144).has_value());
    CHECK_FALSE(notice.RateApplies(48, 144).has_value());
    CHECK_FALSE(notice.RateApplies(59, 59).has_value());
}

TEST_CASE("uneven notice: again for another uneven rate or another monitor") {
    gp::UnevenNotice notice;
    CHECK(notice.RateApplies(120, 144).has_value());
    CHECK(notice.RateApplies(60, 144) == "60 on a 144 Hz monitor: motion will not be perfectly even");
    CHECK_FALSE(notice.RateApplies(72, 144).has_value());
    CHECK(notice.RateApplies(60, 144).has_value());  // uneven again after an even one
    CHECK(notice.RateApplies(60, 165).has_value());  // the window moved
    CHECK_FALSE(notice.RateApplies(60, 60).has_value());
}

TEST_CASE("uneven notice: again when Target starts again") {
    gp::UnevenNotice notice;
    CHECK(notice.RateApplies(120, 144).has_value());
    notice.Reset();
    CHECK(notice.RateApplies(120, 144).has_value());
}

TEST_CASE("pacing with a rate above the refresh or none: one per vsync") {
    CHECK(OneSecond(200, 144).size() == 144);
    CHECK(OneSecond(0, 144).size() == 144);
}

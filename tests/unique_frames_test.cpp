#include "doctest.h"

#include <cstdint>
#include <memory>
#include <vector>

#include "core/unique_frames.h"

using gp::Frame;
using gp::FramePtr;
using gp::GameRateOverride;
using gp::TimedFrame;
using gp::UniqueFrameProcessor;

namespace {

// A tiny clock: 600 ticks a second, so a 60 Hz Signal Rate is one frame every 10 ticks.
const int64_t kTicksPerSecond = 600;
const int64_t kSignalInterval = 10;
const size_t kWords = 1000;  // 8000-byte pictures; 0.5% of them is 5 words

// A picture identified by `picture` (0-255): equal numbers give byte-identical
// pixels, different ones differ in every word.
std::shared_ptr<Frame> Picture(int picture) {
    auto frame = std::make_shared<Frame>();
    frame->width = 100;
    frame->height = 40;
    frame->pixels.resize(kWords * 8);
    for (size_t i = 0; i < frame->pixels.size(); i++) {
        frame->pixels[i] = static_cast<uint8_t>(picture * 31 + i * 7);
    }
    return frame;
}

// `picture` with `words` of its 8-byte words changed slightly (noise from the card).
std::shared_ptr<Frame> NoisyPicture(int picture, size_t words) {
    auto frame = Picture(picture);
    for (size_t w = 0; w < words; w++) frame->pixels[w * (kWords / words) * 8] ^= 1;
    return frame;
}

// Feeds a sequence of pictures arriving at the Signal Rate; remembers what was emitted.
struct Feed {
    explicit Feed(double tolerance = gp::kNearDuplicateTolerance) : processor(kTicksPerSecond, tolerance) {}
    UniqueFrameProcessor processor;
    int64_t now = 0;
    std::vector<FramePtr> emitted;

    // Returns how many frames this arrival emitted.
    size_t Arrive(std::shared_ptr<Frame> frame) {
        now += kSignalInterval;
        frame->arrivalQpc = now;
        std::vector<TimedFrame> out;
        processor.Process(frame, out);
        for (const TimedFrame& shown : out) emitted.push_back(shown.frame);
        return out.size();
    }
    // Each number is one arrival showing that picture.
    void Arrive(const std::vector<int>& pictures) {
        for (int picture : pictures) Arrive(Picture(picture));
    }
    // `seconds` of a game whose pictures each stay for `repeats` signal frames.
    void Game(int seconds, int repeats, int& picture) {
        for (int i = 0; i < seconds * 60; i++) {
            if (i % repeats == 0) picture++;
            Arrive(Picture(picture));
        }
    }
    gp::FrameRates Rates() { return processor.RatesAt(now); }
};

}  // namespace

TEST_CASE("a byte-identical frame is an exact match") {
    CHECK(gp::CompareFrames(*Picture(1), *Picture(1), 0.005) == gp::FrameMatch::Exact);
    CHECK(gp::CompareFrames(*Picture(1), *Picture(2), 0.005) == gp::FrameMatch::Different);
}

TEST_CASE("tolerance boundary: fewer than 0.5% of words differing is a near match, 0.5% is not") {
    // 1000 words: 4 differing (0.4%) is under, 5 (0.5%) is not "fewer than 0.5%".
    double share = -1;
    CHECK(gp::CompareFrames(*NoisyPicture(1, 4), *Picture(1), 0.005, &share) == gp::FrameMatch::Near);
    CHECK(share == doctest::Approx(0.004));
    CHECK(gp::CompareFrames(*NoisyPicture(1, 5), *Picture(1), 0.005) == gp::FrameMatch::Different);
}

TEST_CASE("tolerance zero: only byte-identical frames are repeats") {
    CHECK(gp::CompareFrames(*NoisyPicture(1, 1), *Picture(1), 0.0) == gp::FrameMatch::Different);
    CHECK(gp::CompareFrames(*Picture(1), *Picture(1), 0.0) == gp::FrameMatch::Exact);
}

TEST_CASE("frames of another size are different, whatever their bytes") {
    auto smaller = Picture(1);
    smaller->height = 20;
    CHECK(gp::CompareFrames(*smaller, *Picture(1), 0.005) == gp::FrameMatch::Different);
}

TEST_CASE("a picture size that is not a whole number of words still compares its last bytes") {
    auto a = Picture(1);
    auto b = Picture(1);
    a->pixels.push_back(1);
    b->pixels.push_back(2);
    CHECK(gp::CompareFrames(*a, *b, 0.0) == gp::FrameMatch::Different);
}

TEST_CASE("all-unique input: every frame is emitted at once, as soon as possible") {
    Feed feed;
    for (int i = 1; i <= 60; i++) {
        auto frame = Picture(i);
        feed.now += kSignalInterval;
        frame->arrivalQpc = feed.now;
        std::vector<TimedFrame> out;
        feed.processor.Process(frame, out);
        REQUIRE(out.size() == 1);
        CHECK(out[0].frame.get() == frame.get());
        CHECK(out[0].showAtQpc <= frame->arrivalQpc);
    }
}

TEST_CASE("strictly doubled input: the second copy of each picture is dropped") {
    Feed feed;
    feed.Arrive({1, 1, 2, 2, 3, 3});
    REQUIRE(feed.emitted.size() == 3);
    CHECK(feed.emitted[0]->pixels == Picture(1)->pixels);
    CHECK(feed.emitted[1]->pixels == Picture(2)->pixels);
    CHECK(feed.emitted[2]->pixels == Picture(3)->pixels);
}

TEST_CASE("repeats that differ by card noise under the tolerance are dropped and counted apart") {
    Feed feed(0.005);
    feed.Arrive(Picture(1));
    feed.Arrive(Picture(1));          // exact
    feed.Arrive(NoisyPicture(1, 2));  // near
    feed.Arrive(Picture(2));
    CHECK(feed.emitted.size() == 2);
    const gp::UniqueFrameTotals totals = feed.processor.Totals();
    CHECK(totals.arrived == 4);
    CHECK(totals.unique == 2);
    CHECK(totals.repeatedExact == 1);
    CHECK(totals.repeatedNear == 1);
    CHECK(totals.presented == 2);
    CHECK(totals.dropped == 2);
    CHECK(feed.processor.TakeCloseness().nearShareMax == doctest::Approx(0.002));
}

TEST_CASE("the default tolerance is exact match only: a noisy repeat is shown") {
    CHECK(gp::kNearDuplicateTolerance == 0.0);
    Feed feed;
    feed.Arrive(Picture(1));
    feed.Arrive(NoisyPicture(1, 2));
    CHECK(feed.emitted.size() == 2);
}

TEST_CASE("Unique Frames that were close to repeats are reported, so a tolerance can be chosen later") {
    Feed feed;  // exact only
    feed.Arrive(Picture(1));
    feed.Arrive(NoisyPicture(1, 3));  // 0.3% differ: shown, but close
    feed.Arrive(Picture(1));          // back to 1: shown, 0.3% from the noisy one
    feed.Arrive(Picture(2));          // a real change: not close
    gp::UniqueCloseness close = feed.processor.TakeCloseness();
    CHECK(close.closeUnique == 2);
    CHECK(close.closeShareMin == doctest::Approx(0.003));
    // Taken: the next period starts empty.
    close = feed.processor.TakeCloseness();
    CHECK(close.closeUnique == 0);
    CHECK(close.closeShareMin == 1.0);
}

TEST_CASE("rates: an all-unique game reads 60 / 60") {
    Feed feed;
    int picture = 0;
    feed.Game(3, 1, picture);
    CHECK(feed.Rates().signalRate == 60);
    CHECK(feed.Rates().gameRate == 60);
}

TEST_CASE("rates: a doubled game reads 30 / 60, and half the frames are presented") {
    Feed feed;
    int picture = 0;
    feed.Game(3, 2, picture);
    CHECK(feed.Rates().signalRate == 60);
    CHECK(feed.Rates().gameRate == 30);
    CHECK(feed.emitted.size() == 90);
}

TEST_CASE("rates: a tripled game reads 20, and 2-1 pacing (a 40 fps mode) reads 40") {
    Feed tripled;
    int picture = 0;
    tripled.Game(3, 3, picture);
    CHECK(tripled.Rates().gameRate == 20);

    Feed forty;
    for (int i = 0; i < 60; i++) forty.Arrive({i * 2, i * 2, i * 2 + 1});  // one picture for 2 signal frames, the next for 1
    CHECK(forty.Rates().gameRate == 40);
    CHECK(forty.emitted.size() == 120);
}

TEST_CASE("rates: uneven pacing gives a steady figure, not one that flickers every second") {
    Feed feed;
    int picture = 0;
    feed.Game(2, 2, picture);
    REQUIRE(feed.Rates().gameRate == 30);
    // Every half second one picture stays for three signal frames instead of
    // two: 15 pictures in 31 signal frames, about 29 per second.
    std::vector<int> readings;
    for (int round = 0; round < 12; round++) {
        for (int i = 0; i < 14; i++) feed.Arrive({++picture, picture});
        feed.Arrive({++picture, picture, picture});
        readings.push_back(feed.Rates().gameRate);
        CHECK(feed.Rates().signalRate == 60);
    }
    for (size_t i = 4; i < readings.size(); i++) CHECK(readings[i] == 29);
}

TEST_CASE("rates: a change from 30 to 60 mid-stream is followed within two seconds") {
    Feed feed;
    int picture = 0;
    feed.Game(3, 2, picture);
    CHECK(feed.Rates().gameRate == 30);
    feed.Game(1, 1, picture);
    CHECK(feed.Rates().gameRate > 40);
    feed.Game(1, 1, picture);
    CHECK(feed.Rates().gameRate == 60);
    CHECK(feed.Rates().signalRate == 60);
}

TEST_CASE("a fully static Feed reads Game Rate 0 with Signal Rate 60, and nothing is presented") {
    Feed feed;
    feed.Arrive(Picture(1));
    for (int i = 0; i < 600; i++) feed.Arrive(Picture(1));
    const gp::FrameRates rates = feed.Rates();
    CHECK(rates.signalRate == 60);
    CHECK(rates.gameRate == 0);
    CHECK(feed.emitted.size() == 1);
    CHECK(gp::FpsCounterText(rates) == L"0 / 60");
}

TEST_CASE("a long still run followed by motion: the first new picture is shown at once and the rate recovers") {
    Feed feed;
    for (int i = 0; i < 300; i++) feed.Arrive(Picture(1));
    CHECK(feed.Arrive(Picture(2)) == 1);
    int picture = 2;
    feed.Game(2, 2, picture);
    CHECK(feed.Rates().gameRate == 30);
}

TEST_CASE("no frames at all: rates are zero, no division by zero") {
    Feed feed;
    CHECK(feed.Rates().signalRate == 0);
    CHECK(feed.Rates().gameRate == 0);
    feed.Arrive(Picture(1));
    CHECK(feed.Rates().signalRate >= 0);
    // The card stops sending: once the window has passed the rates fall to zero.
    feed.now += 3 * kTicksPerSecond;
    CHECK(feed.Rates().signalRate == 0);
    CHECK(feed.Rates().gameRate == 0);
}

TEST_CASE("override 60: every captured frame is presented, repeats still counted") {
    Feed feed;
    feed.processor.SetOverride(GameRateOverride::Fps60);
    feed.Arrive({1, 1, 2, 2, 3, 3});
    CHECK(feed.emitted.size() == 6);
    CHECK(feed.processor.Totals().repeatedExact == 3);
    CHECK(feed.processor.Totals().dropped == 0);
}

TEST_CASE("override 30: a game whose repeats look different is held to every second frame") {
    Feed feed(0.0);
    feed.processor.SetOverride(GameRateOverride::Fps30);
    feed.Arrive({1, 2, 3, 4, 5, 6});
    REQUIRE(feed.emitted.size() == 3);
    CHECK(feed.emitted[0]->pixels == Picture(1)->pixels);
    CHECK(feed.emitted[1]->pixels == Picture(3)->pixels);
    CHECK(feed.emitted[2]->pixels == Picture(5)->pixels);
}

TEST_CASE("override 30 prefers Unique Frames: a doubled game out of step still shows each picture") {
    Feed feed;
    feed.processor.SetOverride(GameRateOverride::Fps30);
    feed.Arrive({1, 2, 2, 3, 3, 4, 4});
    REQUIRE(feed.emitted.size() == 4);
    CHECK(feed.emitted[1]->pixels == Picture(2)->pixels);
    CHECK(feed.emitted[3]->pixels == Picture(4)->pixels);
}

TEST_CASE("override 30 on a still picture presents nothing new") {
    Feed feed;
    feed.processor.SetOverride(GameRateOverride::Fps30);
    feed.Arrive({1, 1, 1, 1, 1, 1});
    CHECK(feed.emitted.size() == 1);
}

TEST_CASE("the override can change while running, and back to Auto") {
    Feed feed;
    CHECK(feed.processor.Override() == GameRateOverride::Auto);
    feed.Arrive({1, 1});
    feed.processor.SetOverride(GameRateOverride::Fps60);
    feed.Arrive({2, 2});
    feed.processor.SetOverride(GameRateOverride::Auto);
    feed.Arrive({3, 3});
    CHECK(feed.emitted.size() == 4);  // 1, 2, 2, 3
}

TEST_CASE("the last three Unique Frames are kept, newest first") {
    Feed feed;
    feed.Arrive({1, 1, 2, 3, 3, 4});
    const auto& recent = feed.processor.RecentUniqueFrames();
    REQUIRE(recent.size() == 3);
    CHECK(recent[0]->pixels == Picture(4)->pixels);
    CHECK(recent[1]->pixels == Picture(3)->pixels);
    CHECK(recent[2]->pixels == Picture(2)->pixels);
}

TEST_CASE("after Reset the next frame is shown even if it repeats the last picture") {
    Feed feed;
    feed.Arrive({1, 1});
    feed.processor.Reset();
    CHECK(feed.Arrive(Picture(1)) == 1);
}

TEST_CASE("the override is read from a command-line word") {
    GameRateOverride setting = GameRateOverride::Auto;
    CHECK(gp::ParseGameRateOverride(L"30", setting));
    CHECK(setting == GameRateOverride::Fps30);
    CHECK(gp::ParseGameRateOverride(L"60", setting));
    CHECK(setting == GameRateOverride::Fps60);
    CHECK(gp::ParseGameRateOverride(L"auto", setting));
    CHECK(setting == GameRateOverride::Auto);
    CHECK_FALSE(gp::ParseGameRateOverride(L"45", setting));
    CHECK(setting == GameRateOverride::Auto);
}

TEST_CASE("the FPS counter reads Game Rate / Signal Rate") {
    CHECK(gp::FpsCounterText({60, 30}) == L"30 / 60");
    CHECK(gp::FpsCounterText({60, 60}) == L"60 / 60");
    CHECK(gp::FpsCounterText({0, 0}) == L"0 / 0");
}

TEST_CASE("with tolerance zero the same noisy repeat is a Unique Frame") {
    Feed feed(0.0);
    feed.Arrive(Picture(1));
    feed.Arrive(NoisyPicture(1, 2));
    CHECK(feed.emitted.size() == 2);
    CHECK(feed.processor.Totals().repeatedNear == 0);
}

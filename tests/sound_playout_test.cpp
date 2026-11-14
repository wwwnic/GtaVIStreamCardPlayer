#include "doctest.h"

#include <cmath>
#include <vector>

#include "core/sound_playout.h"

using gp::SoundPlayout;

namespace {
// Stereo ramp: frame n has left n / 1000 and right -n / 1000.
std::vector<float> Ramp(size_t first, size_t count) {
    std::vector<float> samples;
    for (size_t n = first; n < first + count; n++) {
        samples.push_back(n / 1000.0f);
        samples.push_back(-(n / 1000.0f));
    }
    return samples;
}

bool AllSilent(const std::vector<float>& samples) {
    for (float s : samples) {
        if (s != 0.0f) return false;
    }
    return true;
}

const size_t kTargetFrames48k = static_cast<size_t>(SoundPlayout::kTargetFillSeconds * 48000);
}  // namespace

TEST_CASE("playback stays silent until the target fill would remain after the period, then plays the sound in order") {
    SoundPlayout playout(48000, 48000);
    std::vector<float> out(2 * 100);
    playout.Push(Ramp(0, kTargetFrames48k + 50).data(), kTargetFrames48k + 50);
    playout.Pull(out.data(), 100);
    CHECK(AllSilent(out));

    playout.Push(Ramp(kTargetFrames48k + 50, 60).data(), 60);
    playout.Pull(out.data(), 100);
    CHECK(out[0] == doctest::Approx(0.0));
    CHECK(out[1] == doctest::Approx(0.0));
    CHECK(out[2 * 99] == doctest::Approx(0.099).epsilon(0.001));
    CHECK(out[2 * 99 + 1] == doctest::Approx(-0.099).epsilon(0.001));
}

TEST_CASE("when the buffer runs dry (underrun) playback goes silent and waits for the target fill again") {
    SoundPlayout playout(48000, 48000);
    std::vector<float> out(2 * 480);
    playout.Push(Ramp(1, kTargetFrames48k + 490).data(), kTargetFrames48k + 490);
    playout.Pull(out.data(), 480);
    CHECK_FALSE(AllSilent(out));
    playout.Pull(out.data(), 480);  // takes the reserve
    CHECK_FALSE(AllSilent(out));
    CHECK(playout.TakeStats().underruns == 0);
    playout.Pull(out.data(), 480);  // nothing left
    CHECK(AllSilent(out));
    CHECK(playout.TakeStats().underruns == 1);

    playout.Push(Ramp(1, 600).data(), 600);  // enough for a period, but not with the reserve
    playout.Pull(out.data(), 480);
    CHECK(AllSilent(out));
    playout.Push(Ramp(1, kTargetFrames48k).data(), kTargetFrames48k);
    playout.Pull(out.data(), 480);
    CHECK_FALSE(AllSilent(out));
    CHECK(playout.TakeStats().underruns == 0);  // counted once, then reset by TakeStats
}

TEST_CASE("when sound piles up (overrun) the buffer drops back to the target, keeping the newest sound") {
    SoundPlayout playout(48000, 48000);
    std::vector<float> out(2 * 10);
    const size_t lots = 48000;  // a whole second arrives with nothing played
    playout.Push(Ramp(0, lots).data(), lots);
    const SoundPlayout::Stats stats = playout.TakeStats();
    CHECK(stats.overruns == 1);
    playout.Push(Ramp(lots, 12).data(), 12);  // one more packet, enough to start
    playout.Pull(out.data(), 10);
    CHECK(out[0] == doctest::Approx((lots - kTargetFrames48k) / 1000.0));
    CHECK(playout.TakeStats().fillAverageSeconds == doctest::Approx(SoundPlayout::kTargetFillSeconds).epsilon(0.02));
}

namespace {
struct Session {
    unsigned underruns = 0;
    unsigned overruns = 0;
    double worstFillError = 0.0;  // seconds, after the first minute
};

// Capture delivers 10 ms packets on the card's clock (`cardSpeed` times
// nominal); playback pulls 10 ms periods on the output device's clock.
Session Simulate(uint32_t inputRate, uint32_t outputRate, double cardSpeed, int minutes) {
    SoundPlayout playout(inputRate, outputRate);
    Session session;
    std::vector<float> in(2 * inputRate / 50, 0.25f);
    std::vector<float> out(2 * outputRate / 100);
    double owed = 0.0;
    const int periods = minutes * 60 * 100;
    for (int i = 0; i < periods; i++) {
        owed += inputRate / 100.0 * cardSpeed;
        if (owed >= inputRate / 100.0) {  // the packet arrives whole
            const size_t frames = static_cast<size_t>(owed);
            playout.Push(in.data(), frames);
            owed -= static_cast<double>(frames);
        }
        playout.Pull(out.data(), outputRate / 100);
        if (i % 100 == 99) {
            const SoundPlayout::Stats stats = playout.TakeStats();
            if (i > 3 * 6000) {  // stats after the first three minutes (one is spent at the start)
                session.underruns += stats.underruns;
                session.overruns += stats.overruns;
                session.worstFillError = std::fmax(session.worstFillError,
                                                   std::fabs(stats.fillAverageSeconds - SoundPlayout::kTargetFillSeconds));
            }
        }
    }
    return session;
}
}  // namespace

TEST_CASE("with the card's clock 300 ppm fast, a long session holds the fill at its target without glitches") {
    const Session session = Simulate(48000, 48000, 1.0003, 20);
    CHECK(session.underruns == 0);
    CHECK(session.overruns == 0);
    CHECK(session.worstFillError < 0.005);
}

TEST_CASE("with the card's clock 300 ppm slow, a long session holds the fill too") {
    const Session session = Simulate(48000, 48000, 0.9997, 20);
    CHECK(session.underruns == 0);
    CHECK(session.overruns == 0);
    CHECK(session.worstFillError < 0.005);
}

TEST_CASE("48 kHz capture plays on a 44.1 kHz output without glitches, and the other way round") {
    const Session down = Simulate(48000, 44100, 1.0001, 10);
    CHECK(down.underruns == 0);
    CHECK(down.overruns == 0);
    CHECK(down.worstFillError < 0.005);
    const Session up = Simulate(44100, 48000, 0.9999, 10);
    CHECK(up.underruns == 0);
    CHECK(up.overruns == 0);
    CHECK(up.worstFillError < 0.005);
}

TEST_CASE("the reported playback rate stays within the inaudible correction bound") {
    SoundPlayout playout(48000, 48000);
    std::vector<float> in(2 * 960, 0.1f);
    std::vector<float> out(2 * 480);
    double worst = 0.0;
    for (int i = 0; i < 6000; i++) {
        if (i % 2 == 0) playout.Push(in.data(), 960);
        playout.Pull(out.data(), 470);  // output 2% slow: the fill keeps rising, overruns follow
        worst = std::fmax(worst, std::fabs(playout.TakeStats().rate - 1.0));
    }
    CHECK(worst > 0.0);
    CHECK(worst <= gp::DriftCorrection::kMaxCorrection + 1e-9);
}

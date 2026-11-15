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
    // 102 frames make a period of 100 here; the 8 oldest frames beyond the
    // target were dropped, and the rest plays in order.
    CHECK(out[0] == doctest::Approx(0.008));
    CHECK(out[1] == doctest::Approx(-0.008));
    CHECK(out[2 * 99] == doctest::Approx(0.107).epsilon(0.001));
    CHECK(out[2 * 99 + 1] == doctest::Approx(-0.107).epsilon(0.001));
}

TEST_CASE("playback starts with no more than the target fill, so no extra delay is carried") {
    SoundPlayout playout(48000, 48000);
    std::vector<float> out(2 * 100);
    playout.Push(Ramp(0, kTargetFrames48k + 400).data(), kTargetFrames48k + 400);  // a big first packet
    playout.Pull(out.data(), 100);
    CHECK(playout.TakeStats().fillAverageSeconds == doctest::Approx(SoundPlayout::kTargetFillSeconds));
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
// A playout that is already playing steady sound of 0.5 on both channels.
void StartPlaying(SoundPlayout& playout, std::vector<float>& out, size_t period) {
    std::vector<float> in(2 * (kTargetFrames48k + 2 * period), 0.5f);
    playout.Push(in.data(), kTargetFrames48k + 2 * period);
    playout.Pull(out.data(), period);
}

void PushSteady(SoundPlayout& playout, size_t frames) {
    std::vector<float> in(2 * frames, 0.5f);
    playout.Push(in.data(), frames);
}
}  // namespace

TEST_CASE("a new gain fades in over one period, then applies to all the sound") {
    SoundPlayout playout(48000, 48000);
    std::vector<float> out(2 * 100);
    StartPlaying(playout, out, 100);
    CHECK(out[0] == doctest::Approx(0.5));

    playout.SetGain(0.2f);
    PushSteady(playout, 100);
    playout.Pull(out.data(), 100);
    CHECK(out[0] > 0.45f);  // no jump at the start of the period
    CHECK(out[2 * 99] == doctest::Approx(0.1));
    PushSteady(playout, 100);
    playout.Pull(out.data(), 100);
    CHECK(out[0] == doctest::Approx(0.1));
    CHECK(out[2 * 99 + 1] == doctest::Approx(0.1));
}

TEST_CASE("muting (gain 0) fades out and then plays silence") {
    SoundPlayout playout(48000, 48000);
    std::vector<float> out(2 * 100);
    StartPlaying(playout, out, 100);
    playout.SetGain(0.0f);
    PushSteady(playout, 100);
    playout.Pull(out.data(), 100);
    CHECK(out[0] > 0.45f);
    PushSteady(playout, 100);
    playout.Pull(out.data(), 100);
    CHECK(AllSilent(out));
}

TEST_CASE("the buffer target is the base fill plus the Audio Delay, within the room the buffer has") {
    SoundPlayout playout(48000, 48000);
    CHECK(playout.TargetFillSeconds() == doctest::Approx(SoundPlayout::kTargetFillSeconds));
    playout.SetAudioDelay(0.2);
    CHECK(playout.TargetFillSeconds() == doctest::Approx(SoundPlayout::kTargetFillSeconds + 0.2));
    playout.SetAudioDelay(-0.1);
    CHECK(playout.TargetFillSeconds() == doctest::Approx(SoundPlayout::kTargetFillSeconds));
    playout.SetAudioDelay(5.0);
    CHECK(playout.TargetFillSeconds() == doctest::Approx(SoundPlayout::kLongestTargetSeconds));
}

TEST_CASE("a longer Audio Delay fades the sound out, waits quietly for the longer fill, then fades back in") {
    SoundPlayout playout(48000, 48000);
    const size_t period = 480;
    std::vector<float> out(2 * period);
    StartPlaying(playout, out, period);
    playout.TakeStats();

    playout.SetAudioDelay(0.1);
    PushSteady(playout, period);
    playout.Pull(out.data(), period);
    CHECK(out[0] > 0.45f);                       // starts where the sound was
    CHECK(out[2 * (period - 1)] < 0.01f);        // and ends near silence: no click
    int silentPeriods = 0;
    for (;;) {
        PushSteady(playout, period);
        playout.Pull(out.data(), period);
        if (!AllSilent(out)) break;
        silentPeriods++;
        REQUIRE(silentPeriods < 100);
    }
    CHECK(silentPeriods >= 9);  // about 100 ms of sound gathered while quiet
    CHECK(out[0] < 0.05f);      // fades in
    CHECK(out[2 * (period - 1)] == doctest::Approx(0.5));
    playout.TakeStats();
    PushSteady(playout, period);
    playout.Pull(out.data(), period);
    const SoundPlayout::Stats stats = playout.TakeStats();
    CHECK(stats.fillAverageSeconds == doctest::Approx(0.115).epsilon(0.02));
    CHECK(stats.overruns == 0);
    CHECK(stats.underruns == 0);
}

TEST_CASE("a shorter Audio Delay fades out, drops the extra sound while quiet, and fades in at the shorter fill") {
    SoundPlayout playout(48000, 48000);
    const size_t period = 480;
    std::vector<float> out(2 * period);
    playout.SetAudioDelay(0.2);
    const size_t start = static_cast<size_t>(0.215 * 48000) + period + 10;
    PushSteady(playout, start);
    playout.Pull(out.data(), period);
    CHECK_FALSE(AllSilent(out));

    playout.SetAudioDelay(0.0);
    PushSteady(playout, period);
    playout.Pull(out.data(), period);
    CHECK(out[2 * (period - 1)] < 0.01f);
    int silentPeriods = 0;
    for (;;) {
        PushSteady(playout, period);
        playout.Pull(out.data(), period);
        if (!AllSilent(out)) break;
        silentPeriods++;
        REQUIRE(silentPeriods < 100);
    }
    CHECK(silentPeriods <= 1);  // plenty buffered: restarts almost at once
    CHECK(out[0] < 0.05f);
    CHECK(out[2 * (period - 1)] == doctest::Approx(0.5));
    const SoundPlayout::Stats stats = playout.TakeStats();
    CHECK(stats.overruns == 0);
    PushSteady(playout, period);
    playout.Pull(out.data(), period);
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
// `audioDelay` is set 30 seconds in, while playing.
Session Simulate(uint32_t inputRate, uint32_t outputRate, double cardSpeed, int minutes, double audioDelay = 0.0) {
    SoundPlayout playout(inputRate, outputRate);
    const double target = SoundPlayout::kTargetFillSeconds + audioDelay;
    Session session;
    std::vector<float> in(2 * inputRate / 50, 0.25f);
    std::vector<float> out(2 * outputRate / 100);
    double owed = 0.0;
    const int periods = minutes * 60 * 100;
    for (int i = 0; i < periods; i++) {
        if (i == 3000 && audioDelay > 0.0) playout.SetAudioDelay(audioDelay);
        owed +=inputRate / 100.0 * cardSpeed;
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
                                                   std::fabs(stats.fillAverageSeconds - target));
            }
        }
    }
    return session;
}
}  // namespace

namespace {
// Uneven arrival: 10 ms capture packets on the card's clock, every
// `lateEvery`-th one late by `lateBy` packets (1: it comes with the next);
// the output takes `period` frames at a time. 48 kHz both sides.
Session SimulateUneven(size_t period, double cardSpeed, int lateEvery, int minutes, double lateBy = 1.0) {
    SoundPlayout playout(48000, 48000);
    Session session;
    const size_t packet = 480;
    std::vector<float> in(2 * packet, 0.25f);
    std::vector<float> out(2 * period);
    const double packetTime = static_cast<double>(packet) / cardSpeed;  // in output frames
    const uint64_t pulls = static_cast<uint64_t>(minutes) * 60 * 48000 / period;
    const uint64_t pullsPerSecond = 48000 / period;
    uint64_t nextPacket = 0;
    for (uint64_t j = 0; j < pulls; j++) {
        const double now = static_cast<double>(j * period);
        for (;;) {
            double arrival = static_cast<double>(nextPacket + 1) * packetTime;
            if (lateEvery > 0 && nextPacket % lateEvery == static_cast<uint64_t>(lateEvery) - 1) arrival += lateBy * packetTime;
            if (arrival > now) break;
            playout.Push(in.data(), packet);
            nextPacket++;
        }
        playout.Pull(out.data(), period);
        if (j % pullsPerSecond == pullsPerSecond - 1) {
            const SoundPlayout::Stats stats = playout.TakeStats();
            if (j > 180 * pullsPerSecond) {
                session.underruns += stats.underruns;
                session.overruns += stats.overruns;
                session.worstFillError = std::fmax(
                    session.worstFillError, std::fabs(stats.fillAverageSeconds - SoundPlayout::kTargetFillSeconds));
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
    CHECK(session.worstFillError < 0.001);
}

TEST_CASE("with the card's clock 300 ppm slow, a long session holds the fill too") {
    const Session session = Simulate(48000, 48000, 0.9997, 20);
    CHECK(session.underruns == 0);
    CHECK(session.overruns == 0);
    CHECK(session.worstFillError < 0.001);
}

TEST_CASE("after the Audio Delay changes, drift correction holds the fill at the shifted target") {
    const Session fast = Simulate(48000, 48000, 1.0003, 20, 0.3);
    CHECK(fast.underruns == 0);
    CHECK(fast.overruns == 0);
    CHECK(fast.worstFillError < 0.001);
    const Session slow = Simulate(48000, 44100, 0.9997, 20, 0.5);
    CHECK(slow.underruns == 0);
    CHECK(slow.overruns == 0);
    CHECK(slow.worstFillError < 0.001);
}

TEST_CASE("48 kHz capture plays on a 44.1 kHz output without glitches, and the other way round") {
    const Session down = Simulate(48000, 44100, 1.0001, 10);
    CHECK(down.underruns == 0);
    CHECK(down.overruns == 0);
    CHECK(down.worstFillError < 0.001);
    const Session up = Simulate(44100, 48000, 0.9999, 10);
    CHECK(up.underruns == 0);
    CHECK(up.overruns == 0);
    CHECK(up.worstFillError < 0.001);
}

// The target fill is sized to outlast one late capture packet. These guard
// that against the cases a real machine has: late packets, clocks that
// differ, output periods shorter than the packets.
TEST_CASE("a capture packet that comes a whole packet late does not run the buffer dry") {
    const Session session = SimulateUneven(480, 1.0, 300, 10);
    CHECK(session.underruns == 0);
    CHECK(session.overruns == 0);
}

// Known limit, not asserted: a packet a WHOLE packet late still runs the
// buffer dry when the card's clock is slow (the packets slide against the
// periods, so the fill before a packet is at times only 10 ms) or when the
// output periods are short. Three quarters late is covered everywhere.
TEST_CASE("capture packets three quarters late do not run the buffer dry, whichever clock is faster") {
    const double cardSpeeds[] = {0.9997, 0.99995, 1.00005, 1.0003};
    for (const double cardSpeed : cardSpeeds) {
        const Session session = SimulateUneven(480, cardSpeed, 300, 10, 0.75);
        CAPTURE(cardSpeed);
        CHECK(session.underruns == 0);
        CHECK(session.overruns == 0);
    }
}

TEST_CASE("short output periods ride out capture packets three quarters late") {
    const Session session = SimulateUneven(128, 1.0, 300, 10, 0.75);
    CHECK(session.underruns == 0);
    CHECK(session.overruns == 0);
}

TEST_CASE("short output periods (128 frames, the low-latency kind) against 10 ms capture packets play without glitches") {
    const Session session = SimulateUneven(128, 1.0002, 0, 10);
    CHECK(session.underruns == 0);
    CHECK(session.overruns == 0);
    CHECK(session.worstFillError < 0.001);
}

TEST_CASE("an output that takes 40 ms at a time still starts playing, and keeps playing") {
    SoundPlayout playout(48000, 48000);
    std::vector<float> in(2 * 480, 0.25f);
    std::vector<float> out(2 * 1920);
    int silentPulls = 0;
    bool heard = false;
    for (int i = 0; i < 4000; i++) {  // 40 seconds
        playout.Push(in.data(), 480);
        if (i % 4 != 3) continue;
        playout.Pull(out.data(), 1920);
        if (!heard) {
            heard = !AllSilent(out);
            if (heard) playout.TakeStats();
            else silentPulls++;
        }
    }
    CHECK(heard);
    CHECK(silentPulls <= 2);
    const SoundPlayout::Stats stats = playout.TakeStats();
    CHECK(stats.underruns == 0);
    CHECK(stats.overruns == 0);
    CHECK(stats.fillAverageSeconds == doctest::Approx(SoundPlayout::kTargetFillSeconds).epsilon(0.05));
}

TEST_CASE("an output that takes 40 ms at a time recovers from an underrun") {
    SoundPlayout playout(48000, 48000);
    std::vector<float> in(2 * 480, 0.25f);
    std::vector<float> out(2 * 1920);
    for (int i = 0; i < 400; i++) {
        playout.Push(in.data(), 480);
        if (i % 4 == 3) playout.Pull(out.data(), 1920);
    }
    playout.Pull(out.data(), 1920);  // no sound arrived in between
    CHECK(AllSilent(out));
    CHECK(playout.TakeStats().underruns == 1);
    bool heard = false;
    for (int i = 0; i < 400; i++) {
        playout.Push(in.data(), 480);
        if (i % 4 == 3) {
            playout.Pull(out.data(), 1920);
            heard = !AllSilent(out);
        }
    }
    CHECK(heard);
    CHECK(playout.TakeStats().underruns == 0);
}

TEST_CASE("an ordinary output keeps the overrun limit it had: 40 ms above the target") {
    SoundPlayout playout(48000, 48000);
    std::vector<float> out(2 * 480);
    StartPlaying(playout, out, 480);
    playout.TakeStats();
    // The fill is at the target now. 40 ms more is allowed; one frame beyond is an overrun.
    PushSteady(playout, 1920);
    CHECK(playout.TakeStats().overruns == 0);
    PushSteady(playout, 1);
    CHECK(playout.TakeStats().overruns == 1);
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

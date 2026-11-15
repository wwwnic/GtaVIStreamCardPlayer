#include "doctest.h"

#include <cmath>
#include <vector>

#include "core/resampler.h"

using gp::Resampler;

namespace {
// Runs `outFrames` through the resampler in chunks of `chunk`, feeding the
// stereo signal produced by `signal(frameNumber)` as input is asked for.
template <typename Signal>
std::vector<float> Run(Resampler& resampler, double step, size_t outFrames, size_t chunk, Signal signal,
                       size_t* inputUsed = nullptr) {
    std::vector<float> out;
    std::vector<float> in;
    size_t next = 0;
    for (size_t done = 0; done < outFrames; done += chunk) {
        const size_t needed = resampler.InputNeeded(chunk, step);
        in.clear();
        for (size_t i = 0; i < needed; i++, next++) {
            in.push_back(signal(next));
            in.push_back(-signal(next));
        }
        std::vector<float> part(2 * chunk);
        resampler.Process(in.data(), needed, part.data(), chunk, step);
        out.insert(out.end(), part.begin(), part.end());
    }
    if (inputUsed) *inputUsed = next;
    return out;
}

const double kPi = 3.14159265358979323846;
}  // namespace

TEST_CASE("at a step of exactly 1 the resampler passes the sound through unchanged") {
    Resampler resampler;
    const auto out = Run(resampler, 1.0, 300, 64, [](size_t n) { return static_cast<float>(n) * 0.001f; });
    for (size_t k = 0; k < 300; k++) {
        CHECK(out[2 * k] == doctest::Approx(k * 0.001));
        CHECK(out[2 * k + 1] == doctest::Approx(-(k * 0.001)));
    }
}

TEST_CASE("a step of one half doubles the rate, placing new frames halfway between old ones") {
    Resampler resampler;
    const auto out = Run(resampler, 0.5, 200, 32, [](size_t n) { return static_cast<float>(n); });
    for (size_t k = 4; k < 200; k++) CHECK(out[2 * k] == doctest::Approx(k * 0.5));
}

TEST_CASE("over many periods the resampler consumes input at exactly its step") {
    Resampler resampler;
    size_t used = 0;
    const double step = 44100.0 / 48000.0 * 1.0007;
    Run(resampler, step, 48000, 480, [](size_t) { return 0.0f; }, &used);
    CHECK(std::fabs(static_cast<double>(used) - 48000 * step) < 4.0);
}

TEST_CASE("a slightly sped-up tone stays a clean tone") {
    Resampler resampler;
    const double step = 1.001;
    const double frequency = 1000.0 / 48000.0;  // cycles per input frame
    const auto out = Run(resampler, step, 4800, 100,
                         [&](size_t n) { return static_cast<float>(std::sin(2 * kPi * frequency * n)); });
    double worst = 0.0;
    for (size_t k = 8; k < 4800; k++) {
        const double expected = std::sin(2 * kPi * frequency * (k * step));
        worst = std::fmax(worst, std::fabs(out[2 * k] - expected));
    }
    CHECK(worst < 0.001);
}

TEST_CASE("a large step (48 kHz to 8 kHz) asks for all the input it skips, period after period") {
    Resampler resampler;
    size_t used = 0;
    const auto out = Run(resampler, 6.0, 800, 80, [](size_t n) { return static_cast<float>(n) * 0.001f; }, &used);
    for (size_t k = 0; k < 800; k++) CHECK(out[2 * k] == doctest::Approx(k * 6 * 0.001));
    CHECK(std::fabs(static_cast<double>(used) - 800 * 6.0) < 8.0);
}

TEST_CASE("a large uneven step (48 kHz to 11.025 kHz) consumes input at exactly its step") {
    Resampler resampler;
    size_t used = 0;
    const double step = 48000.0 / 11025.0;
    Run(resampler, step, 11000, 110, [](size_t) { return 0.0f; }, &used);
    CHECK(std::fabs(static_cast<double>(used) - 11000 * step) < 8.0);
}

TEST_CASE("after a reset the resampler starts a fresh stream") {
    Resampler resampler;
    Run(resampler, 0.9, 100, 10, [](size_t) { return 1.0f; });
    resampler.Reset();
    const auto out = Run(resampler, 1.0, 10, 10, [](size_t n) { return static_cast<float>(n + 5); });
    CHECK(out[0] == doctest::Approx(5.0));
}

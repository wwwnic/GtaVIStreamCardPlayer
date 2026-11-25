#include "doctest.h"

#include <cmath>

#include "core/night_mode.h"

using gp::NightMode;

namespace {
// The shader's curve (platform/shadow_curve.h), on one grey value.
float Lift(float value, float strength) {
    if (strength == 0 || value <= 0 || value >= gp::kNightModeKnee) return value;
    const float t = value / gp::kNightModeKnee;
    const float weight = 1 - t * t * (3 - 2 * t);  // 1 - smoothstep(0, knee, value)
    return value + (std::sqrt(value) - value) * strength * weight;
}
}  // namespace

TEST_CASE("N cycles night mode off, low, high, auto and back to off") {
    CHECK(gp::NextNightMode(NightMode::Off) == NightMode::Low);
    CHECK(gp::NextNightMode(NightMode::Low) == NightMode::High);
    CHECK(gp::NextNightMode(NightMode::High) == NightMode::Auto);
    CHECK(gp::NextNightMode(NightMode::Auto) == NightMode::Off);
}

TEST_CASE("off changes nothing; high lifts more than low") {
    CHECK(gp::NightModeStrength(NightMode::Off) == 0.0f);
    CHECK(gp::NightModeStrength(NightMode::Low) > 0.0f);
    CHECK(gp::NightModeStrength(NightMode::High) > gp::NightModeStrength(NightMode::Low));
    CHECK(gp::NightModeStrength(NightMode::High) <= 1.0f);
}

TEST_CASE("every night mode survives its word") {
    for (NightMode mode : {NightMode::Off, NightMode::Low, NightMode::High, NightMode::Auto})
        CHECK(gp::NightModeOf(gp::NightModeWord(mode)) == mode);
    CHECK_FALSE(gp::NightModeOf("dark").has_value());
    CHECK_FALSE(gp::NightModeOf("").has_value());
}

TEST_CASE("the curve lifts shadows, keeps black, bright and order") {
    const float high = gp::NightModeStrength(NightMode::High);
    CHECK(Lift(0.0f, high) == 0.0f);
    CHECK(Lift(0.1f, high) > 0.1f);
    CHECK(Lift(0.8f, high) == 0.8f);
    CHECK(Lift(1.0f, high) == 1.0f);
    float previous = 0;
    for (int i = 1; i <= 100; i++) {  // never darker, never out of order, never above 1
        const float value = i / 100.0f;
        const float lifted = Lift(value, high);
        CHECK(lifted >= value);
        CHECK(lifted >= previous);
        CHECK(lifted <= 1.0f);
        previous = lifted;
    }
}

TEST_CASE("auto night mode: full in a dark scene, none in daylight, sliding in between") {
    gp::AutoNightMode dark;
    CHECK(dark.Update(30, 0) == 1.0f);  // the first look is taken as it is
    gp::AutoNightMode day;
    CHECK(day.Update(140, 0) == 0.0f);
    gp::AutoNightMode dusk;
    const float half = dusk.Update(static_cast<int>((gp::kAutoNightDark + gp::kAutoNightBright) / 2), 0);
    CHECK(half == doctest::Approx(0.5f).epsilon(0.05));
}

TEST_CASE("auto night mode follows a change of scene in a few seconds, not at once") {
    gp::AutoNightMode night;
    uint64_t now = 0;
    CHECK(night.Update(140, now) == 0.0f);  // daylight
    for (int i = 0; i < 4; i++) night.Update(30, now += 250);  // one second of night: not yet
    CHECK(night.Strength() == 0.0f);
    for (int i = 0; i < 8; i++) night.Update(30, now += 250);  // three seconds: on its way
    CHECK(night.Strength() > 0.0f);
    CHECK(night.Strength() < 1.0f);
    for (int i = 0; i < 60; i++) night.Update(30, now += 250);  // fifteen seconds
    CHECK(night.Strength() == doctest::Approx(1.0f).epsilon(0.01));
}

TEST_CASE("auto night mode: a quarter-second flash barely moves it, no picture holds it") {
    gp::AutoNightMode night;
    uint64_t now = 0;
    night.Update(30, now);
    night.Update(230, now += 250);  // an explosion, a headlight
    CHECK(night.Strength() > 0.7f);
    const float held = night.Update(-1, now += 250);
    CHECK(held == night.Strength());
    CHECK(night.Update(-1, now += 5000) == held);
    night.Reset();
    CHECK(night.Strength() == 0.0f);
    CHECK(night.SceneLuma() < 0);
}

TEST_CASE("auto night mode never leaves 0..1, whatever the luma or the clock") {
    gp::AutoNightMode night;
    uint64_t now = 0;
    for (int luma : {0, 255, 16, 235, 95, 45, 0, 255}) {
        const float strength = night.Update(luma, now += 1000);
        CHECK(strength >= 0.0f);
        CHECK(strength <= 1.0f);
    }
    CHECK(night.Update(0, now) >= 0.0f);  // the same time twice: no step
    CHECK(night.Update(0, now - 500) <= 1.0f);  // a clock going back: no step
}

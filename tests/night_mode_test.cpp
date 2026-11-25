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

TEST_CASE("N cycles night mode off, low, high and back to off") {
    CHECK(gp::NextNightMode(NightMode::Off) == NightMode::Low);
    CHECK(gp::NextNightMode(NightMode::Low) == NightMode::High);
    CHECK(gp::NextNightMode(NightMode::High) == NightMode::Off);
}

TEST_CASE("off changes nothing; high lifts more than low") {
    CHECK(gp::NightModeStrength(NightMode::Off) == 0.0f);
    CHECK(gp::NightModeStrength(NightMode::Low) > 0.0f);
    CHECK(gp::NightModeStrength(NightMode::High) > gp::NightModeStrength(NightMode::Low));
    CHECK(gp::NightModeStrength(NightMode::High) <= 1.0f);
}

TEST_CASE("every night mode survives its word") {
    for (NightMode mode : {NightMode::Off, NightMode::Low, NightMode::High})
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

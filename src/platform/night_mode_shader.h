// Night mode's curve (core/night_mode.h), prepended to every Presenter
// shader. The last pass that writes the picture (Show, or RCAS with Sharp
// Upscale) calls it, so real and Generated Frames look the same; Frame
// Generation's inputs stay untouched and the text overlay is drawn after.
// Strength comes from constant buffer b1; unbound it reads 0: no change.
// Keep the knee (0.65) equal to kNightModeKnee; tests/night_mode_test.cpp
// checks the same curve on the CPU.
#pragma once

namespace gp {

inline constexpr char kNightModeShader[] = R"hlsl(
cbuffer NightMode : register(b1) { float nightStrength; };
float3 BoostShadows(float3 rgb) {
    float peak = max(rgb.r, max(rgb.g, rgb.b));
    if (nightStrength == 0 || peak <= 0 || peak >= 0.65) return rgb;
    float weight = 1 - smoothstep(0.0, 0.65, peak);
    float lifted = lerp(peak, sqrt(peak), nightStrength * weight);
    return rgb * (lifted / peak);
}
)hlsl";

}  // namespace gp

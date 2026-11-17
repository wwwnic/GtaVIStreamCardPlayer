// FidelityFX Super Resolution 1, adapted from AMD's published EASU and RCAS.
// https://github.com/GPUOpen-Effects/FidelityFX-FSR/blob/master/ffx-fsr/ffx_fsr1.h
// Copyright (c) 2021 Advanced Micro Devices, Inc. All rights reserved.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
#pragma once

namespace gp {
inline constexpr char kSharpUpscaleShaders[] = R"hlsl(
struct VertexOut { float4 position : SV_Position; float2 uv : TEXCOORD0; };
Texture2D<float4> picture : register(t2);
float3 P(int2 p, int2 size) { return picture.Load(int3(clamp(p, int2(0, 0), size - 1), 0)).rgb; }
float Luma(float3 c) { return c.b * 0.5 + (c.r * 0.5 + c.g); }

// One '+' of five lumas (A above, B left, C centre, D right, E below), weighted w.
void EasuSet(inout float2 dir, inout float len, float w, float lA, float lB, float lC, float lD, float lE) {
    float lenX = 1.0 / max(max(abs(lD - lC), abs(lC - lB)), 1e-5);
    float dirX = lD - lB;
    dir.x += dirX * w;
    lenX = saturate(abs(dirX) * lenX);
    len += lenX * lenX * w;
    float lenY = 1.0 / max(max(abs(lE - lC), abs(lC - lA)), 1e-5);
    float dirY = lE - lA;
    dir.y += dirY * w;
    lenY = saturate(abs(dirY) * lenY);
    len += lenY * lenY * w;
}

void EasuTap(inout float3 sum, inout float weight, float2 off, float2 dir, float2 len, float lob, float clp, float3 c) {
    float2 v = float2(off.x * dir.x + off.y * dir.y, off.x * -dir.y + off.y * dir.x) * len;
    float d2 = min(dot(v, v), clp);
    float wB = 2.0 / 5.0 * d2 - 1.0;
    float wA = lob * d2 - 1.0;
    wB *= wB;
    wA *= wA;
    wB = 25.0 / 16.0 * wB - (25.0 / 16.0 - 1.0);
    float w = wB * wA;
    sum += c * w;
    weight += w;
}

float4 Easu(VertexOut input) : SV_Target {
    float2 size;
    picture.GetDimensions(size.x, size.y);
    int2 n = int2(size);
    float2 pp = input.uv * size - 0.5;
    float2 fp = floor(pp);
    pp -= fp;
    int2 o = int2(fp);
    // 12 taps around the four nearest texels f g j k:
    //    b c
    //  e f g h
    //  i j k l
    //    m q
    float3 b = P(o + int2(0, -1), n), c = P(o + int2(1, -1), n);
    float3 e = P(o + int2(-1, 0), n), f = P(o, n), g = P(o + int2(1, 0), n), h = P(o + int2(2, 0), n);
    float3 i = P(o + int2(-1, 1), n), j = P(o + int2(0, 1), n), k = P(o + int2(1, 1), n), l = P(o + int2(2, 1), n);
    float3 m = P(o + int2(0, 2), n), q = P(o + int2(1, 2), n);
    float bL = Luma(b), cL = Luma(c), eL = Luma(e), fL = Luma(f), gL = Luma(g), hL = Luma(h);
    float iL = Luma(i), jL = Luma(j), kL = Luma(k), lL = Luma(l), mL = Luma(m), qL = Luma(q);

    float2 dir = 0;
    float len = 0;
    EasuSet(dir, len, (1 - pp.x) * (1 - pp.y), bL, eL, fL, gL, jL);
    EasuSet(dir, len, pp.x * (1 - pp.y), cL, fL, gL, hL, kL);
    EasuSet(dir, len, (1 - pp.x) * pp.y, fL, iL, jL, kL, mL);
    EasuSet(dir, len, pp.x * pp.y, gL, jL, kL, lL, qL);

    float dirR = dot(dir, dir);
    bool flat = dirR < 1.0 / 32768.0;
    dirR = flat ? 1.0 : rsqrt(dirR);
    dir.x = flat ? 1.0 : dir.x;
    dir *= dirR;
    len = len * 0.5;
    len *= len;
    float stretch = dot(dir, dir) / max(abs(dir.x), abs(dir.y));
    float2 len2 = float2(1.0 + (stretch - 1.0) * len, 1.0 - 0.5 * len);
    float lob = 0.5 + ((1.0 / 4.0 - 0.04) - 0.5) * len;
    float clp = 1.0 / lob;

    float3 low = min(min(f, g), min(j, k));
    float3 high = max(max(f, g), max(j, k));
    float3 sum = 0;
    float weight = 0;
    EasuTap(sum, weight, float2(0, -1) - pp, dir, len2, lob, clp, b);
    EasuTap(sum, weight, float2(1, -1) - pp, dir, len2, lob, clp, c);
    EasuTap(sum, weight, float2(-1, 1) - pp, dir, len2, lob, clp, i);
    EasuTap(sum, weight, float2(0, 1) - pp, dir, len2, lob, clp, j);
    EasuTap(sum, weight, float2(0, 0) - pp, dir, len2, lob, clp, f);
    EasuTap(sum, weight, float2(-1, 0) - pp, dir, len2, lob, clp, e);
    EasuTap(sum, weight, float2(1, 1) - pp, dir, len2, lob, clp, k);
    EasuTap(sum, weight, float2(2, 1) - pp, dir, len2, lob, clp, l);
    EasuTap(sum, weight, float2(2, 0) - pp, dir, len2, lob, clp, h);
    EasuTap(sum, weight, float2(1, 0) - pp, dir, len2, lob, clp, g);
    EasuTap(sum, weight, float2(1, 2) - pp, dir, len2, lob, clp, q);
    EasuTap(sum, weight, float2(0, 2) - pp, dir, len2, lob, clp, m);
    return float4(min(high, max(low, sum / weight)), 1);
}

// Here `picture` is the Easu result, already at the size shown.
float4 Rcas(VertexOut input) : SV_Target {
    float2 size;
    picture.GetDimensions(size.x, size.y);
    int2 n = int2(size);
    int2 p = int2(input.uv * size);
    float3 b = P(p + int2(0, -1), n), d = P(p + int2(-1, 0), n), e = P(p, n);
    float3 f = P(p + int2(1, 0), n), h = P(p + int2(0, 1), n);
    float3 low = min(min(b, d), min(f, h));
    float3 high = max(max(b, d), max(f, h));
    // A zero denominator imposes no limit on the lobe (as in the
    // reference max of the two limits, which ignores a single NaN).
    float3 hitMin = high > 0 ? min(low, e) / (4.0 * high) : 1.0;
    float3 hitMax = low < 1 ? (1.0 - max(high, e)) / (4.0 * low - 4.0) : -1.0;
    float3 lobeRgb = max(-hitMin, hitMax);
    float lobe = max(-0.1875, min(max(lobeRgb.r, max(lobeRgb.g, lobeRgb.b)), 0.0)) * exp2(-0.2);
    return float4(saturate((lobe * (b + d + f + h) + e) / (4.0 * lobe + 1.0)), 1);
}
)hlsl";
}  // namespace gp

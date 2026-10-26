// AgX tone mapping (Troy Sobotka's display transform; ToneMapping::AgX and AgXPunchy): very
// bright colors bleach towards white while keeping their hue, instead of turning flat yellow,
// cyan or magenta. This is "AgX Minimal" by Benjamin Wrensch (a polynomial fitted to AgX's
// curve), ported from GLSL to HLSL (matrices written row by row for mul(vector, matrix)), from
// https://iolite-engine.com/blog_posts/minimal_agx_implementation, under this license:
//
//   MIT License
//
//   Copyright (c) 2024 Missing Deadlines (Benjamin Wrensch)
//
//   Permission is hereby granted, free of charge, to any person obtaining a copy
//   of this software and associated documentation files (the "Software"), to deal
//   in the Software without restriction, including without limitation the rights
//   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//   copies of the Software, and to permit persons to whom the Software is
//   furnished to do so, subject to the following conditions:
//
//   The above copyright notice and this permission notice shall be included in
//   all copies or substantial portions of the Software.
//
//   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//   OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
//   SOFTWARE.
//
// All values used to derive this implementation are sourced from Troy's initial AgX
// implementation/OCIO config file: https://github.com/sobotka/AgX
#ifndef POLIGON_AGX_HLSLI
#define POLIGON_AGX_HLSLI

// Mean error^2: 3.6705141e-06
float3 AgxDefaultContrastApprox(float3 x) {
    float3 x2 = x * x;
    float3 x4 = x2 * x2;
    return 15.5 * x4 * x2 - 40.14 * x4 * x + 31.96 * x4 - 6.868 * x2 * x + 0.4298 * x2 + 0.1191 * x - 0.00232;
}

float3 Agx(float3 value) {
    const float3x3 agx_matrix = float3x3(0.842479062253094, 0.0423282422610123, 0.0423756549057051,
                                         0.0784335999999992, 0.878468636469772, 0.0784336,
                                         0.0792237451477643, 0.0791661274605434, 0.879142973793104);
    const float min_ev = -12.47393;
    const float max_ev = 4.026069;
    // Input transform (inset), then log2 space encoding.
    value = mul(max(value, 0.0), agx_matrix);
    value = clamp(log2(max(value, 1e-10)), min_ev, max_ev);
    value = (value - min_ev) / (max_ev - min_ev);
    // Sigmoid function approximation.
    return AgxDefaultContrastApprox(value);
}

float3 AgxEotf(float3 value) {
    const float3x3 agx_matrix_inverse = float3x3(1.19687900512017, -0.0528968517574562, -0.0529716355144438,
                                                 -0.0980208811401368, 1.15190312990417, -0.0980434501171241,
                                                 -0.0990297440797205, -0.0989611768448433, 1.15107367264116);
    // Inverse input transform (outset), then the 2.2 exponent back to linear: the target
    // encodes to sRGB when the value is stored.
    value = mul(value, agx_matrix_inverse);
    return pow(max(value, 0.0), 2.2);
}

// The "Punchy" look (ASC CDL): more contrast and saturation.
float3 AgxPunchy(float3 value) {
    value = pow(max(value, 0.0), 1.35);
    float luma = dot(value, float3(0.2126, 0.7152, 0.0722));
    return luma + 1.4 * (value - luma);
}

#endif

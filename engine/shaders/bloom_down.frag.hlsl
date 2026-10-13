// Bloom, first half: makes a half-size, blurred copy of an image. Run repeatedly, it gives
// a chain of ever smaller and blurrier images.
//
// The 13-sample pattern is the one from "Next Generation Post Processing in Call of Duty:
// Advanced Warfare" (Jimenez 2014): five overlapping 2 x 2 boxes, which does not flicker
// when small bright things move.

[[vk::combinedImageSampler]][[vk::binding(0, 2)]]
Texture2D<float4> source_texture : register(t0, space2);
[[vk::combinedImageSampler]][[vk::binding(0, 2)]]
SamplerState source_sampler : register(s0, space2);

cbuffer DownUniforms : register(b0, space3) {
    float4 parameters;  // xy: size of one source pixel in uv, z: 1 = tame single very bright pixels
};

struct FragmentInput {
    float4 clip_position : SV_Position;
    float2 uv : TEXCOORD0;
    float2 ndc : TEXCOORD1;
};

float3 Tap(float2 uv, float x, float y) {
    return source_texture.SampleLevel(source_sampler, uv + float2(x, y) * parameters.xy, 0.0).rgb;
}

// Average of four colors in which very bright ones count less. Without it one glinting pixel
// would turn into a large flickering blob.
float3 TamedAverage(float3 a, float3 b, float3 c, float3 d) {
    static const float3 kLuminance = float3(0.2126, 0.7152, 0.0722);
    float wa = 1.0 / (1.0 + dot(a, kLuminance));
    float wb = 1.0 / (1.0 + dot(b, kLuminance));
    float wc = 1.0 / (1.0 + dot(c, kLuminance));
    float wd = 1.0 / (1.0 + dot(d, kLuminance));
    return (a * wa + b * wb + c * wc + d * wd) / (wa + wb + wc + wd);
}

float4 main(FragmentInput input) : SV_Target0 {
    float2 uv = input.uv;
    float3 a = Tap(uv, -2, -2);
    float3 b = Tap(uv, 0, -2);
    float3 c = Tap(uv, 2, -2);
    float3 d = Tap(uv, -2, 0);
    float3 e = Tap(uv, 0, 0);
    float3 f = Tap(uv, 2, 0);
    float3 g = Tap(uv, -2, 2);
    float3 h = Tap(uv, 0, 2);
    float3 i = Tap(uv, 2, 2);
    float3 j = Tap(uv, -1, -1);
    float3 k = Tap(uv, 1, -1);
    float3 l = Tap(uv, -1, 1);
    float3 m = Tap(uv, 1, 1);

    float3 color;
    if (parameters.z > 0.5) {
        color = TamedAverage(j, k, l, m) * 0.5 + TamedAverage(a, b, d, e) * 0.125 + TamedAverage(b, c, e, f) * 0.125 +
                TamedAverage(d, e, g, h) * 0.125 + TamedAverage(e, f, h, i) * 0.125;
    } else {
        color = (j + k + l + m) * 0.125 + (a + c + g + i) * 0.03125 + (b + d + f + h) * 0.0625 + e * 0.125;
    }
    // Guards the chain against a broken (not-a-number) pixel spreading over the whole image.
    return float4(max(color, 0.0), 1.0);
}

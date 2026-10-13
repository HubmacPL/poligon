// Smooths the noisy ambient occlusion image along one axis (run twice: across, then down).
// Neighbors at a clearly different depth are left out, so that the darkening of one object
// does not smear over the outline of another.
#include "common.hlsli"

[[vk::combinedImageSampler]][[vk::binding(0, 2)]]
Texture2D<float> ao_texture : register(t0, space2);
[[vk::combinedImageSampler]][[vk::binding(0, 2)]]
SamplerState ao_sampler : register(s0, space2);

[[vk::combinedImageSampler]][[vk::binding(1, 2)]]
Texture2D<float> depth_texture : register(t1, space2);
[[vk::combinedImageSampler]][[vk::binding(1, 2)]]
SamplerState depth_sampler : register(s1, space2);

cbuffer BlurUniforms : register(b0, space3) {
    float4 projection_info;  // see ViewDistance in common.hlsli
    float4 direction;        // xy: step in pixels, (1, 0) or (0, 1); zw: image size in pixels
};

struct FragmentInput {
    float4 clip_position : SV_Position;
    float2 uv : TEXCOORD0;
    float2 ndc : TEXCOORD1;
};

float main(FragmentInput input) : SV_Target0 {
    static const float kWeights[4] = {0.324, 0.232, 0.0855, 0.0205};
    int2 pixel = int2(input.clip_position.xy);
    int2 last_pixel = int2(direction.zw) - 1;
    float center_distance = ViewDistance(depth_texture.Load(int3(pixel, 0)), projection_info);

    float sum = 0.0;
    float weight_sum = 0.0;
    for (int i = -3; i <= 3; ++i) {
        int2 tap = clamp(pixel + int2(direction.xy) * i, int2(0, 0), last_pixel);
        float tap_distance = ViewDistance(depth_texture.Load(int3(tap, 0)), projection_info);
        float depth_weight = saturate(1.0 - abs(tap_distance - center_distance) / (0.05 * center_distance + 0.02));
        float weight = kWeights[abs(i)] * depth_weight;
        sum += ao_texture.Load(int3(tap, 0)) * weight;
        weight_sum += weight;
    }
    return sum / max(weight_sum, 0.0001);
}

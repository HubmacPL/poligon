// Puts the half-resolution fog (fog.frag.hlsl) over the full-resolution scene. The pass is
// drawn with blending: result = fog light + scene * part let through.
//
// Each pixel averages the 3 x 3 nearest fog pixels, which removes their noise, but only
// those whose depth is close to its own; otherwise fog computed for the sky would bleed
// over the outline of a tree in front of it.
#include "common.hlsli"

[[vk::combinedImageSampler]][[vk::binding(0, 2)]]
Texture2D<float4> fog_texture : register(t0, space2);
[[vk::combinedImageSampler]][[vk::binding(0, 2)]]
SamplerState fog_sampler : register(s0, space2);

[[vk::combinedImageSampler]][[vk::binding(1, 2)]]
Texture2D<float> depth_texture : register(t1, space2);
[[vk::combinedImageSampler]][[vk::binding(1, 2)]]
SamplerState depth_sampler : register(s1, space2);

cbuffer CompositeUniforms : register(b0, space3) {
    float4 projection_info;  // see ViewDistance in common.hlsli
    float4 sizes;            // xy: fog image size in pixels, zw: depth image size in pixels
};

struct FragmentInput {
    float4 clip_position : SV_Position;
    float2 uv : TEXCOORD0;
    float2 ndc : TEXCOORD1;
};

float4 main(FragmentInput input) : SV_Target0 {
    static const float kWeights[2] = {1.0, 0.5};
    int2 pixel = int2(input.clip_position.xy);
    int2 fog_pixel = pixel / 2;
    int2 last_fog_pixel = int2(sizes.xy) - 1;
    int2 last_depth_pixel = int2(sizes.zw) - 1;
    float center_distance = ViewDistance(depth_texture.Load(int3(pixel, 0)), projection_info);

    float4 sum = float4(0.0, 0.0, 0.0, 0.0);
    float weight_sum = 0.0;
    float4 nearest = float4(0.0, 0.0, 0.0, 1.0);
    float nearest_difference = 1.0e30;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            int2 tap = clamp(fog_pixel + int2(x, y), int2(0, 0), last_fog_pixel);
            float4 tap_fog = fog_texture.Load(int3(tap, 0));
            // The depth this fog pixel was computed for.
            float tap_distance =
                ViewDistance(depth_texture.Load(int3(min(tap * 2, last_depth_pixel), 0)), projection_info);
            float difference = abs(tap_distance - center_distance);
            float weight = kWeights[abs(x)] * kWeights[abs(y)] * saturate(1.0 - difference / (0.1 * center_distance + 0.05));
            sum += tap_fog * weight;
            weight_sum += weight;
            if (difference < nearest_difference) {
                nearest_difference = difference;
                nearest = tap_fog;
            }
        }
    }
    // No neighbor at a similar depth (a thin twig against the sky): take the closest match.
    return weight_sum > 0.001 ? sum / weight_sum : nearest;
}

// Fragment shader used by Renderer::BakeImpostor: writes what a surface is, not how it is
// lit. Target 0: base color (sRGB-encoded) and coverage; target 1: the surface's facing in
// the model's own coordinates, as 0..1 per axis. Cut-out pixels are dropped as in mesh.frag.hlsl.
#include "common.hlsli"

[[vk::combinedImageSampler]][[vk::binding(0, 2)]]
Texture2D<float4> base_texture : register(t0, space2);
[[vk::combinedImageSampler]][[vk::binding(0, 2)]]
SamplerState base_sampler : register(s0, space2);

[[vk::combinedImageSampler]][[vk::binding(1, 2)]]
Texture2D<float4> normal_texture : register(t1, space2);
[[vk::combinedImageSampler]][[vk::binding(1, 2)]]
SamplerState normal_sampler : register(s1, space2);

cbuffer MaterialUniforms : register(b0, space3) {
    float4 material_factors;  // z: alpha cutoff
    float4 material_flags;    // x: has normal map
};

struct FragmentInput {
    float4 clip_position : SV_Position;
    float3 world_normal : TEXCOORD0;
    float4 color : TEXCOORD1;
    float2 uv : TEXCOORD2;
    float3 world_position : TEXCOORD3;
    bool is_front_face : SV_IsFrontFace;
};

struct FragmentOutput {
    float4 color : SV_Target0;
    float4 normal : SV_Target1;
};

FragmentOutput main(FragmentInput input) {
    float4 texel = base_texture.Sample(base_sampler, input.uv);
    float alpha = texel.a * input.color.a;
    if (material_factors.z > 0.0 && alpha < material_factors.z) {
        discard;
    }
    float3 normal = normalize(input.world_normal) * (input.is_front_face ? 1.0 : -1.0);
    if (material_flags.x > 0.5) {
        float3 map = normal_texture.Sample(normal_sampler, input.uv).xyz * 2.0 - 1.0;
        normal = ApplyNormalMapSample(map, normal, input.world_position, input.uv);
    }
    FragmentOutput output;
    output.color = float4(LinearToSrgb(saturate(texel.rgb * input.color.rgb)), 1.0);
    output.normal = float4(normal * 0.5 + 0.5, 1.0);
    return output;
}

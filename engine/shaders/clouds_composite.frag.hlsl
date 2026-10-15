// Lays the clouds (clouds.frag.hlsl, drawn smaller) over the scene where it shows the sky:
// output rgb the clouds' light, a the share of the sky that still shows, blended as
// scene * a + rgb.

[[vk::combinedImageSampler]][[vk::binding(0, 2)]]
Texture2D<float4> cloud_texture : register(t0, space2);
[[vk::combinedImageSampler]][[vk::binding(0, 2)]]
SamplerState cloud_sampler : register(s0, space2);
[[vk::combinedImageSampler]][[vk::binding(1, 2)]]
Texture2D<float> depth_texture : register(t1, space2);
[[vk::combinedImageSampler]][[vk::binding(1, 2)]]
SamplerState depth_sampler : register(s1, space2);

cbuffer CompositeUniforms : register(b0, space3) {
    float4 unused;
};

struct FragmentInput {
    float4 clip_position : SV_Position;
    float2 uv : TEXCOORD0;
    float2 ndc : TEXCOORD1;
};

float4 main(FragmentInput input) : SV_Target0 {
    if (depth_texture.Load(int3(int2(input.clip_position.xy), 0)) > 0.0) {
        return float4(0.0, 0.0, 0.0, 1.0);  // land, trees, water: the clouds are behind them
    }
    return cloud_texture.SampleLevel(cloud_sampler, input.uv, 0.0);
}

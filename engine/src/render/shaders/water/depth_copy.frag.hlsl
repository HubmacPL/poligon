// Copies the depth image into a plain one-channel image, so that the water pass can read
// the depth of what lies under the water while it writes its own depth.

[[vk::combinedImageSampler]][[vk::binding(0, 2)]]
Texture2D<float> depth_texture : register(t0, space2);
[[vk::combinedImageSampler]][[vk::binding(0, 2)]]
SamplerState depth_sampler : register(s0, space2);

cbuffer CopyUniforms : register(b0, space3) {
    float4 unused;
};

struct FragmentInput {
    float4 clip_position : SV_Position;
    float2 uv : TEXCOORD0;
    float2 ndc : TEXCOORD1;
};

float main(FragmentInput input) : SV_Target0 {
    return depth_texture.Load(int3(int2(input.clip_position.xy), 0));
}

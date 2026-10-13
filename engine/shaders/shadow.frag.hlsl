// Fragment shader of the shadow pass. Only depth is written; the shader exists so that
// cut-out materials (leaves) cast shadows of their visible shape, not of their whole square.

[[vk::combinedImageSampler]][[vk::binding(0, 2)]]
Texture2D<float4> base_texture : register(t0, space2);
[[vk::combinedImageSampler]][[vk::binding(0, 2)]]
SamplerState base_sampler : register(s0, space2);

cbuffer MaterialUniforms : register(b0, space3) {
    float4 material_factors;  // z: alpha cutoff
    float4 material_flags;
};

struct FragmentInput {
    float4 clip_position : SV_Position;
    float3 world_normal : TEXCOORD0;
    float4 color : TEXCOORD1;
    float2 uv : TEXCOORD2;
    float3 world_position : TEXCOORD3;
};

void main(FragmentInput input) {
    if (material_factors.z > 0.0) {
        float alpha = base_texture.Sample(base_sampler, input.uv).a * input.color.a;
        if (alpha < material_factors.z) {
            discard;
        }
    }
}

// Fragment shader of the depth pass (meshes and terrain). Depth is written as in the shadow
// pass (cut-out leaves and fading plants keep their shape), and the motion image gets the
// point's motion on screen since the previous frame (motion vectors): where it was minus
// where it is, in texture coordinates. Temporal anti-aliasing reads its history there, so
// it follows animals, moved objects and plants in the wind instead of smearing them.
#include "common.hlsli"

[[vk::combinedImageSampler]][[vk::binding(0, 2)]]
Texture2D<float4> base_texture : register(t0, space2);
[[vk::combinedImageSampler]][[vk::binding(0, 2)]]
SamplerState base_sampler : register(s0, space2);

cbuffer MaterialUniforms : register(b0, space3) {
    float4 material_factors;  // z: alpha cutoff, w: the frame's noise offset
    float4 material_flags;
    float4 material_shading;
};

struct FragmentInput {
    float4 clip_position : SV_Position;
    float3 world_normal : TEXCOORD0;
    float4 color : TEXCOORD1;
    float2 uv : TEXCOORD2;
    float3 world_position : TEXCOORD3;
    float fade : TEXCOORD4;  // see KeptByFade in common.hlsli
    float4 motion_clip : TEXCOORD5;
    float4 previous_clip : TEXCOORD6;
};

float2 main(FragmentInput input) : SV_Target0 {
    if (!KeptByFade(input.fade, input.clip_position.xy + float2(material_factors.w, 0.0))) {
        discard;
    }
    if (material_factors.z > 0.0) {
        float alpha = base_texture.Sample(base_sampler, input.uv).a * input.color.a;
        if (alpha < material_factors.z) {
            discard;
        }
    }
    if (input.motion_clip.w <= 0.0 || input.previous_clip.w <= 0.0) {
        return float2(4.0, 4.0);  // no motion worked out (see kNoMotion in renderer_internal.h)
    }
    float2 now = input.motion_clip.xy / input.motion_clip.w;
    float2 before = input.previous_clip.xy / input.previous_clip.w;
    return float2(before.x - now.x, now.y - before.y) * 0.5;
}

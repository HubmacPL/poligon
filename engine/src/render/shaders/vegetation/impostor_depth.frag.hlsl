// Fragment shader for impostors in the depth pass: only decides which pixels the card
// covers, the same way impostor.frag.hlsl does. Far away and still, impostors write no
// motion of their own; temporal anti-aliasing works it out from the depth and the cameras.
#include "common.hlsli"
#define IMPOSTOR_FRAGMENT
#include "impostor.hlsli"

cbuffer ImpostorMaterial : register(b0, space3) {
    float4 impostor_material;  // x: alpha cutoff, z: the frame's noise offset
};

struct FragmentInput {
    float4 clip_position : SV_Position;
    float4 atlas_uv01 : TEXCOORD0;
    float4 atlas_uv23 : TEXCOORD1;
    float4 frame_uv01 : TEXCOORD2;
    float4 frame_uv23 : TEXCOORD3;
    float3 world_position : TEXCOORD4;
    float3 axis_x : TEXCOORD5;
    float3 axis_y : TEXCOORD6;
    float3 axis_z : TEXCOORD7;
    float3 blend_fade : TEXCOORD8;
};

float2 main(FragmentInput input) : SV_Target0 {
    if (!KeptByFade(input.blend_fade.z, input.clip_position.xy + float2(impostor_material.z, 0.0))) {
        discard;
    }
    float2 atlas_uv[4] = {input.atlas_uv01.xy, input.atlas_uv01.zw, input.atlas_uv23.xy, input.atlas_uv23.zw};
    float2 frame_uv[4] = {input.frame_uv01.xy, input.frame_uv01.zw, input.frame_uv23.xy, input.frame_uv23.zw};
    ImpostorTexel texel = SampleImpostor(atlas_uv, frame_uv, input.blend_fade.xy);
    if (texel.color.a < impostor_material.x) {
        discard;
    }
    return float2(4.0, 4.0);  // no motion of its own (see kNoMotion in renderer_internal.h)
}

// Fragment shader for impostors in the scene pass: blends four baked frames and lights the
// result like any other surface (lighting.hlsli), so a distant forest follows the sun, the
// sky and the fog exactly as the near one does.
#define LIGHTING_SHADOW_BINDING 2
#define LIGHTING_SHADOW_T t2
#define LIGHTING_SHADOW_S s2
#define LIGHTING_AO_BINDING 3
#define LIGHTING_AO_T t3
#define LIGHTING_AO_S s3
#define LIGHTING_CLOUD_BINDING 4
#define LIGHTING_CLOUD_T t4
#define LIGHTING_CLOUD_S s4
#define LIGHTING_SKY_BINDING 5
#define LIGHTING_SKY_T t5
#define LIGHTING_SKY_S s5
#define LIGHTING_RADIANCE_BINDING 6
#define LIGHTING_RADIANCE_T t6
#define LIGHTING_RADIANCE_S s6
#include "lighting.hlsli"
#define IMPOSTOR_FRAGMENT
#define IMPOSTOR_NORMALS
#include "impostor.hlsli"

cbuffer ImpostorMaterial : register(b1, space3) {
    float4 impostor_material;  // x: alpha cutoff, y: roughness, z: the frame's noise offset
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

#ifdef EARLY_DEPTH_TEST
[earlydepthstencil]
#endif
float4 main(FragmentInput input) : SV_Target0 {
    if (!KeptByFade(input.blend_fade.z, input.clip_position.xy + float2(impostor_material.z, 0.0))) {
        discard;
    }
    float2 atlas_uv[4] = {input.atlas_uv01.xy, input.atlas_uv01.zw, input.atlas_uv23.xy, input.atlas_uv23.zw};
    float2 frame_uv[4] = {input.frame_uv01.xy, input.frame_uv01.zw, input.frame_uv23.xy, input.frame_uv23.zw};
    ImpostorTexel texel = SampleImpostor(atlas_uv, frame_uv, input.blend_fade.xy);
    if (texel.color.a < impostor_material.x) {
        discard;
    }

    float3 local_normal = texel.normal;
    float3 normal = normalize(input.axis_x * local_normal.x + input.axis_y * local_normal.y + input.axis_z * local_normal.z);
    Surface surface;
    surface.base_color = texel.color.rgb;
    surface.metallic = 0.0;
    surface.roughness = impostor_material.y;
    surface.occlusion = 1.0;
    surface.normal = normal;
    surface.surface_normal = normal;
    surface.world_position = input.world_position;
    surface.pixel = input.clip_position.xy;
    surface.thin_foliage = true;
    surface.emission = float3(0.0, 0.0, 0.0);
    return float4(ShadeSurface(surface), 1.0);
}

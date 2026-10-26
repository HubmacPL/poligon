// Particles: the texture times the particle's color, lit simply (the sun with its shadows,
// sky light; no normals) or not at all, faded where the opaque scene lies just behind the
// card (soft edge) and left out where the scene is in front of it (the depth image tells),
// then fog and the haze of distance at the particle's own distance.
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
#define LIGHTING_SPOT_SHADOW_BINDING 7
#define LIGHTING_SPOT_SHADOW_T t7
#define LIGHTING_SPOT_SHADOW_S s7
// The lights' storage buffer comes after the 8 textures.
#define LIGHTING_LIGHT_DATA_BINDING 8
#define LIGHTING_LIGHT_DATA_T t8
#include "lighting.hlsli"

[[vk::combinedImageSampler]][[vk::binding(0, 2)]]
Texture2D<float4> particle_texture : register(t0, space2);
[[vk::combinedImageSampler]][[vk::binding(0, 2)]]
SamplerState particle_sampler : register(s0, space2);

[[vk::combinedImageSampler]][[vk::binding(1, 2)]]
Texture2D<float> depth_texture : register(t1, space2);
[[vk::combinedImageSampler]][[vk::binding(1, 2)]]
SamplerState depth_sampler : register(s1, space2);

cbuffer ParticleUniforms : register(b1, space3) {
    float4 particle_screen;      // xy: size of the image drawn into, zw: size of the depth image
    float4 particle_projection;  // see ViewDistance in common.hlsli
    float4 particle_settings;    // x: soft distance, y: 1 = lit, z: 1 = additive
};

struct FragmentInput {
    float4 clip_position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : TEXCOORD1;
    float3 world_position : TEXCOORD2;
    float view_depth : TEXCOORD3;
};

float4 main(FragmentInput input) : SV_Target0 {
    float4 texel = particle_texture.Sample(particle_sampler, input.uv) * input.color;
    // The scene's depth image may be smaller than the image drawn into (render scale).
    int2 depth_pixel = int2(input.clip_position.xy / particle_screen.xy * particle_screen.zw);
    float scene_depth = depth_texture.Load(int3(depth_pixel, 0));
    float behind = scene_depth > 0.0 ? ViewDistance(scene_depth, particle_projection) - input.view_depth : 1e6;
    if (behind <= 0.0) {
        discard;  // behind a wall, under the ground
    }
    float alpha = texel.a * (particle_settings.x > 0.0 ? SoftFade(behind, particle_settings.x) : 1.0);
    if (alpha < 0.002) {
        discard;
    }
    float3 color = texel.rgb;
    if (particle_settings.y > 0.5) {
        float3 to_camera = normalize(camera_position.xyz - input.world_position);
        float sun = SunVisibility(input.world_position, to_camera) * CloudShadow(input.world_position);
        float3 ambient = sky_radiance.y > 0.5 ? SkyIrradiance(float3(0.0, 1.0, 0.0)) : ambient_sky.rgb;
        color *= light_color.rgb * sun + ambient + LocalLightsSimple(input.clip_position.xy, input.world_position);
    }
    color = ApplyDistanceFog(color, input.world_position);
    // Additive: light added in proportion to alpha; alpha blending: covered by alpha.
    return particle_settings.z > 0.5 ? float4(color * alpha, 0.0) : float4(color, alpha);
}

// Decals: the point of the scene this pixel shows is found from the depth image; if it lies
// inside the decal's box, the decal's picture is looked up at its place in the box (the box's
// x and z; it is pressed along its y axis) and the point is lit as a surface of the decal's
// material, then blended over the scene. Points near the box's faces fade out; points whose
// surface is turned too far from the box's up axis (a trunk standing in a print's box) are
// left out.
#define LIGHTING_SHADOW_BINDING 3
#define LIGHTING_SHADOW_T t3
#define LIGHTING_SHADOW_S s3
#define LIGHTING_AO_BINDING 4
#define LIGHTING_AO_T t4
#define LIGHTING_AO_S s4
#define LIGHTING_CLOUD_BINDING 5
#define LIGHTING_CLOUD_T t5
#define LIGHTING_CLOUD_S s5
#define LIGHTING_SKY_BINDING 6
#define LIGHTING_SKY_T t6
#define LIGHTING_SKY_S s6
#define LIGHTING_RADIANCE_BINDING 7
#define LIGHTING_RADIANCE_T t7
#define LIGHTING_RADIANCE_S s7
#include "lighting.hlsli"

[[vk::combinedImageSampler]][[vk::binding(0, 2)]]
Texture2D<float4> decal_texture : register(t0, space2);
[[vk::combinedImageSampler]][[vk::binding(0, 2)]]
SamplerState decal_sampler : register(s0, space2);

[[vk::combinedImageSampler]][[vk::binding(1, 2)]]
Texture2D<float4> decal_normal_texture : register(t1, space2);
[[vk::combinedImageSampler]][[vk::binding(1, 2)]]
SamplerState decal_normal_sampler : register(s1, space2);

[[vk::combinedImageSampler]][[vk::binding(2, 2)]]
Texture2D<float> depth_texture : register(t2, space2);
[[vk::combinedImageSampler]][[vk::binding(2, 2)]]
SamplerState depth_sampler : register(s2, space2);

cbuffer DecalUniforms : register(b1, space3) {
    float4x4 inverse_view_projection;
    float4 decal_screen;   // xy: drawn size in pixels, z: 1 = normal map, w: opacity
    float4 decal_color;    // linear color and alpha
    float4 decal_surface;  // x: metallic, y: roughness, z: edge fade, w: cosine of the largest angle
};

struct FragmentInput {
    float4 clip_position : SV_Position;
    nointerpolation float4 inverse0 : TEXCOORD0;
    nointerpolation float4 inverse1 : TEXCOORD1;
    nointerpolation float4 inverse2 : TEXCOORD2;
    nointerpolation float3 axis_x : TEXCOORD3;
    nointerpolation float3 axis_up : TEXCOORD4;
    nointerpolation float3 axis_z : TEXCOORD5;
};

float3 WorldAt(int2 pixel) {
    pixel = clamp(pixel, int2(0, 0), int2(decal_screen.xy) - 1);
    float depth = depth_texture.Load(int3(pixel, 0));
    return WorldPositionFromDepth((float2(pixel) + 0.5) / decal_screen.xy, depth, inverse_view_projection);
}

float4 main(FragmentInput input) : SV_Target0 {
    int2 pixel = int2(input.clip_position.xy);
    float depth = depth_texture.Load(int3(pixel, 0));
    if (depth <= 0.0) {
        discard;  // sky
    }
    float3 world = WorldPositionFromDepth((float2(pixel) + 0.5) / decal_screen.xy, depth, inverse_view_projection);
    float4 world4 = float4(world, 1.0);
    float3 local = float3(dot(input.inverse0, world4), dot(input.inverse1, world4), dot(input.inverse2, world4));
    float3 distance_to_face = 0.5 - abs(local);
    if (any(distance_to_face < 0.0)) {
        discard;
    }

    // The surface's direction from neighboring depths; of the two neighbors on each axis the
    // nearer one, so that the normal stays right at an object's outline (as in ao.frag.hlsl).
    float3 left = world - WorldAt(pixel + int2(-1, 0));
    float3 right = WorldAt(pixel + int2(1, 0)) - world;
    float3 up = world - WorldAt(pixel + int2(0, -1));
    float3 down = WorldAt(pixel + int2(0, 1)) - world;
    float3 along_x = dot(left, left) < dot(right, right) ? left : right;
    float3 along_y = dot(up, up) < dot(down, down) ? up : down;
    float3 normal = normalize(cross(along_y, along_x));
    if (dot(normal, camera_position.xyz - world) < 0.0) {
        normal = -normal;
    }
    if (dot(normal, input.axis_up) < decal_surface.w) {
        discard;
    }

    float2 uv = local.xz + 0.5;
    float4 texel = decal_texture.Sample(decal_sampler, uv) * decal_color;
    float fade = saturate(min(min(distance_to_face.x, distance_to_face.y), distance_to_face.z) / max(decal_surface.z * 0.5, 0.0001));
    float alpha = texel.a * decal_screen.w * fade;
    if (alpha < 0.002) {
        discard;
    }

    Surface surface;
    surface.base_color = texel.rgb;
    surface.metallic = decal_surface.x;
    surface.roughness = decal_surface.y;
    surface.occlusion = 1.0;
    surface.surface_normal = normal;
    surface.normal = normal;
    if (decal_screen.z > 0.5) {
        // The picture's x runs along the box's x axis, its "up" (towards v = 0) along -z.
        float2 map_xy = decal_normal_texture.Sample(decal_normal_sampler, uv).xy * 2.0 - 1.0;
        float3 map = float3(map_xy, sqrt(saturate(1.0 - dot(map_xy, map_xy))));
        float3 tangent = normalize(input.axis_x - normal * dot(input.axis_x, normal));
        float3 bitangent = normalize(cross(normal, tangent)) * (dot(cross(normal, tangent), -input.axis_z) < 0.0 ? -1.0 : 1.0);
        surface.normal = normalize(tangent * map.x + bitangent * map.y + normal * map.z);
    }
    surface.world_position = world;
    surface.pixel = input.clip_position.xy;
    surface.thin_foliage = false;
    surface.emission = float3(0.0, 0.0, 0.0);
    return float4(ShadeSurface(surface), alpha);
}

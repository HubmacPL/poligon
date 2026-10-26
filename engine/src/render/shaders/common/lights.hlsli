// Point and spot lights (Renderer::DrawLight; resources/lights.cpp): the lights and the tiles'
// masks in a storage buffer (LIGHTING_LIGHT_DATA_BINDING, after the shader's textures; too large
// for a uniform buffer), the rest in uniform buffer 3 of the fragment shader. The screen is cut
// into tiles; each holds a 64-bit mask of the lights that may reach it, so a pixel only visits
// the lights its tile lists. Included by lighting.hlsli
// (surfaces, with spot light shadows) and fog.frag.hlsl (the nearest four lights in the fog).
#ifndef POLIGON_LIGHTS_HLSLI
#define POLIGON_LIGHTS_HLSLI

struct LocalLight {
    float4 position_range;   // xyz: relative to the camera, w: no light beyond this distance
    float4 color_spot;       // rgb: color times intensity, w: 1 = spot light
    float4 direction_outer;  // xyz: a spot's axis, w: cosine of its outer angle
    float4 inner_shadow;     // x: cosine of the inner angle, y: shadow tile (-1 = none)
};

#ifndef LIGHTS_NO_UNIFORMS
cbuffer LightUniforms : register(b3, space3) {
    float4 light_info;            // x: light count, y, z: tiles across and down
    float4 pixel_to_tile;         // xy: tiles per pixel of the image drawn into
    float4x4 spot_shadow_matrices[4];
};

// 64 lights of 64 bytes, then 48 x 27 tiles, two per 16 bytes (xy the first, zw the second).
[[vk::binding(LIGHTING_LIGHT_DATA_BINDING, 2)]]
ByteAddressBuffer light_data : register(LIGHTING_LIGHT_DATA_T, space2);

LocalLight LoadLocalLight(int index) {
    uint start = (uint)index * 64u;
    LocalLight light;
    light.position_range = asfloat(light_data.Load4(start));
    light.color_spot = asfloat(light_data.Load4(start + 16u));
    light.direction_outer = asfloat(light_data.Load4(start + 32u));
    light.inner_shadow = asfloat(light_data.Load4(start + 48u));
    return light;
}

// The lights that may reach a pixel of the image drawn into, as two 32-bit masks.
uint2 LightMask(float2 pixel) {
    int2 tile = clamp(int2(pixel * pixel_to_tile.xy), int2(0, 0), int2(light_info.yz) - 1);
    int index = tile.y * (int)light_info.y + tile.x;
    uint4 pair = light_data.Load4(4096u + (uint)(index >> 1) * 16u);
    return (index & 1) != 0 ? pair.zw : pair.xy;
}
#endif

// The light of one point or spot light arriving at `position` (no shadow), and the way to it.
// It falls with the square of the distance (a white matte surface facing the light from 1 m
// shows its color times intensity) and fades smoothly to nothing at its range (the window of
// Karis, "Real Shading in Unreal Engine 4", 2013); a spot fades from its inner to its outer angle.
float3 LocalLightArriving(LocalLight light, float3 position, out float3 to_light, out float distance) {
    float3 offset = light.position_range.xyz - position;
    float distance_squared = dot(offset, offset);
    distance = sqrt(distance_squared);
    to_light = offset / max(distance, 1e-4);
    float ratio = distance / light.position_range.w;
    float window = saturate(1.0 - ratio * ratio * ratio * ratio);
    float3 arriving = light.color_spot.rgb * (window * window / max(distance_squared, 0.01));
    if (light.color_spot.w > 0.5) {
        arriving *= smoothstep(light.direction_outer.w, light.inner_shadow.x, dot(-to_light, light.direction_outer.xyz));
    }
    return arriving;
}

#endif

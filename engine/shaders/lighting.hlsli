// Lighting shared by every fragment shader that draws a lit surface (mesh.frag.hlsl,
// terrain.frag.hlsl, impostor.frag.hlsl, water.frag.hlsl): the frame's uniforms, the sun with
// its shadows and the shadows of clouds, ambient light from the sky (darkened where hills
// and tree crowns hide it) and the ground, light shining through thin leaves, a faint
// reflection of the sky, fog and the haze of distance (aerial perspective).
//
// A shader that includes this file first says which texture slots the shadow image, the
// ambient occlusion image, the cloud shadow image and the sky occlusion map use, because its
// own textures come before them:
//     #define LIGHTING_SHADOW_BINDING 3   // and LIGHTING_SHADOW_T t3, LIGHTING_SHADOW_S s3
//     #define LIGHTING_AO_BINDING 4       // and LIGHTING_AO_T t4, LIGHTING_AO_S s4
//     #define LIGHTING_CLOUD_BINDING 5    // and LIGHTING_CLOUD_T t5, LIGHTING_CLOUD_S s5
//     #define LIGHTING_SKY_BINDING 6      // and LIGHTING_SKY_T t6, LIGHTING_SKY_S s6
// The frame's uniforms are always uniform buffer 0 of the fragment shader.
#include "common.hlsli"

// All shadow cascades side by side in one depth image (2 x 2 tiles).
[[vk::combinedImageSampler]][[vk::binding(LIGHTING_SHADOW_BINDING, 2)]]
Texture2D<float> shadow_texture : register(LIGHTING_SHADOW_T, space2);
[[vk::combinedImageSampler]][[vk::binding(LIGHTING_SHADOW_BINDING, 2)]]
SamplerComparisonState shadow_sampler : register(LIGHTING_SHADOW_S, space2);

// Ambient occlusion computed from the depth image (ao.frag.hlsl), one value per screen pixel.
[[vk::combinedImageSampler]][[vk::binding(LIGHTING_AO_BINDING, 2)]]
Texture2D<float> ao_texture : register(LIGHTING_AO_T, space2);
[[vk::combinedImageSampler]][[vk::binding(LIGHTING_AO_BINDING, 2)]]
SamplerState ao_sampler : register(LIGHTING_AO_S, space2);

// Share of sunlight that gets through the clouds, over the land around the camera
// (cloud_shadow.frag.hlsl).
[[vk::combinedImageSampler]][[vk::binding(LIGHTING_CLOUD_BINDING, 2)]]
Texture2D<float> cloud_shadow_texture : register(LIGHTING_CLOUD_T, space2);
[[vk::combinedImageSampler]][[vk::binding(LIGHTING_CLOUD_BINDING, 2)]]
SamplerState cloud_shadow_sampler : register(LIGHTING_CLOUD_S, space2);

// Share of the sky each place on the land sees (poligon/sky_occlusion.h).
[[vk::combinedImageSampler]][[vk::binding(LIGHTING_SKY_BINDING, 2)]]
Texture2D<float> sky_occlusion_texture : register(LIGHTING_SKY_T, space2);
[[vk::combinedImageSampler]][[vk::binding(LIGHTING_SKY_BINDING, 2)]]
SamplerState sky_occlusion_sampler : register(LIGHTING_SKY_S, space2);

cbuffer FrameUniforms : register(b0, space3) {
    float4x4 shadow_matrices[4];  // world position -> shadow image coordinates and depth, per cascade
    float4 cascade_far;           // distance from the camera where each cascade ends
    float4 cascade_texel;         // size in meters of one shadow image pixel, per cascade
    float4 camera_position;
    float4 camera_forward;
    float4 light_direction;       // xyz: direction the light travels, normalized
    float4 light_color;
    float4 ambient_sky;           // light arriving from above
    float4 ambient_ground;        // light arriving from below
    float4 sky_zenith;
    float4 sky_horizon;
    float4 sky_ground;
    float4 fog;                   // rgb: color, a: density per meter
    float4 options;               // x: shadows on, y: alpha to coverage on, z: sky reflections on, w: shadow texel size
    float4 screen;                // x: screen-space ambient occlusion on, y: tint by shadow cascade (debug)
    float4 cascade_scale;         // share of its tile's width and height each cascade uses
    float4 cloud_shadow_area;     // xy: world x and z of the cloud shadow image's corner, z: 1 / its size in meters, w: 1 = on
    float4 sky_occlusion_area;    // xy: world x and z of the sky occlusion map's first point, z: 1 / its size in meters, w: 1 = on
    float4 aerial;                // rgb: color of the haze of distance, a: its density per meter (0 = off)
    float4 foliage;               // x: how much sunlight thin leaves let through
};

// Share of sunlight the clouds let through at a place (1 without clouds).
float CloudShadow(float3 world_position) {
    if (cloud_shadow_area.w < 0.5) {
        return 1.0;
    }
    float2 uv = (world_position.xz - cloud_shadow_area.xy) * cloud_shadow_area.z;
    if (any(uv < 0.0) || any(uv > 1.0)) {
        return 1.0;
    }
    return cloud_shadow_texture.SampleLevel(cloud_shadow_sampler, uv, 0.0);
}

// Share of the sky a place sees (1 without a map). The map holds the ground's view; a point
// higher up (a tree's crown) gets the same value, which darkens crowns a little too much.
float SkyVisibility(float3 world_position, float3 normal) {
    if (sky_occlusion_area.w < 0.5) {
        return 1.0;
    }
    float2 uv = (world_position.xz - sky_occlusion_area.xy) * sky_occlusion_area.z;
    if (any(uv < 0.0) || any(uv > 1.0)) {
        return 1.0;
    }
    return sky_occlusion_texture.SampleLevel(sky_occlusion_sampler, uv, 0.0);
}

// Distant things fade into the haze: bluish away from the sun, warm and bright towards it.
float3 ApplyAerialPerspective(float3 color, float3 world_position) {
    if (aerial.a <= 0.0) {
        return color;
    }
    float3 view = world_position - camera_position.xyz;
    float distance_to_camera = length(view);
    float towards_sun = pow(saturate(dot(view / max(distance_to_camera, 0.001), -light_direction.xyz)), 6.0);
    float3 haze = lerp(aerial.rgb, aerial.rgb * 0.5 + light_color.rgb * 0.35, towards_sun);
    return lerp(color, haze, 1.0 - exp(-distance_to_camera * aerial.a));
}

static const float kPi = 3.14159265;

// A surface point as the lighting needs it.
struct Surface {
    float3 base_color;
    float metallic;
    float roughness;
    // Ambient occlusion from the material: 1 = open, 0 = fully shielded from ambient light.
    float occlusion;
    // Direction the surface faces for lighting (with the normal map applied) ...
    float3 normal;
    // ... and without it, for placing shadow lookups.
    float3 surface_normal;
    float3 world_position;
    // SV_Position.xy of the pixel.
    float2 pixel;
    // Thin foliage lets some sunlight through to the far side.
    bool thin_foliage;
};

// For a tent filter 5 shadow pixels wide centered `offset` pixels past a pixel center: the
// filter's weight on each of the six pixels it can touch (pixel -2 to pixel 3), folded into
// three pairs that one bilinear fetch each can read. `weights` are the pairs' weights,
// `positions` where to fetch, in pixels from the center pixel.
// Share of the tent (half width 2.5, height 1, area 6.25) that lies left of x.
float TentIntegral(float x) {
    x = clamp(x, -2.5, 2.5);
    float from_edge = 2.5 - abs(x);
    float corner = from_edge * from_edge * 0.08;  // 0.5 / 6.25
    return x < 0.0 ? corner : 1.0 - corner;
}

void TentWeights(float offset, out float3 weights, out float3 positions) {
    // The seven pixel edges from -2.5 to 3.5, relative to the tent's center.
    float integrals[7];
    [unroll] for (int edge = 0; edge < 7; ++edge) {
        integrals[edge] = TentIntegral(float(edge) - 2.5 - offset);
    }
    [unroll] for (int pair = 0; pair < 3; ++pair) {
        float a = integrals[pair * 2 + 1] - integrals[pair * 2];
        float b = integrals[pair * 2 + 2] - integrals[pair * 2 + 1];
        weights[pair] = a + b;
        positions[pair] = float(pair * 2 - 2) + b / max(a + b, 0.0001);
    }
}

// Shadow lookup in one cascade: 1 = lit, 0 = in shadow.
float CascadeVisibility(int cascade, float3 world_position, float3 normal) {
    // Move the lookup point off the surface by two and a half shadow pixels (the filter's
    // reach), so that a surface does not shadow itself. Fine cascades need (and get) a much
    // smaller shift than coarse ones.
    float3 offset_position = world_position + normal * (cascade_texel[cascade] * 2.5);
    float4 shadow_position = mul(shadow_matrices[cascade], float4(offset_position, 1.0));
    float texel = options.w;

    // A tent filter 5 pixels wide, read with 9 bilinear comparisons: soft edges without the
    // steps a box of single pixels shows. The fetches stay inside the cascade's tile.
    float2 pixel = shadow_position.xy / texel;
    float2 center = floor(pixel - 0.5) + 0.5;
    float2 offset = pixel - center;
    float3 weights_x, positions_x, weights_y, positions_y;
    TentWeights(offset.x, weights_x, positions_x);
    TentWeights(offset.y, weights_y, positions_y);
    float2 tile_min = float2(cascade & 1, cascade >> 1) * 0.5 + texel * 0.5;
    float2 tile_max = tile_min + 0.5 * cascade_scale[cascade] - texel;
    float depth = shadow_position.z - 0.0002;
    float lit = 0.0;
    [unroll] for (int y = 0; y < 3; ++y) {
        [unroll] for (int x = 0; x < 3; ++x) {
            float2 uv = clamp((center + float2(positions_x[x], positions_y[y])) * texel, tile_min, tile_max);
            lit += weights_x[x] * weights_y[y] * shadow_texture.SampleCmpLevelZero(shadow_sampler, uv, depth);
        }
    }
    return lit;
}

// The shadow cascade that covers a point: 0 to 3, or 4 beyond the shadow distance.
int CascadeAt(float3 world_position, out float view_depth) {
    view_depth = dot(world_position - camera_position.xyz, camera_forward.xyz);
    int cascade = 4;
    for (int i = 3; i >= 0; --i) {
        if (view_depth < cascade_far[i]) {
            cascade = i;
        }
    }
    return cascade;
}

// Part of the sun's light that reaches this point: 1 = fully lit, 0 = in shadow.
float SunVisibility(float3 world_position, float3 normal) {
    if (options.x < 0.5) {
        return 1.0;
    }
    float view_depth;
    int cascade = CascadeAt(world_position, view_depth);
    if (cascade == 4) {
        return 1.0;  // beyond the shadow distance
    }

    float lit = CascadeVisibility(cascade, world_position, normal);
    // Over the last tenth of a cascade's range, fade into the next (coarser) one, so that
    // no line shows on the ground where shadows change sharpness; the last cascade fades
    // into full light, so that the end of the shadow range is not a line either.
    float fade = saturate((view_depth - cascade_far[cascade] * 0.9) / (cascade_far[cascade] * 0.1));
    if (fade > 0.0) {
        lit = lerp(lit, cascade < 3 ? CascadeVisibility(cascade + 1, world_position, normal) : 1.0, fade);
    }
    return lit;
}

// The color of a surface point under the frame's light, in linear wide-range values.
float3 ShadeSurface(Surface surface) {
    float3 base_color = surface.base_color;
    float metallic = surface.metallic;
    float roughness = clamp(surface.roughness, 0.045, 1.0);
    float occlusion = surface.occlusion;
    if (screen.x > 0.5) {
        occlusion *= ao_texture.Load(int3(int2(surface.pixel), 0));
    }
    float3 normal = surface.normal;

    float3 to_camera = normalize(camera_position.xyz - surface.world_position);
    float3 to_light = -light_direction.xyz;
    float n_dot_l = saturate(dot(normal, to_light));
    float n_dot_v = max(dot(normal, to_camera), 0.0001);

    float3 diffuse_color = base_color * (1.0 - metallic);
    // Reflectance when looking straight at the surface: 4% for non-metals, the base color for metals.
    float3 specular_color = lerp(float3(0.04, 0.04, 0.04), base_color, metallic);

    float3 color = float3(0.0, 0.0, 0.0);

    // --- Sun ---
    if (dot(light_color.rgb, light_color.rgb) > 0.0) {
        float visibility = SunVisibility(surface.world_position, surface.surface_normal) * CloudShadow(surface.world_position);
        float3 sun = light_color.rgb * visibility;

        // Specular highlight (GGX microfacet model).
        float3 half_vector = normalize(to_light + to_camera);
        float n_dot_h = saturate(dot(normal, half_vector));
        float v_dot_h = saturate(dot(to_camera, half_vector));
        float a = roughness * roughness;
        float a2 = a * a;
        float denominator = n_dot_h * n_dot_h * (a2 - 1.0) + 1.0;
        float distribution = a2 / (kPi * denominator * denominator);
        float k = (roughness + 1.0) * (roughness + 1.0) / 8.0;
        float geometry = (n_dot_l / (n_dot_l * (1.0 - k) + k)) * (n_dot_v / (n_dot_v * (1.0 - k) + k));
        float3 fresnel = specular_color + (1.0 - specular_color) * pow(1.0 - v_dot_h, 5.0);
        float3 specular = distribution * geometry * fresnel / max(4.0 * n_dot_l * n_dot_v, 0.0001);

        // Light units are chosen so that a white, rough surface facing the sun reflects exactly
        // the sun's color; the highlight is scaled by pi to match.
        color += (diffuse_color * (1.0 - fresnel) + specular * kPi) * sun * n_dot_l;

        // Thin foliage lets light through: a leaf lit from behind glows on the near side, in
        // a warmer, more saturated green than its lit face (light passes the leaf's color
        // twice), and most when the camera looks towards the sun through it.
        if (surface.thin_foliage) {
            float back_light = saturate(dot(-normal, to_light) * 0.6 + 0.4);
            float through = pow(saturate(dot(-to_camera, to_light)), 4.0);
            float3 transmitted_color = diffuse_color * (0.4 + 0.8 * diffuse_color);
            color += transmitted_color * sun * foliage.x * (back_light * 0.6 + through * 1.4);
        }
    }

    // --- Ambient light: from the sky above and the ground below ---
    // The sky's light comes mostly from the blue zenith, so surfaces in shadow take a cool
    // tint; hills and tree crowns around a place hide part of the sky (large-scale occlusion).
    float sky_amount = normal.y * 0.5 + 0.5;
    float sky_visible = SkyVisibility(surface.world_position, normal);
    float3 ambient = lerp(ambient_ground.rgb * lerp(0.6, 1.0, sky_visible), ambient_sky.rgb * sky_visible, sky_amount);
    color += diffuse_color * ambient * occlusion;

    // Smooth surfaces also mirror the sky faintly.
    if (options.z > 0.5) {
        float3 reflected = reflect(-to_camera, normal);
        float3 sky = SkyColor(reflected, sky_zenith.rgb, sky_horizon.rgb, sky_ground.rgb);
        float3 fresnel = specular_color + (max(1.0 - roughness, specular_color) - specular_color) * pow(1.0 - n_dot_v, 5.0);
        color += sky * fresnel * (1.0 - roughness) * occlusion;
    }

    // Debug aid: show which shadow cascade shades each pixel.
    if (screen.y > 0.5) {
        static const float3 kCascadeTints[5] = {float3(1.0, 0.35, 0.35), float3(0.35, 1.0, 0.35), float3(0.4, 0.4, 1.0),
                                                float3(1.0, 1.0, 0.35), float3(1.0, 1.0, 1.0)};
        float view_depth;
        color *= kCascadeTints[CascadeAt(surface.world_position, view_depth)];
    }

    // --- Fog: distant things fade into the fog color ---
    if (fog.a > 0.0) {
        float distance_to_camera = length(camera_position.xyz - surface.world_position);
        float fog_amount = 1.0 - exp(-distance_to_camera * fog.a);
        color = lerp(color, fog.rgb, fog_amount);
    }
    return ApplyAerialPerspective(color, surface.world_position);
}

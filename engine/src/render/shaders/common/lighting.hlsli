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
//     #define LIGHTING_RADIANCE_BINDING 7 // and LIGHTING_RADIANCE_T t7, LIGHTING_RADIANCE_S s7
//     #define LIGHTING_SPOT_SHADOW_BINDING 8 // and LIGHTING_SPOT_SHADOW_T t8, LIGHTING_SPOT_SHADOW_S s8
//     #define LIGHTING_LIGHT_DATA_BINDING 10 // and LIGHTING_LIGHT_DATA_T t10: the lights' storage buffer, after all textures
// The frame's uniforms are always uniform buffer 0 of the fragment shader, the point and
// spot lights (lights.hlsli) uniform buffer 3.
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

// Share of the sky each place on the land sees (poligon/world/sky_occlusion.h).
[[vk::combinedImageSampler]][[vk::binding(LIGHTING_SKY_BINDING, 2)]]
Texture2D<float> sky_occlusion_texture : register(LIGHTING_SKY_T, space2);
[[vk::combinedImageSampler]][[vk::binding(LIGHTING_SKY_BINDING, 2)]]
SamplerState sky_occlusion_sampler : register(LIGHTING_SKY_S, space2);

// The sky light map: the sky with its clouds in every direction, blurred level by level as
// ever rougher surfaces reflect it (sky_capture and sky_prefilter in shaders/sky).
[[vk::combinedImageSampler]][[vk::binding(LIGHTING_RADIANCE_BINDING, 2)]]
TextureCube<float4> sky_radiance_texture : register(LIGHTING_RADIANCE_T, space2);
[[vk::combinedImageSampler]][[vk::binding(LIGHTING_RADIANCE_BINDING, 2)]]
SamplerState sky_radiance_sampler : register(LIGHTING_RADIANCE_S, space2);

// The shadows of up to four spot lights, 2 x 2 tiles (passes/spot_shadow_pass.cpp).
[[vk::combinedImageSampler]][[vk::binding(LIGHTING_SPOT_SHADOW_BINDING, 2)]]
Texture2D<float> spot_shadow_texture : register(LIGHTING_SPOT_SHADOW_T, space2);
[[vk::combinedImageSampler]][[vk::binding(LIGHTING_SPOT_SHADOW_BINDING, 2)]]
SamplerComparisonState spot_shadow_sampler : register(LIGHTING_SPOT_SHADOW_S, space2);

#include "lights.hlsli"

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
    float4 sky_radiance;          // x: the sky light map's level for roughness 1, y: 1 = ambient light and reflections come from it, z: its level for ambient light, w: share of the sky's color in fog and haze
    float4 cascade_depth;         // meters across each cascade's depth range (towards the sun)
    float4 shadow_soft;           // x: penumbra width per meter from the caster (0 = fixed filter), y: widest filter, in filter widths
};

// The sky light map in a direction, as a surface of the given roughness (0..1) reflects it.
float3 SkyRadiance(float3 direction, float roughness) {
    return sky_radiance_texture.SampleLevel(sky_radiance_sampler, direction, roughness * sky_radiance.x).rgb;
}

// The light a matte surface facing `normal` gets from the whole sky (and ground) before it.
float3 SkyIrradiance(float3 normal) {
    return sky_radiance_texture.SampleLevel(sky_radiance_sampler, normal, sky_radiance.z).rgb;
}

// The split-sum environment BRDF as two numbers (scale and bias of the specular color),
// fitted in closed form instead of read from a table (Karis, "Physically Based Shading on
// Mobile", 2014, after Lazarov 2013).
float2 EnvironmentBrdf(float roughness, float n_dot_v) {
    const float4 c0 = float4(-1.0, -0.0275, -0.572, 0.022);
    const float4 c1 = float4(1.0, 0.0425, 1.04, -0.04);
    float4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * n_dot_v)) * r.x + r.y;
    return float2(-1.04, 1.04) * a004 + r.zw;
}

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
// The sky a little above the horizon in the direction's bearing, somewhat blurred (the
// level for roughness 1/3, which still leaves the ground below the horizon out): the color
// fog and haze take far away, from the sky light map, so with its clouds.
float3 HorizonSky(float3 direction) {
    float2 bearing = normalize(direction.xz + float2(0.0, 1e-4));
    return sky_radiance_texture.SampleLevel(sky_radiance_sampler, normalize(float3(bearing.x, 0.15, bearing.y)), sky_radiance.x / 3.0).rgb;
}

float3 ApplyAerialPerspective(float3 color, float3 world_position) {
    if (aerial.a <= 0.0) {
        return color;
    }
    float3 view = world_position - camera_position.xyz;
    float distance_to_camera = length(view);
    float3 direction = view / max(distance_to_camera, 0.001);
    float towards_sun = pow(saturate(dot(direction, -light_direction.xyz)), 6.0);
    float3 haze_color = aerial.rgb;
    if (sky_radiance.w > 0.0) {
        haze_color = lerp(haze_color, HorizonSky(direction), sky_radiance.w);
    }
    float3 haze = lerp(haze_color, haze_color * 0.5 + light_color.rgb * 0.35, towards_sun);
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
    // Light the surface gives off itself; added before fog and haze.
    float3 emission;
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
    float2 tile_min = float2(cascade & 1, cascade >> 1) * 0.5 + texel * 0.5;
    float2 tile_max = tile_min + 0.5 * cascade_scale[cascade] - texel;

    // Shadows soften with distance from their caster (percentage-closer soft shadows,
    // Fernando 2005, without the usual random sampling, so that the picture stays still):
    // the average depth of what blocks the sun around the point (9 reads, 3 pixels apart)
    // gives the distance to the caster, the sun's size the penumbra, and the filter below
    // is stretched to match, from its own width up to shadow_soft.y times it.
    float scale = 1.0;
    if (shadow_soft.x > 0.0) {
        int2 base = int2(shadow_position.xy / texel);
        int2 lowest = int2(tile_min / texel);
        int2 highest = int2(tile_max / texel);
        float receiver = shadow_position.z;
        float blocker_sum = 0.0;
        float blockers = 0.0;
        [unroll] for (int by = -1; by <= 1; ++by) {
            [unroll] for (int bx = -1; bx <= 1; ++bx) {
                float stored = shadow_texture.Load(int3(clamp(base + int2(bx, by) * 3, lowest, highest), 0));
                if (stored < receiver - 0.0002) {
                    blocker_sum += stored;
                    blockers += 1.0;
                }
            }
        }
        if (blockers > 0.0) {
            float caster_distance = (receiver - blocker_sum / blockers) * cascade_depth[cascade];
            float penumbra_pixels = caster_distance * shadow_soft.x / cascade_texel[cascade];
            scale = clamp(penumbra_pixels / 5.0, 1.0, shadow_soft.y);
            // The wider filter reaches further; move the point off the surface to match.
            offset_position = world_position + normal * (cascade_texel[cascade] * 2.5 * scale);
            shadow_position = mul(shadow_matrices[cascade], float4(offset_position, 1.0));
        }
    }

    // A tent filter 5 pixels wide (5 x scale), read with 9 bilinear comparisons: soft edges
    // without the steps a box of single pixels shows. The fetches stay inside the cascade's tile.
    float stretched = texel * scale;
    float2 pixel = shadow_position.xy / stretched;
    float2 center = floor(pixel - 0.5) + 0.5;
    float2 offset = pixel - center;
    float3 weights_x, positions_x, weights_y, positions_y;
    TentWeights(offset.x, weights_x, positions_x);
    TentWeights(offset.y, weights_y, positions_y);
    float depth = shadow_position.z - 0.0002;
    float lit = 0.0;
    [unroll] for (int y = 0; y < 3; ++y) {
        [unroll] for (int x = 0; x < 3; ++x) {
            float2 uv = clamp((center + float2(positions_x[x], positions_y[y])) * stretched, tile_min, tile_max);
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

// Fog (Environment::fog_density; distant things fade into the fog color) and the haze of
// distance, for a point of a lit surface or a particle.
float3 ApplyDistanceFog(float3 color, float3 world_position) {
    if (fog.a > 0.0) {
        float distance_to_camera = length(camera_position.xyz - world_position);
        float fog_amount = 1.0 - exp(-distance_to_camera * fog.a);
        float3 fog_color = fog.rgb;
        if (sky_radiance.w > 0.0) {
            fog_color = lerp(fog_color, HorizonSky(world_position - camera_position.xyz), sky_radiance.w);
        }
        color = lerp(color, fog_color, fog_amount);
    }
    return ApplyAerialPerspective(color, world_position);
}

// What a surface sends towards the camera of light arriving from `to_light`: a GGX highlight
// and the diffuse part. Light units are chosen so that a white, rough surface facing a light
// reflects exactly the light's color; the highlight is scaled by pi to match.
float3 SurfaceReflection(float3 normal, float3 to_camera, float3 to_light, float3 diffuse_color, float3 specular_color,
                         float roughness, float n_dot_v) {
    float n_dot_l = saturate(dot(normal, to_light));
    float3 half_vector = normalize(to_light + to_camera);
    float n_dot_h = saturate(dot(normal, half_vector));
    float v_dot_h = saturate(dot(to_camera, half_vector));
    float a = roughness * roughness;
    float a2 = a * a;
    float denominator = n_dot_h * n_dot_h * (a2 - 1.0) + 1.0;
    float distribution = a2 / (kPi * denominator * denominator);
    float k = (roughness + 1.0) * (roughness + 1.0) / 8.0;
    float geometry = (n_dot_l / (n_dot_l * (1.0 - k) + k)) * (n_dot_v / (n_dot_v * (1.0 - k) + k));
    float edge = 1.0 - v_dot_h;
    float edge2 = edge * edge;
    float3 fresnel = specular_color + (1.0 - specular_color) * (edge2 * edge2 * edge);
    float3 specular = distribution * geometry * fresnel / max(4.0 * n_dot_l * n_dot_v, 0.0001);
    return (diffuse_color * (1.0 - fresnel) + specular * kPi) * n_dot_l;
}

// Share of a spot light's light reaching a point past what its shadow image holds: 3 x 3
// comparisons one shadow pixel apart, the point moved off the surface along its normal by
// about a shadow pixel's size at that distance.
float SpotShadow(int tile, float3 world_position, float3 normal, float distance) {
    float4 position = mul(spot_shadow_matrices[tile], float4(world_position + normal * (distance * 0.003 + 0.01), 1.0));
    float3 shadow_position = position.xyz / position.w;
    const float texel = 1.0 / 2048.0;
    float lit = 0.0;
    [unroll] for (int y = -1; y <= 1; ++y) {
        [unroll] for (int x = -1; x <= 1; ++x) {
            lit += spot_shadow_texture.SampleCmpLevelZero(spot_shadow_sampler, shadow_position.xy + float2(x, y) * texel, shadow_position.z);
        }
    }
    return lit / 9.0;
}

// One point or spot light as it arrives at a point, with its shadow for a spot light that has one.
float3 LocalLightWithShadow(LocalLight light, float3 position, float3 shadow_normal, out float3 to_local) {
    float local_distance;
    float3 arriving = LocalLightArriving(light, position, to_local, local_distance);
    if (light.inner_shadow.y >= 0.0) {
        arriving *= SpotShadow((int)light.inner_shadow.y, position, shadow_normal, local_distance);
    }
    return arriving;
}

// The point and spot lights listed for a pixel's tile, reflected by a surface.
float3 LocalLightsOnSurface(float2 pixel, float3 position, float3 surface_normal, float3 normal, float3 to_camera,
                            float3 diffuse_color, float3 specular_color, float roughness, float n_dot_v) {
    float3 total = float3(0.0, 0.0, 0.0);
    uint2 mask = LightMask(pixel);
    [loop] for (int part = 0; part < 2; ++part) {
        uint bits = part == 0 ? mask.x : mask.y;
        [loop] while (bits != 0u) {
            uint bit = firstbitlow(bits);
            bits &= bits - 1u;
            // A tile lists every light whose reach may touch it; many of its pixels still lie
            // beyond the reach or behind the surface: those read only the light's position.
            int index = part * 32 + (int)bit;
            float4 position_range = LoadLightPosition(index);
            float3 offset = position_range.xyz - position;
            if (dot(offset, offset) >= position_range.w * position_range.w || dot(offset, normal) <= 0.0) {
                continue;
            }
            LocalLight light = LoadLocalLight(index);
            float3 to_local;
            float3 arriving = LocalLightWithShadow(light, position, surface_normal, to_local);
            if (all(arriving <= 0.0)) {
                continue;
            }
            total += SurfaceReflection(normal, to_camera, to_local, diffuse_color, specular_color, roughness, n_dot_v) * arriving;
        }
    }
    return total;
}

// Point and spot light on a particle lit the simple way: all that arrives, whatever its facing.
float3 LocalLightsSimple(float2 pixel, float3 position) {
    float3 total = float3(0.0, 0.0, 0.0);
    uint2 mask = LightMask(pixel);
    [loop] for (int part = 0; part < 2; ++part) {
        uint bits = part == 0 ? mask.x : mask.y;
        [loop] while (bits != 0u) {
            uint bit = firstbitlow(bits);
            bits &= bits - 1u;
            float3 to_local;
            total += LocalLightWithShadow(LoadLocalLight(part * 32 + (int)bit), position, float3(0.0, 0.0, 0.0), to_local);
        }
    }
    return total;
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

        color += SurfaceReflection(normal, to_camera, to_light, diffuse_color, specular_color, roughness, n_dot_v) * sun;

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

    // --- Point and spot lights listed for this pixel's tile ---
    color += LocalLightsOnSurface(surface.pixel, surface.world_position, surface.surface_normal, normal, to_camera, diffuse_color,
                                  specular_color, roughness, n_dot_v);

    // --- Ambient light: from the sky above and the ground below ---
    // Hills and tree crowns around a place hide part of the sky (large-scale occlusion): the
    // sky's share of the light is cut by what the place sees of it, the ground's less.
    float sky_amount = normal.y * 0.5 + 0.5;
    float sky_visible = SkyVisibility(surface.world_position, normal);
    if (sky_radiance.y > 0.5) {
        // From the sky light map: the real sky before the surface, with its clouds.
        float visible = lerp(lerp(0.6, 1.0, sky_visible), sky_visible, sky_amount);
        color += diffuse_color * SkyIrradiance(normal) * visible * occlusion;

        // Reflections of the sky: the direction bent towards the normal as roughness grows,
        // the level of the map for the roughness, nothing from below the surface's horizon,
        // and the split-sum BRDF with the energy rough surfaces would otherwise lose.
        float3 reflected = reflect(-to_camera, normal);
        reflected = normalize(lerp(reflected, normal, roughness * roughness));
        float horizon = saturate(1.0 + dot(reflected, surface.surface_normal));
        float2 brdf = EnvironmentBrdf(roughness, n_dot_v);
        float3 energy = 1.0 + specular_color * (1.0 / max(brdf.x + brdf.y, 0.001) - 1.0);
        // Less reflection where the surroundings hide the sky: from ambient occlusion
        // (Lagarde and de Rousiers, "Moving Frostbite to PBR", 2014) and the sky occlusion map.
        float specular_occlusion = saturate(pow(n_dot_v + occlusion, exp2(-16.0 * roughness - 1.0)) - 1.0 + occlusion) * sky_visible;
        color += SkyRadiance(reflected, roughness) * (specular_color * brdf.x + brdf.y) * energy * horizon * horizon * specular_occlusion;
    } else {
        // Without the map: two colors, the sky's from above and the ground's from below.
        float3 ambient = lerp(ambient_ground.rgb * lerp(0.6, 1.0, sky_visible), ambient_sky.rgb * sky_visible, sky_amount);
        color += diffuse_color * ambient * occlusion;
    }

    color += surface.emission;

    // Debug aid: show which shadow cascade shades each pixel.
    if (screen.y > 0.5) {
        static const float3 kCascadeTints[5] = {float3(1.0, 0.35, 0.35), float3(0.35, 1.0, 0.35), float3(0.4, 0.4, 1.0),
                                                float3(1.0, 1.0, 0.35), float3(1.0, 1.0, 1.0)};
        float view_depth;
        color *= kCascadeTints[CascadeAt(surface.world_position, view_depth)];
    }

    return ApplyDistanceFog(color, surface.world_position);
}

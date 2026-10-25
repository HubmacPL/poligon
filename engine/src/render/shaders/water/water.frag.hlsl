// Fragment shader for water surfaces (Renderer::DrawWater), drawn after the scene.
//
// For each pixel of the surface:
// 1. Small waves: the surface's direction is tilted by moving noise (three sizes, drifting
//    downwind), so it ripples without any geometry.
// 2. What lies under the water: the scene image as drawn before the water, read a little
//    off to the side where waves bend the view (refraction), and dimmed by how far light
//    travels through the water (each color lost at its own rate per meter, red fastest),
//    while the water's own color, lit by sky and sun, takes over with depth.
// 3. What the water mirrors: a ray reflected off the surface is walked through the depth
//    image (screen-space reflections); where it meets something on screen, that is
//    mirrored; elsewhere the sky's color. With 0 steps only the sky is mirrored.
// 4. The share of each: Fresnel's law (little reflection looking straight down, much at a
//    grazing angle), plus the sun's highlight, shadowed like everything else.
// 5. At the shore, where the water is only centimeters deep, the surface fades into the
//    ground under it, so the edge is soft.
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

// The scene as drawn before the water, and its depth (copied: the water writes depth).
[[vk::combinedImageSampler]][[vk::binding(0, 2)]]
Texture2D<float4> scene_texture : register(t0, space2);
[[vk::combinedImageSampler]][[vk::binding(0, 2)]]
SamplerState scene_sampler : register(s0, space2);
[[vk::combinedImageSampler]][[vk::binding(1, 2)]]
Texture2D<float> scene_depth_texture : register(t1, space2);
[[vk::combinedImageSampler]][[vk::binding(1, 2)]]
SamplerState scene_depth_sampler : register(s1, space2);

cbuffer WaterUniforms : register(b1, space3) {
    float4x4 view_projection;
    float4x4 inverse_view_projection;
    float4 absorption;   // rgb: share of light lost per meter of water, a: how fast the water's own color takes over per meter
    float4 water_color;  // rgb: color of the light the water scatters back, a: roughness of the surface
    float4 waves;        // x: size of the waves in meters, y: their strength, z: time in seconds, w: refraction strength
    float4 wind;         // xy: wind direction, z: wind strength, w: water depth over which the shore fades in, meters
    float4 water_screen; // xy: size of the drawn image, z: reflection steps (0 = sky only), w: longest reflection, meters
};

struct FragmentInput {
    float4 clip_position : SV_Position;
    float3 world_normal : TEXCOORD0;
    float4 color : TEXCOORD1;
    float2 uv : TEXCOORD2;
    float3 world_position : TEXCOORD3;
    float fade : TEXCOORD4;
};

// Random value for a whole-number position, from integer arithmetic (stays exact far from
// the origin, where sin-based hashes break up).
float HashPoint(float2 cell) {
    uint2 q = uint2(int2(cell) + int2(32768, 32768));
    uint h = q.x * 0x8DA6B343u ^ q.y * 0xD8163841u;
    h ^= h >> 13;
    h *= 0x5BD1E995u;
    h ^= h >> 15;
    return float(h & 0xFFFFFFu) / 16777216.0;
}

// Smooth noise and how fast it changes along x and y.
float3 NoiseWithSlope(float2 p) {
    float2 cell = floor(p);
    float2 f = p - cell;
    float2 u = f * f * (3.0 - 2.0 * f);
    float2 du = 6.0 * f * (1.0 - f);
    float a = HashPoint(cell);
    float b = HashPoint(cell + float2(1.0, 0.0));
    float c = HashPoint(cell + float2(0.0, 1.0));
    float d = HashPoint(cell + float2(1.0, 1.0));
    float k = a - b - c + d;
    float value = a + (b - a) * u.x + (c - a) * u.y + k * u.x * u.y;
    float2 slope = du * float2(b - a + k * u.y, c - a + k * u.x);
    return float3(value, slope);
}

// The slope of the water surface at a place: three sizes of noise drifting downwind.
float2 WaveSlope(float2 place) {
    float size = max(waves.x, 0.05);
    float2 drift = wind.xy * (0.4 + wind.z) * waves.z;
    float2 slope = float2(0.0, 0.0);
    float amplitude = 1.0;
    float scale = 1.0 / size;
    [unroll] for (int octave = 0; octave < 3; ++octave) {
        float2 offset = drift * (1.0 + octave * 0.6) + float2(octave * 17.0, octave * 31.0);
        slope += NoiseWithSlope(place * scale + offset * scale).yz * amplitude * scale;
        amplitude *= 0.45;
        scale *= 2.3;
    }
    return slope * size;
}

float4 main(FragmentInput input) : SV_Target0 {
    float3 position = input.world_position;
    float3 to_camera = normalize(camera_position.xyz - position);
    float2 uv = input.clip_position.xy / water_screen.xy;

    // 1. Waves.
    float2 slope = WaveSlope(position.xz) * waves.y * (0.5 + 0.5 * wind.z);
    float3 normal = normalize(float3(-slope.x, 1.0, -slope.y));

    // 2. Under the water. Depth 0 is the sky: treat it as very deep water.
    float ground_depth = scene_depth_texture.Load(int3(int2(input.clip_position.xy), 0));
    float3 ground = ground_depth > 0.0 ? WorldPositionFromDepth(uv, ground_depth, inverse_view_projection) : position - to_camera * 1000.0;
    float water_depth = max(position.y - ground.y, 0.0);
    float2 bent_uv = uv + normal.xz * waves.w * saturate(water_depth * 0.5);
    float bent_depth = scene_depth_texture.SampleLevel(scene_depth_sampler, bent_uv, 0.0);
    float3 bent_ground = bent_depth > 0.0 ? WorldPositionFromDepth(bent_uv, bent_depth, inverse_view_projection) : position - to_camera * 1000.0;
    if (bent_ground.y > position.y - 0.01) {
        // The bent view lands on something above the water (a shore stone, a reed): look straight.
        bent_uv = uv;
        bent_ground = ground;
    }
    float path = length(bent_ground - position);
    float3 under = scene_texture.SampleLevel(scene_sampler, bent_uv, 0.0).rgb;
    float3 transmitted = exp(-absorption.rgb * path);
    float3 to_light = -light_direction.xyz;
    float sun_visibility = SunVisibility(position, float3(0.0, 1.0, 0.0)) * CloudShadow(position);
    float3 light_in_water = ambient_sky.rgb + light_color.rgb * saturate(to_light.y) * sun_visibility * 0.35;
    float scattered = 1.0 - exp(-absorption.a * path);
    float3 refracted = under * transmitted + water_color.rgb * light_in_water * scattered;

    // 3. Reflection: the sky, or what the reflected ray meets on screen.
    float3 reflected_direction = reflect(-to_camera, normal);
    reflected_direction.y = abs(reflected_direction.y);  // waves never mirror the ground under the water
    // The sky with its clouds from the sky light map (a mirror's level); without it, the sky's colors.
    float3 reflection = sky_radiance.y > 0.5 ? SkyRadiance(reflected_direction, 0.0)
                                             : SkyColor(reflected_direction, sky_zenith.rgb, sky_horizon.rgb, sky_ground.rgb);
    int steps = (int)water_screen.z;
    if (steps > 0) {
        float travelled = 0.0;
        float step_length = max(0.5, length(camera_position.xyz - position) * 0.02);
        float growth = pow(max(water_screen.w / (step_length * steps), 1.0), 1.0 / steps);
        [loop] for (int i = 0; i < steps; ++i) {
            travelled += step_length;
            step_length *= growth;
            float3 ray = position + reflected_direction * travelled;
            float4 clip = mul(view_projection, float4(ray, 1.0));
            if (clip.w <= 0.0) {
                break;
            }
            float2 ray_uv = float2(clip.x / clip.w * 0.5 + 0.5, 0.5 - clip.y / clip.w * 0.5);
            if (any(ray_uv < 0.0) || any(ray_uv > 1.0)) {
                break;
            }
            float scene_depth = scene_depth_texture.SampleLevel(scene_depth_sampler, ray_uv, 0.0);
            float ray_depth = clip.z / clip.w;
            // Reversed depth: the ray is behind what the screen shows when its depth is smaller.
            if (scene_depth > 0.0 && ray_depth < scene_depth) {
                float3 surface = WorldPositionFromDepth(ray_uv, scene_depth, inverse_view_projection);
                float behind = length(ray - camera_position.xyz) - length(surface - camera_position.xyz);
                if (behind < step_length * 1.5 + 0.3) {
                    float2 edge = saturate(min(ray_uv, 1.0 - ray_uv) * 12.0);
                    float fade = edge.x * edge.y * (1.0 - (float)i / steps);
                    reflection = lerp(reflection, scene_texture.SampleLevel(scene_sampler, ray_uv, 0.0).rgb, fade);
                }
                break;
            }
        }
    }

    // 4. Fresnel and the sun's highlight.
    float n_dot_v = saturate(dot(normal, to_camera));
    float fresnel = 0.02 + 0.98 * pow(1.0 - n_dot_v, 5.0);
    float3 color = lerp(refracted, reflection, fresnel);
    float roughness = max(water_color.a, 0.02);
    float3 half_vector = normalize(to_light + to_camera);
    float n_dot_h = saturate(dot(normal, half_vector));
    float n_dot_l = saturate(dot(normal, to_light));
    float a2 = roughness * roughness * roughness * roughness;
    float denominator = n_dot_h * n_dot_h * (a2 - 1.0) + 1.0;
    float distribution = a2 / (kPi * denominator * denominator);
    color += light_color.rgb * sun_visibility * distribution * fresnel * n_dot_l * 0.25;

    // Distance fog, as on every other surface (volumetric fog is laid over later).
    if (fog.a > 0.0) {
        float distance_to_camera = length(camera_position.xyz - position);
        color = lerp(color, fog.rgb, 1.0 - exp(-distance_to_camera * fog.a));
    }

    color = ApplyAerialPerspective(color, position);

    // 5. The shore: fade into the ground under the water where it is only centimeters deep.
    float shore = saturate(water_depth / max(wind.w, 0.001));
    float3 ground_seen = scene_texture.SampleLevel(scene_sampler, uv, 0.0).rgb;
    return float4(lerp(ground_seen, color, shore), 1.0);
}

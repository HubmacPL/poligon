// Volumetric fog, at half resolution. For every pixel the line of sight is walked in steps
// from the camera to the first surface. Each step adds the light the fog scatters towards
// the camera there, and removes part of the light coming from behind it. Steps that lie in
// the sun's shadow (or a cloud's) add less, which is what makes shafts of light visible
// between trees.
//
// Output: rgb = light added by the fog, a = part of the scene's light that gets through.
#include "common.hlsli"
#define LIGHTS_NO_UNIFORMS
#include "lights.hlsli"

[[vk::combinedImageSampler]][[vk::binding(0, 2)]]
Texture2D<float> depth_texture : register(t0, space2);
[[vk::combinedImageSampler]][[vk::binding(0, 2)]]
SamplerState depth_sampler : register(s0, space2);

[[vk::combinedImageSampler]][[vk::binding(1, 2)]]
Texture2D<float> shadow_texture : register(t1, space2);
[[vk::combinedImageSampler]][[vk::binding(1, 2)]]
SamplerComparisonState shadow_sampler : register(s1, space2);

// Share of sunlight the clouds let through over the land around the camera.
[[vk::combinedImageSampler]][[vk::binding(2, 2)]]
Texture2D<float> cloud_shadow_texture : register(t2, space2);
[[vk::combinedImageSampler]][[vk::binding(2, 2)]]
SamplerState cloud_shadow_sampler : register(s2, space2);

// The sky light map (lighting.hlsli): the fog can take the sky's color in the direction one looks.
[[vk::combinedImageSampler]][[vk::binding(3, 2)]]
TextureCube<float4> sky_radiance_texture : register(t3, space2);
[[vk::combinedImageSampler]][[vk::binding(3, 2)]]
SamplerState sky_radiance_sampler : register(s3, space2);

cbuffer FogUniforms : register(b0, space3) {
    float4x4 inverse_view_projection;
    float4x4 shadow_matrices[4];
    float4 cascade_far;
    float4 camera_position;
    float4 camera_forward;   // w: the frame's noise offset (moves the step pattern for TAA)
    float4 light_direction;  // xyz: direction the light travels
    float4 light_color;
    float4 fog;              // rgb: color of sunlit fog, a: density per meter at the base height
    float4 fog_shape;        // x: thinning per meter of height, y: base height, z: sun glow strength, w: brightness of shadowed fog
    float4 options;          // x: shadows on, y: farthest distance walked, zw: size of the depth image in pixels
    float4 quality;          // x: steps along each line of sight, y: the sky light map's level for roughness 1, z: share of the sky's color in the fog
    float4 cloud_shadow_area;  // xy: world x and z of the cloud shadow image's corner, z: 1 / its size, w: 1 = on
    LocalLight fog_lights[4];  // the point and spot lights nearest the camera (lights.hlsli)
    float4 fog_light_count;    // x: how many of them
};

struct FragmentInput {
    float4 clip_position : SV_Position;
    float2 uv : TEXCOORD0;
    float2 ndc : TEXCOORD1;
};


float CloudShadow(float3 world_position) {
    if (cloud_shadow_area.w < 0.5) {
        return 1.0;
    }
    float2 uv = (world_position.xz - cloud_shadow_area.xy) * cloud_shadow_area.z;
    return any(uv < 0.0) || any(uv > 1.0) ? 1.0 : cloud_shadow_texture.SampleLevel(cloud_shadow_sampler, uv, 0.0);
}

float SunVisibility(float3 world_position) {
    if (options.x < 0.5) {
        return CloudShadow(world_position);
    }
    float view_depth = dot(world_position - camera_position.xyz, camera_forward.xyz);
    int cascade = 4;
    for (int i = 3; i >= 0; --i) {
        if (view_depth < cascade_far[i]) {
            cascade = i;
        }
    }
    if (cascade == 4) {
        return CloudShadow(world_position);  // beyond the shadow distance
    }
    float4 shadow_position = mul(shadow_matrices[cascade], float4(world_position, 1.0));
    return shadow_texture.SampleCmpLevelZero(shadow_sampler, shadow_position.xy, shadow_position.z) * CloudShadow(world_position);
}

// Point and spot light scattered towards the camera by the fog along one step of the walk,
// per unit of fog density, without the lights' shadows: evenly in every direction (a quarter of
// what arrives in the light's units: the even phase function 1 / (4 pi) times pi). The falling
// off with the square of the distance is integrated exactly over the step (the integral of
// 1 / (h^2 + t^2) is atan(t / h) / h), because a step can be meters long and a sample next to a
// lamp would flash; the range's fade and a spot's cone are taken at the step's jittered point
// (`sample_at`), whose noise temporal anti-aliasing averages away like the rest of the fog's.
float3 LocalLightsInFog(float3 direction, float start, float end, float sample_at) {
    float3 total = float3(0.0, 0.0, 0.0);
    for (int l = 0; l < (int)fog_light_count.x; ++l) {
        LocalLight light = fog_lights[l];
        float3 to_light = light.position_range.xyz - camera_position.xyz;
        float along = dot(to_light, direction);
        float passing = max(length(to_light - direction * along), 0.05);
        float integral = (atan((end - along) / passing) - atan((start - along) / passing)) / passing;
        float3 to_point;
        float point_distance;
        // LocalLightArriving's falling off is replaced by the integral: times the squared distance.
        float3 arriving = LocalLightArriving(light, camera_position.xyz + direction * sample_at, to_point, point_distance);
        total += arriving * max(point_distance * point_distance, 0.01) * integral * 0.25;
    }
    return total;
}

float4 main(FragmentInput input) : SV_Target0 {
    // Each half-resolution pixel stands for a 2 x 2 block; it uses the depth of the block's
    // top-left pixel. fog_composite.frag.hlsl relies on this.
    int2 depth_pixel = min(int2(input.clip_position.xy) * 2, int2(options.zw) - 1);
    float depth = depth_texture.Load(int3(depth_pixel, 0));
    float2 uv = (float2(depth_pixel) + 0.5) / options.zw;
    // Depth runs from 1 at the near plane to 0 far away; 0 is the far plane itself (sky).
    float4 world = mul(inverse_view_projection, float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, depth, 1.0));
    float3 to_surface = world.xyz / world.w - camera_position.xyz;
    float surface_distance = length(to_surface);
    float3 direction = to_surface / surface_distance;
    float walk_distance = depth <= 0.0 ? options.y : min(surface_distance, options.y);

    // Fog scatters most of the sunlight onwards in nearly the same direction, so it glows
    // when looking towards the sun (Henyey-Greenstein phase function).
    float cos_angle = dot(direction, -light_direction.xyz);
    const float g = 0.7;
    float glow = (1.0 - g * g) / (12.566 * pow(1.0 + g * g - 2.0 * g * cos_angle, 1.5));
    float3 sun_glow = light_color.rgb * (glow * fog_shape.z);
    // The fog's own light: its color, or (fog_sky_color) the blurred sky in this direction.
    float3 fog_light = fog.rgb;
    if (quality.z > 0.0) {
        // Somewhat blurred (the level for roughness 1/3) and never below the horizon, where
        // the map holds the ground.
        float3 sky = sky_radiance_texture.SampleLevel(sky_radiance_sampler, normalize(float3(direction.x, max(direction.y, 0.15), direction.z)), quality.y / 3.0).rgb;
        fog_light = lerp(fog.rgb, sky, quality.z);
    }

    // The start of each step is shifted by a per-pixel amount; the resulting noise is
    // removed when the fog is put over the scene.
    float jitter = InterleavedGradientNoise(input.clip_position.xy + float2(camera_forward.w, 0.0));
    int step_count = (int)quality.x;
    float3 added_light = float3(0.0, 0.0, 0.0);
    float let_through = 1.0;
    for (int i = 0; i < step_count; ++i) {
        // Steps grow with distance: short and precise nearby, long far away.
        float start_fraction = float(i) / step_count;
        float end_fraction = float(i + 1) / step_count;
        float start = walk_distance * start_fraction * start_fraction;
        float end = walk_distance * end_fraction * end_fraction;
        float3 position = camera_position.xyz + direction * lerp(start, end, jitter);

        float density = fog.a * exp(-max(position.y - fog_shape.y, 0.0) * fog_shape.x);
        float blocked = 1.0 - exp(-density * (end - start));
        float visibility = SunVisibility(position);
        float3 light = fog_light * lerp(fog_shape.w, 1.0, visibility) + sun_glow * visibility;
        added_light += let_through * (blocked * light + density * LocalLightsInFog(direction, start, end, lerp(start, end, jitter)));
        let_through *= 1.0 - blocked;
    }
    return float4(added_light, let_through);
}

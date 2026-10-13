// Volumetric fog, at half resolution. For every pixel the line of sight is walked in steps
// from the camera to the first surface. Each step adds the light the fog scatters towards
// the camera there, and removes part of the light coming from behind it. Steps that lie in
// the sun's shadow add less, which is what makes shafts of light visible between trees.
//
// Output: rgb = light added by the fog, a = part of the scene's light that gets through.
#include "common.hlsli"

[[vk::combinedImageSampler]][[vk::binding(0, 2)]]
Texture2D<float> depth_texture : register(t0, space2);
[[vk::combinedImageSampler]][[vk::binding(0, 2)]]
SamplerState depth_sampler : register(s0, space2);

[[vk::combinedImageSampler]][[vk::binding(1, 2)]]
Texture2D<float> shadow_texture : register(t1, space2);
[[vk::combinedImageSampler]][[vk::binding(1, 2)]]
SamplerComparisonState shadow_sampler : register(s1, space2);

cbuffer FogUniforms : register(b0, space3) {
    float4x4 inverse_view_projection;
    float4x4 shadow_matrices[4];
    float4 cascade_far;
    float4 camera_position;
    float4 camera_forward;
    float4 light_direction;  // xyz: direction the light travels
    float4 light_color;
    float4 fog;              // rgb: color of sunlit fog, a: density per meter at the base height
    float4 fog_shape;        // x: thinning per meter of height, y: base height, z: sun glow strength, w: brightness of shadowed fog
    float4 options;          // x: shadows on, y: farthest distance walked, zw: size of the depth image in pixels
};

struct FragmentInput {
    float4 clip_position : SV_Position;
    float2 uv : TEXCOORD0;
    float2 ndc : TEXCOORD1;
};

static const int kStepCount = 32;

float SunVisibility(float3 world_position) {
    if (options.x < 0.5) {
        return 1.0;
    }
    float view_depth = dot(world_position - camera_position.xyz, camera_forward.xyz);
    int cascade = 4;
    for (int i = 3; i >= 0; --i) {
        if (view_depth < cascade_far[i]) {
            cascade = i;
        }
    }
    if (cascade == 4) {
        return 1.0;  // beyond the shadow distance
    }
    float4 shadow_position = mul(shadow_matrices[cascade], float4(world_position, 1.0));
    return shadow_texture.SampleCmpLevelZero(shadow_sampler, shadow_position.xy, shadow_position.z);
}

float4 main(FragmentInput input) : SV_Target0 {
    // Each half-resolution pixel stands for a 2 x 2 block; it uses the depth of the block's
    // top-left pixel. fog_composite.frag.hlsl relies on this.
    int2 depth_pixel = min(int2(input.clip_position.xy) * 2, int2(options.zw) - 1);
    float depth = depth_texture.Load(int3(depth_pixel, 0));
    float2 uv = (float2(depth_pixel) + 0.5) / options.zw;
    float4 world = mul(inverse_view_projection, float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, min(depth, 0.9999), 1.0));
    float3 to_surface = world.xyz / world.w - camera_position.xyz;
    float surface_distance = length(to_surface);
    float3 direction = to_surface / surface_distance;
    float walk_distance = depth >= 1.0 ? options.y : min(surface_distance, options.y);

    // Fog scatters most of the sunlight onwards in nearly the same direction, so it glows
    // when looking towards the sun (Henyey-Greenstein phase function).
    float cos_angle = dot(direction, -light_direction.xyz);
    const float g = 0.7;
    float glow = (1.0 - g * g) / (12.566 * pow(1.0 + g * g - 2.0 * g * cos_angle, 1.5));
    float3 sun_glow = light_color.rgb * (glow * fog_shape.z);

    // The start of each step is shifted by a per-pixel amount; the resulting noise is
    // removed when the fog is put over the scene.
    float jitter = InterleavedGradientNoise(input.clip_position.xy);
    float3 added_light = float3(0.0, 0.0, 0.0);
    float let_through = 1.0;
    for (int i = 0; i < kStepCount; ++i) {
        // Steps grow with distance: short and precise nearby, long far away.
        float start_fraction = float(i) / kStepCount;
        float end_fraction = float(i + 1) / kStepCount;
        float start = walk_distance * start_fraction * start_fraction;
        float end = walk_distance * end_fraction * end_fraction;
        float3 position = camera_position.xyz + direction * lerp(start, end, jitter);

        float density = fog.a * exp(-max(position.y - fog_shape.y, 0.0) * fog_shape.x);
        float blocked = 1.0 - exp(-density * (end - start));
        float visibility = SunVisibility(position);
        float3 light = fog.rgb * lerp(fog_shape.w, 1.0, visibility) + sun_glow * visibility;
        added_light += let_through * blocked * light;
        let_through *= 1.0 - blocked;
    }
    return float4(added_light, let_through);
}

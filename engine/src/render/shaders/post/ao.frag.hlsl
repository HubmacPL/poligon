// Ambient occlusion from the depth image: how much of the surroundings of each pixel is
// blocked by nearby geometry. Crevices, the inside of tree crowns and the line where a trunk
// meets the ground get a value below 1 and later receive less ambient light.
//
// Method: the sampling of "Scalable Ambient Obscurance" (McGuire, Mara, Luebke 2012). Points
// on a spiral around the pixel are looked up in the depth image; each one that lies in front
// of the surface, within `radius` meters, darkens the pixel by the cosine of its direction
// from the surface normal, fading out towards `radius`. Unlike SAO's height / distance^2 the
// cosine is at most 1 per sample, so a surface a centimeter away (the ground at a trunk, a
// leaf over a leaf) shades the pixel instead of driving it to black.
#include "common.hlsli"

[[vk::combinedImageSampler]][[vk::binding(0, 2)]]
Texture2D<float> depth_texture : register(t0, space2);
[[vk::combinedImageSampler]][[vk::binding(0, 2)]]
SamplerState depth_sampler : register(s0, space2);

cbuffer AoUniforms : register(b0, space3) {
    float4 projection_info;  // see ViewPosition in common.hlsli
    float4 screen;           // xy: size in pixels, z: samples per pixel
    float4 ao;               // x: radius in meters, y: strength, z: pixels per meter at 1 m distance, w: the frame's noise offset
};

struct FragmentInput {
    float4 clip_position : SV_Position;
    float2 uv : TEXCOORD0;
    float2 ndc : TEXCOORD1;
};

static const float kSpiralTurns = 7.0;
static const float kMaxPixelRadius = 64.0;

float3 PositionAt(int2 pixel) {
    pixel = clamp(pixel, int2(0, 0), int2(screen.xy) - 1);
    float depth = depth_texture.Load(int3(pixel, 0));
    return ViewPosition((float2(pixel) + 0.5) / screen.xy, depth, projection_info);
}

float main(FragmentInput input) : SV_Target0 {
    int2 pixel = int2(input.clip_position.xy);
    float depth = depth_texture.Load(int3(pixel, 0));
    if (depth <= 0.0) {
        return 1.0;  // sky (depth runs from 1 near to 0 far)
    }
    float3 position = ViewPosition((float2(pixel) + 0.5) / screen.xy, depth, projection_info);

    // Surface direction from neighboring depths. Of the two neighbors on each axis the one
    // nearer in depth is used, so that the normal stays right next to an object's outline.
    float3 left = position - PositionAt(pixel + int2(-1, 0));
    float3 right = PositionAt(pixel + int2(1, 0)) - position;
    float3 up = position - PositionAt(pixel + int2(0, -1));
    float3 down = PositionAt(pixel + int2(0, 1)) - position;
    float3 along_x = abs(left.z) < abs(right.z) ? left : right;
    float3 along_y = abs(up.z) < abs(down.z) ? up : down;
    float3 normal = normalize(cross(along_y, along_x));

    float radius = ao.x;
    float distance_to_camera = -position.z;
    float pixel_radius = min(radius * ao.z / distance_to_camera, kMaxPixelRadius);
    if (pixel_radius < 1.5) {
        return 1.0;  // too far away for the effect to cover even a pixel
    }

    int sample_count = (int)screen.z;
    float start_angle = InterleavedGradientNoise(input.clip_position.xy + float2(ao.w, 0.0)) * 6.2831853;
    // Depth precision falls with distance; the bias keeps flat distant surfaces from darkening themselves.
    float bias = 0.03 * radius + 0.002 * distance_to_camera;
    float radius_squared = radius * radius;
    float sum = 0.0;
    for (int i = 0; i < sample_count; ++i) {
        float along = (i + 0.5) / sample_count;
        float angle = start_angle + along * kSpiralTurns * 6.2831853;
        float2 offset = float2(cos(angle), sin(angle)) * (along * pixel_radius);
        float3 to_sample = PositionAt(pixel + int2(offset)) - position;
        float distance_squared = dot(to_sample, to_sample);
        float height = dot(to_sample, normal);
        float falloff = saturate(1.0 - distance_squared / radius_squared);
        sum += falloff * saturate((height - bias) * rsqrt(distance_squared + 1e-6));
    }
    // Scaled so that the floor right at a wall far taller and wider than the radius, which
    // sees exactly half of the sky, comes out at about 0.5 (measured 0.47, effects_render_test).
    float obscurance = sum * 3.0 / sample_count;
    return saturate(1.0 - obscurance * ao.y);
}

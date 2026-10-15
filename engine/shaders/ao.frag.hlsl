// Ambient occlusion from the depth image: how much of the surroundings of each pixel is
// blocked by nearby geometry. Crevices, the inside of tree crowns and the line where a trunk
// meets the ground get a value below 1 and later receive less ambient light.
//
// Method: "Scalable Ambient Obscurance" (McGuire, Mara, Luebke 2012). Points on a spiral
// around the pixel are looked up in the depth image; each one that lies in front of the
// surface, within `radius` meters, darkens the pixel.
#include "common.hlsli"

[[vk::combinedImageSampler]][[vk::binding(0, 2)]]
Texture2D<float> depth_texture : register(t0, space2);
[[vk::combinedImageSampler]][[vk::binding(0, 2)]]
SamplerState depth_sampler : register(s0, space2);

cbuffer AoUniforms : register(b0, space3) {
    float4 projection_info;  // see ViewPosition in common.hlsli
    float4 screen;           // xy: size in pixels
    float4 ao;               // x: radius in meters, y: strength, z: pixels per meter at 1 m distance
};

struct FragmentInput {
    float4 clip_position : SV_Position;
    float2 uv : TEXCOORD0;
    float2 ndc : TEXCOORD1;
};

static const int kSampleCount = 20;
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

    float start_angle = InterleavedGradientNoise(input.clip_position.xy) * 6.2831853;
    // Depth precision falls with distance; the bias keeps flat distant surfaces from darkening themselves.
    float bias = 0.03 * radius + 0.002 * distance_to_camera;
    float radius_squared = radius * radius;
    float sum = 0.0;
    for (int i = 0; i < kSampleCount; ++i) {
        float along = (i + 0.5) / kSampleCount;
        float angle = start_angle + along * kSpiralTurns * 6.2831853;
        float2 offset = float2(cos(angle), sin(angle)) * (along * pixel_radius);
        float3 to_sample = PositionAt(pixel + int2(offset)) - position;
        float distance_squared = dot(to_sample, to_sample);
        float height = dot(to_sample, normal);
        float falloff = max(radius_squared - distance_squared, 0.0);
        sum += falloff * falloff * falloff * max((height - bias) / (distance_squared + 0.0001), 0.0);
    }
    float obscurance = sum * 5.0 / (radius_squared * radius_squared * radius_squared * kSampleCount);
    return saturate(1.0 - obscurance * ao.y);
}

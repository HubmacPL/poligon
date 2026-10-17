// The cloud pass: for every pixel of a smaller image (a quarter or half of the drawn size),
// walks the line of sight through the cloud layer where it sees the sky, adding the light
// the clouds scatter towards the camera and keeping track of how much of the sky behind
// still shows. Output: rgb the clouds' light, a the share of what is behind that still
// shows; laid over the scene by clouds_composite.frag.hlsl.
//
// Each step's light: sunlight dimmed by the cloud between the step and the sun (a few
// coarse steps), shaped by a phase function (bright looking towards the sun, a silver
// lining), with the "powder" darkening of thin edges facing the sun, plus sky light from
// above, stronger near the cloud's top. Every frame starts the steps at a different offset.
//
// Only one pixel of every 2 x 2 block is walked per frame, in turn (weather.y; all of them
// when weather.z is 1); the others keep what they hold, and clouds_resolve.frag.hlsl builds
// the whole image from them and the previous frame's.
#define CLOUDS_SHAPE_BINDING 1
#define CLOUDS_SHAPE_T t1
#define CLOUDS_SHAPE_S s1
#define CLOUDS_DETAIL_BINDING 2
#define CLOUDS_DETAIL_T t2
#define CLOUDS_DETAIL_S s2
#define CLOUDS_WEATHER_BINDING 3
#define CLOUDS_WEATHER_T t3
#define CLOUDS_WEATHER_S s3
#include "clouds.hlsli"

[[vk::combinedImageSampler]][[vk::binding(0, 2)]]
Texture2D<float> depth_texture : register(t0, space2);
[[vk::combinedImageSampler]][[vk::binding(0, 2)]]
SamplerState depth_sampler : register(s0, space2);

struct FragmentInput {
    float4 clip_position : SV_Position;
    float2 uv : TEXCOORD0;
    float2 ndc : TEXCOORD1;
};

// A number that looks random for each input (PCG hash, Jarzynski and Olano 2020).
uint Pcg(uint value) {
    uint state = value * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

// Between 0 and 1, looking random for each pixel block (nested, so rows and columns do not correlate).
float Hash2(uint2 value) {
    return (float)(Pcg(value.x + Pcg(value.y)) >> 8) * (1.0 / 16777216.0);
}

float4 main(FragmentInput input) : SV_Target0 {
    int2 cell = int2(input.clip_position.xy) & 1;
    if (weather.z < 0.5 && (cell.x + cell.y * 2) != (int)weather.y) {
        discard;
    }
    // Only where the scene shows the sky (depth 0) do clouds show; the land is far below them.
    // The depth image is at the drawn size; this image is smaller.
    if (depth_texture.SampleLevel(depth_sampler, input.uv, 0.0) > 0.0) {
        return float4(0.0, 0.0, 0.0, 1.0);
    }
    float4 far_point = mul(inverse_view_projection, float4(input.ndc, 0.000001, 1.0));
    float3 direction = normalize(far_point.xyz / far_point.w - camera_position.xyz);
    float enter;
    float leave;
    if (!LayerSpan(camera_position.xyz, direction, enter, leave)) {
        return float4(0.0, 0.0, 0.0, 1.0);
    }
    // The steps' offset: a random start for each 2 x 2 block, moved on by the golden ratio with
    // every walk, so that over a few walks every pixel's steps cover the step evenly. (A
    // gradient noise such as InterleavedGradientNoise, read at every other pixel, showed as a
    // regular grid of dots.) The four pixels of a block start a quarter step apart.
    uint2 block = uint2(input.clip_position.xy) >> 1;
    float offset = frac(Hash2(block) + camera_position.w + 0.25 * (float)(cell.x + cell.y * 2));
    return MarchClouds(camera_position.xyz, direction, enter, leave, (int)quality.x, (int)quality.y, offset);
}

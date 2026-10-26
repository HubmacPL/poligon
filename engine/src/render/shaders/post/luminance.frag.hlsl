// Automatic exposure, measuring part. Writes one pixel: the brightness the eye is currently
// adapted to. It is the average brightness of the scene, approached gradually from the value
// of the previous frame.
//
// The average is taken of logarithms (the log average), so that a small very bright area
// (the sun, a bright sky between trees) does not outweigh everything else: over 64 x 64
// points of the half-size image (the first level of the bloom chain, each of its pixels a
// blurred mean of the scene pixels around it). The smallest level of the chain was read
// before; its pixels average linearly over large areas, so the sun weighed in before the
// logarithm.

[[vk::combinedImageSampler]][[vk::binding(0, 2)]]
Texture2D<float4> scene_texture : register(t0, space2);
[[vk::combinedImageSampler]][[vk::binding(0, 2)]]
SamplerState scene_sampler : register(s0, space2);

[[vk::combinedImageSampler]][[vk::binding(1, 2)]]
Texture2D<float> previous_texture : register(t1, space2);
[[vk::combinedImageSampler]][[vk::binding(1, 2)]]
SamplerState previous_sampler : register(s1, space2);

cbuffer LuminanceUniforms : register(b0, space3) {
    float4 parameters;  // x, y: share of the way to the new value when it is brighter, darker; z: 1 = first frame
};

struct FragmentInput {
    float4 clip_position : SV_Position;
    float2 uv : TEXCOORD0;
    float2 ndc : TEXCOORD1;
};

float main(FragmentInput input) : SV_Target0 {
    static const int kGrid = 64;
    float log_sum = 0.0;
    for (int y = 0; y < kGrid; ++y) {
        for (int x = 0; x < kGrid; ++x) {
            float2 uv = (float2(x, y) + 0.5) / kGrid;
            float3 color = scene_texture.SampleLevel(scene_sampler, uv, 0.0).rgb;
            log_sum += log(max(dot(color, float3(0.2126, 0.7152, 0.0722)), 0.0001));
        }
    }
    float average = exp(log_sum / (kGrid * kGrid));
    if (parameters.z > 0.5) {
        return average;  // first frame: there is no previous value yet
    }
    float previous = previous_texture.Load(int3(0, 0, 0));
    return lerp(previous, average, average > previous ? parameters.x : parameters.y);
}

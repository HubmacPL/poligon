// Automatic exposure, measuring part. Writes one pixel: the brightness the eye is currently
// adapted to. It is the average brightness of the scene, approached gradually from the value
// of the previous frame.
//
// The average is taken of logarithms (the log average), so that a small very bright area
// (the sun, a bright sky between trees) does not outweigh everything else: over 64 x 64
// points of the half-size image (the first level of the bloom chain, each of its pixels a
// blurred mean of the scene pixels around it), summed in two steps: luminance_blocks.frag.hlsl
// gives the mean logarithm of 16 points per pixel of a 16 x 16 image, read here. The smallest
// level of the chain was read before; its pixels average linearly over large areas, so the
// sun weighed in before the logarithm.

[[vk::combinedImageSampler]][[vk::binding(0, 2)]]
Texture2D<float> blocks_texture : register(t0, space2);
[[vk::combinedImageSampler]][[vk::binding(0, 2)]]
SamplerState blocks_sampler : register(s0, space2);

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
    float log_sum = 0.0;
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            log_sum += blocks_texture.Load(int3(x, y, 0));
        }
    }
    float average = exp(log_sum / 256.0);
    if (parameters.z > 0.5) {
        return average;  // first frame: there is no previous value yet
    }
    float previous = previous_texture.Load(int3(0, 0, 0));
    return lerp(previous, average, average > previous ? parameters.x : parameters.y);
}

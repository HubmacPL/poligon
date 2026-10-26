// Automatic exposure, first half: the mean logarithm of brightness over 4 x 4 points of the
// half-size image (the first level of the bloom chain) for each pixel of a 16 x 16 image, so
// that together they cover 64 x 64 points; luminance.frag.hlsl averages those. Split in two
// because a single pixel reading all 4096 points one after another took about half a millisecond.

[[vk::combinedImageSampler]][[vk::binding(0, 2)]]
Texture2D<float4> scene_texture : register(t0, space2);
[[vk::combinedImageSampler]][[vk::binding(0, 2)]]
SamplerState scene_sampler : register(s0, space2);

struct FragmentInput {
    float4 clip_position : SV_Position;
    float2 uv : TEXCOORD0;
    float2 ndc : TEXCOORD1;
};

float main(FragmentInput input) : SV_Target0 {
    int2 block = int2(input.clip_position.xy);
    float log_sum = 0.0;
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            float2 uv = (float2(block * 4 + int2(x, y)) + 0.5) / 64.0;
            float3 color = scene_texture.SampleLevel(scene_sampler, uv, 0.0).rgb;
            log_sum += log(max(dot(color, float3(0.2126, 0.7152, 0.0722)), 0.0001));
        }
    }
    return log_sum / 16.0;
}

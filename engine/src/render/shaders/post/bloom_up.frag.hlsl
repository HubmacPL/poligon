// Bloom, second half: enlarges a smaller image of the chain with a soft 3 x 3 filter. The
// pass is drawn with blending, which mixes the result into the next larger image; the alpha
// written here is the share of the smaller (blurrier) image in that mix.

[[vk::combinedImageSampler]][[vk::binding(0, 2)]]
Texture2D<float4> source_texture : register(t0, space2);
[[vk::combinedImageSampler]][[vk::binding(0, 2)]]
SamplerState source_sampler : register(s0, space2);

cbuffer UpUniforms : register(b0, space3) {
    float4 parameters;  // xy: size of one source pixel in uv, z: share of the smaller image
};

struct FragmentInput {
    float4 clip_position : SV_Position;
    float2 uv : TEXCOORD0;
    float2 ndc : TEXCOORD1;
};

float3 Tap(float2 uv, float x, float y) {
    return source_texture.SampleLevel(source_sampler, uv + float2(x, y) * parameters.xy, 0.0).rgb;
}

float4 main(FragmentInput input) : SV_Target0 {
    float2 uv = input.uv;
    float3 color = Tap(uv, 0, 0) * 4.0;
    color += (Tap(uv, -1, 0) + Tap(uv, 1, 0) + Tap(uv, 0, -1) + Tap(uv, 0, 1)) * 2.0;
    color += Tap(uv, -1, -1) + Tap(uv, 1, -1) + Tap(uv, -1, 1) + Tap(uv, 1, 1);
    return float4(color / 16.0, parameters.z);
}

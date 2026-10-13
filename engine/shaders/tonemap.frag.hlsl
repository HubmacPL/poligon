// Last pass: turns the wide-range linear image into displayable colors.

[[vk::combinedImageSampler]][[vk::binding(0, 2)]]
Texture2D<float4> scene_texture : register(t0, space2);
[[vk::combinedImageSampler]][[vk::binding(0, 2)]]
SamplerState scene_sampler : register(s0, space2);

cbuffer TonemapUniforms : register(b0, space3) {
    float4 parameters;  // x: exposure (brightness multiplier), y: 0 = none, 1 = ACES filmic curve
};

struct FragmentInput {
    float4 clip_position : SV_Position;
    float2 uv : TEXCOORD0;
    float2 ndc : TEXCOORD1;
};

// Fitted approximation of the ACES filmic curve (Krzysztof Narkowicz): keeps mid-tones,
// rolls bright values off smoothly instead of clipping them to white.
float3 AcesFilmic(float3 x) {
    return saturate((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14));
}

float4 main(FragmentInput input) : SV_Target0 {
    float3 color = scene_texture.Sample(scene_sampler, input.uv).rgb * parameters.x;
    if (parameters.y > 0.5) {
        color = AcesFilmic(color);
    }
    // The target has an sRGB format: the GPU encodes this linear value when storing it.
    return float4(saturate(color), 1.0);
}

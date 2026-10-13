// Last pass: turns the wide-range linear image into displayable colors. Also adds bloom
// (the glow around bright areas) and applies automatic exposure.

[[vk::combinedImageSampler]][[vk::binding(0, 2)]]
Texture2D<float4> scene_texture : register(t0, space2);
[[vk::combinedImageSampler]][[vk::binding(0, 2)]]
SamplerState scene_sampler : register(s0, space2);

// The scene blurred over many sizes (bloom_down.frag.hlsl, bloom_up.frag.hlsl).
[[vk::combinedImageSampler]][[vk::binding(1, 2)]]
Texture2D<float4> bloom_texture : register(t1, space2);
[[vk::combinedImageSampler]][[vk::binding(1, 2)]]
SamplerState bloom_sampler : register(s1, space2);

// One pixel: the brightness the eye is adapted to (luminance.frag.hlsl).
[[vk::combinedImageSampler]][[vk::binding(2, 2)]]
Texture2D<float> luminance_texture : register(t2, space2);
[[vk::combinedImageSampler]][[vk::binding(2, 2)]]
SamplerState luminance_sampler : register(s2, space2);

// One stage's image, shown instead of the frame when a debug view is on.
[[vk::combinedImageSampler]][[vk::binding(3, 2)]]
Texture2D<float4> debug_texture : register(t3, space2);
[[vk::combinedImageSampler]][[vk::binding(3, 2)]]
SamplerState debug_sampler : register(s3, space2);

cbuffer TonemapUniforms : register(b0, space3) {
    float4 parameters;     // x: exposure (brightness multiplier), y: 0 = none, 1 = ACES filmic curve, z: bloom share, w: automatic exposure on
    float4 auto_exposure;  // x: brightness the average is brought to, y: lowest multiplier, z: highest multiplier, w: debug view (0 = off, 1 = show red as gray, 2 = show color)
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
    float3 color = scene_texture.Sample(scene_sampler, input.uv).rgb;
    if (auto_exposure.w > 0.5) {
        float4 debug = debug_texture.SampleLevel(debug_sampler, input.uv, 0.0);
        if (auto_exposure.w < 1.5) {
            return float4(debug.rrr, 1.0);
        }
        color = debug.rgb;
    }
    if (parameters.z > 0.0) {
        float3 bloom = bloom_texture.SampleLevel(bloom_sampler, input.uv, 0.0).rgb;
        color = lerp(color, bloom, parameters.z);
    }

    float exposure = parameters.x;
    if (parameters.w > 0.5) {
        float adapted = max(luminance_texture.Load(int3(0, 0, 0)), 0.0001);
        exposure *= clamp(auto_exposure.x / adapted, auto_exposure.y, auto_exposure.z);
    }
    color *= exposure;

    if (parameters.y > 0.5) {
        color = AcesFilmic(color);
    }
    // The target has an sRGB format: the GPU encodes this linear value when storing it.
    return float4(saturate(color), 1.0);
}

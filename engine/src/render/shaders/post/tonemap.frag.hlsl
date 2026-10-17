// Last pass: turns the wide-range linear image into displayable colors. Also adds bloom
// (the glow around bright areas), applies automatic exposure, and grades the colors:
// white balance, saturation, contrast, a vignette and sharpening (which gives back the
// fine detail temporal anti-aliasing softens).

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
    float4 auto_exposure;  // x: brightness the average is brought to, y: lowest multiplier, z: highest multiplier, w: debug view (0 = off, 1 = show red as gray, 2 = show color, 3 = show motion)
    float4 white_balance;  // rgb: multipliers from temperature and tint (1 = neutral)
    float4 grading;        // x: saturation, y: contrast, z: vignette strength, w: sharpening
    float4 input;          // x: 1 = the scene image has the output's size, 0 = smaller, stretched
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

float Luminance(float3 c) {
    return dot(c, float3(0.2126, 0.7152, 0.0722));
}

float4 main(FragmentInput input_pixel) : SV_Target0 {
    int2 pixel = int2(input_pixel.clip_position.xy);
    bool same_size = input.x > 0.5;
    float3 color = same_size ? scene_texture.Load(int3(pixel, 0)).rgb : scene_texture.SampleLevel(scene_sampler, input_pixel.uv, 0.0).rgb;
    // Sharpening: the pixel minus a share of its four neighbors' difference from it,
    // limited so that bright edges do not ring.
    if (grading.w > 0.0 && same_size) {
        float3 north = scene_texture.Load(int3(pixel + int2(0, -1), 0)).rgb;
        float3 south = scene_texture.Load(int3(pixel + int2(0, 1), 0)).rgb;
        float3 west = scene_texture.Load(int3(pixel + int2(-1, 0), 0)).rgb;
        float3 east = scene_texture.Load(int3(pixel + int2(1, 0), 0)).rgb;
        float3 neighbors = (north + south + west + east) * 0.25;
        float3 lowest = min(color, min(min(north, south), min(west, east)));
        float3 highest = max(color, max(max(north, south), max(west, east)));
        color = clamp(color + (color - neighbors) * grading.w, lowest, highest);
    }
    if (auto_exposure.w > 0.5) {
        float4 debug = debug_texture.SampleLevel(debug_sampler, input_pixel.uv, 0.0);
        if (auto_exposure.w > 2.5) {
            // Motion since the previous frame: gray-yellow = still, more red = moved left, less
            // = right, more green = moved up; blue = none written (worked out from depth).
            if (debug.x > 3.0) {
                return float4(0.0, 0.0, 0.5, 1.0);
            }
            return float4(saturate(0.5 + debug.xy * 25.0), 0.0, 1.0);
        }
        if (auto_exposure.w < 1.5) {
            return float4(debug.rrr, 1.0);
        }
        color = debug.rgb;
    }
    if (parameters.z > 0.0) {
        float3 bloom = bloom_texture.SampleLevel(bloom_sampler, input_pixel.uv, 0.0).rgb;
        color = lerp(color, bloom, parameters.z);
    }

    float exposure = parameters.x;
    if (parameters.w > 0.5) {
        float adapted = max(luminance_texture.Load(int3(0, 0, 0)), 0.0001);
        exposure *= clamp(auto_exposure.x / adapted, auto_exposure.y, auto_exposure.z);
    }
    color *= exposure;

    // Grading in linear light, before the curve: white balance, then saturation around
    // the pixel's own brightness.
    color *= white_balance.rgb;
    color = max(lerp(Luminance(color).xxx, color, grading.x), 0.0);

    if (parameters.y > 0.5) {
        color = AcesFilmic(color);
    }
    // Contrast around middle gray, in a perceptual (square root) scale so that it acts on
    // shadows and lights evenly.
    float3 perceptual = sqrt(saturate(color));
    perceptual = saturate((perceptual - 0.5) * grading.y + 0.5);
    color = perceptual * perceptual;
    // Vignette: corners darken gently, as through a camera lens.
    if (grading.z > 0.0) {
        float2 from_center = input_pixel.uv * 2.0 - 1.0;
        color *= 1.0 - grading.z * saturate(dot(from_center, from_center) * 0.5);
    }
    // The target has an sRGB format: the GPU encodes this linear value when storing it.
    return float4(saturate(color), 1.0);
}

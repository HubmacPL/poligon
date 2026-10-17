// Temporal anti-aliasing (TAA), optionally with upscaling: every frame the scene is drawn
// with the camera shifted by a different fraction of a pixel, possibly at a lower
// resolution than the output, and this pass blends it into the result of the previous
// frames, which is kept at the output resolution. Over a few frames every output pixel
// collects samples from many places inside it: edges, thin twigs, shimmering highlights and
// dithered fades smooth out, and a lower drawing resolution is filled back in.
//
// For each output pixel:
// 1. The new frame's estimate: the drawn pixels around this point, each weighted by how
//    close its (shifted) sample position lies to the output pixel's center.
// 2. Where was this point of the scene in the previous frame? The motion image (motion
//    vectors from the depth pass) says it for meshes, animals and plants in the wind; where
//    nothing wrote one (sky, impostors), the depth image gives the point's position and the
//    cameras of both frames, without their shifts, its motion. The previous result is read there.
// 3. Anything the motion misses (shading changes, things uncovered) is caught by clipping
//    the history into the colors the new frame's neighborhood holds, which removes ghosts. Colors are
//    compared in YCoCg (brightness and two color differences), squeezed by brightness so
//    that a few very bright pixels cannot dominate.
//
// Output: the new result, which the next frame reads as its history.
#include "common.hlsli"

[[vk::combinedImageSampler]][[vk::binding(0, 2)]]
Texture2D<float4> current_texture : register(t0, space2);
[[vk::combinedImageSampler]][[vk::binding(0, 2)]]
SamplerState current_sampler : register(s0, space2);

[[vk::combinedImageSampler]][[vk::binding(1, 2)]]
Texture2D<float4> history_texture : register(t1, space2);
[[vk::combinedImageSampler]][[vk::binding(1, 2)]]
SamplerState history_sampler : register(s1, space2);

[[vk::combinedImageSampler]][[vk::binding(2, 2)]]
Texture2D<float> depth_texture : register(t2, space2);
[[vk::combinedImageSampler]][[vk::binding(2, 2)]]
SamplerState depth_sampler : register(s2, space2);

// Each pixel's motion since the previous frame, in texture coordinates; above 3 = none written.
[[vk::combinedImageSampler]][[vk::binding(3, 2)]]
Texture2D<float2> motion_texture : register(t3, space2);
[[vk::combinedImageSampler]][[vk::binding(3, 2)]]
SamplerState motion_sampler : register(s3, space2);

cbuffer TaaUniforms : register(b0, space3) {
    float4x4 inverse_view_projection;   // this frame, with its sub-pixel shift
    float4x4 current_view_projection;   // this frame, without its shift
    float4x4 previous_view_projection;  // the previous frame, without its shift
    float4 parameters;                  // x: share of the new frame, y: 1 = history is valid, zw: output size in pixels
    float4 input;                       // xy: size of the drawn image in pixels, zw: the frame's shift in drawn pixels
};

struct FragmentInput {
    float4 clip_position : SV_Position;
    float2 uv : TEXCOORD0;
    float2 ndc : TEXCOORD1;
};

float3 ToYCoCg(float3 c) {
    return float3(0.25 * c.r + 0.5 * c.g + 0.25 * c.b, 0.5 * c.r - 0.5 * c.b, -0.25 * c.r + 0.5 * c.g - 0.25 * c.b);
}

float3 FromYCoCg(float3 c) {
    return float3(c.x + c.y - c.z, c.x + c.z, c.x - c.y - c.z);
}

// Wide-range colors squeezed into 0..1 by brightness, so that statistics and blending are
// not ruled by a few very bright pixels (Karis 2014). Undone after blending.
float3 Squeeze(float3 c) {
    return c / (1.0 + max(c.r, max(c.g, c.b)));
}

float3 Unsqueeze(float3 c) {
    return c / max(1.0 - max(c.r, max(c.g, c.b)), 0.0001);
}

// The history read with a Catmull-Rom filter from 5 bilinear reads: sharper than plain
// bilinear, which would blur the picture a little more every frame.
float3 SampleHistory(float2 uv, float2 size) {
    float2 position = uv * size;
    float2 center = floor(position - 0.5) + 0.5;
    float2 f = position - center;
    float2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
    float2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
    float2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
    float2 w3 = f * f * (-0.5 + 0.5 * f);
    float2 w12 = w1 + w2;
    float2 tc0 = (center - 1.0) / size;
    float2 tc3 = (center + 2.0) / size;
    float2 tc12 = (center + w2 / w12) / size;
    float3 result = history_texture.SampleLevel(history_sampler, float2(tc12.x, tc0.y), 0.0).rgb * (w12.x * w0.y) +
                    history_texture.SampleLevel(history_sampler, float2(tc0.x, tc12.y), 0.0).rgb * (w0.x * w12.y) +
                    history_texture.SampleLevel(history_sampler, float2(tc12.x, tc12.y), 0.0).rgb * (w12.x * w12.y) +
                    history_texture.SampleLevel(history_sampler, float2(tc3.x, tc12.y), 0.0).rgb * (w3.x * w12.y) +
                    history_texture.SampleLevel(history_sampler, float2(tc12.x, tc3.y), 0.0).rgb * (w12.x * w3.y);
    float weight = w12.x * w0.y + w0.x * w12.y + w12.x * w12.y + w3.x * w12.y + w12.x * w3.y;
    return max(result / weight, 0.0);
}

// Moves `history` towards the middle of the box until it lies inside it (Playdead's
// "clip towards the center": keeps the color's direction, unlike clamping each channel).
float3 ClipToBox(float3 history, float3 box_min, float3 box_max) {
    float3 center = 0.5 * (box_max + box_min);
    float3 extent = 0.5 * (box_max - box_min) + 0.0001;
    float3 offset = history - center;
    float3 units = abs(offset / extent);
    float largest = max(units.x, max(units.y, units.z));
    return largest > 1.0 ? center + offset / largest : history;
}

float2 ToUv(float4 clip) {
    return float2(clip.x / clip.w * 0.5 + 0.5, 0.5 - clip.y / clip.w * 0.5);
}

float4 main(FragmentInput input_pixel) : SV_Target0 {
    float2 output_size = parameters.zw;
    float2 input_size = input.xy;
    float2 uv = input_pixel.uv;
    // Where this output pixel's center lies in the drawn image, which is shifted by the
    // frame's sub-pixel offset.
    float2 drawn_position = uv * input_size + input.zw;
    int2 nearest_drawn = int2(floor(drawn_position));
    int2 last = int2(input_size) - 1;
    float2 output_per_drawn = output_size / input_size;

    // The 3 x 3 drawn pixels around it: a weighted estimate of the new color, the spread of
    // the neighborhood in YCoCg, and the nearest surface (which decides the motion, so that
    // edges of near objects move with them instead of with the background behind them).
    float3 sum = 0.0;
    float3 sum_squares = 0.0;
    float3 lowest = 1.0e9;
    float3 highest = -1.0e9;
    float3 estimate = 0.0;
    float estimate_weight = 0.0;
    float nearest_depth = 0.0;
    int2 nearest_pixel = clamp(nearest_drawn, int2(0, 0), last);
    [unroll] for (int y = -1; y <= 1; ++y) {
        [unroll] for (int x = -1; x <= 1; ++x) {
            int2 tap = clamp(nearest_drawn + int2(x, y), int2(0, 0), last);
            float3 color = Squeeze(max(current_texture.Load(int3(tap, 0)).rgb, 0.0));
            // Distance from the tap's sample position to the output pixel's center, in output
            // pixels; a Gaussian close to the Blackman-Harris window weights it.
            float2 offset = (float2(tap) + 0.5 - drawn_position) * output_per_drawn;
            float weight = exp(-2.29 * dot(offset, offset));
            estimate += color * weight;
            estimate_weight += weight;
            float3 ycocg = ToYCoCg(color);
            sum += ycocg;
            sum_squares += ycocg * ycocg;
            lowest = min(lowest, ycocg);
            highest = max(highest, ycocg);
            float depth = depth_texture.Load(int3(tap, 0));
            if (depth > nearest_depth) {  // depth runs from 1 near to 0 far
                nearest_depth = depth;
                nearest_pixel = tap;
            }
        }
    }
    float3 current = estimate / max(estimate_weight, 0.0001);
    if (parameters.y < 0.5) {
        // No history yet: the drawn pixel under this point, as a frame without TAA shows it.
        return float4(max(current_texture.Load(int3(clamp(nearest_drawn, int2(0, 0), last), 0)).rgb, 0.0), 1.0);
    }

    // The nearest surface's motion: its own, or from its position and the two cameras.
    float2 motion = motion_texture.Load(int3(nearest_pixel, 0));
    bool behind_camera = false;
    if (motion.x > 3.0) {
        float2 nearest_uv = (float2(nearest_pixel) + 0.5) / input_size;
        float4 world = mul(inverse_view_projection, float4(nearest_uv.x * 2.0 - 1.0, 1.0 - nearest_uv.y * 2.0, nearest_depth, 1.0));
        world /= world.w;
        float4 previous = mul(previous_view_projection, float4(world.xyz, 1.0));
        float4 now = mul(current_view_projection, float4(world.xyz, 1.0));
        motion = ToUv(previous) - ToUv(now);
        behind_camera = previous.w <= 0.0;
    }
    float2 history_uv = uv + motion;
    if (any(history_uv < 0.0) || any(history_uv > 1.0) || behind_camera) {
        return float4(Unsqueeze(current), 1.0);  // off screen in the previous frame
    }

    // Keep only history colors the neighborhood could produce: mean plus or minus 1.25
    // standard deviations, never beyond the neighborhood's own extremes.
    float3 mean = sum / 9.0;
    float3 deviation = sqrt(max(sum_squares / 9.0 - mean * mean, 0.0));
    float3 box_min = max(mean - 1.25 * deviation, lowest);
    float3 box_max = min(mean + 1.25 * deviation, highest);
    float3 history = ToYCoCg(Squeeze(SampleHistory(history_uv, output_size)));
    history = ClipToBox(history, box_min, box_max);

    // The faster the pixel moves, the more of the new frame it takes: moving history has
    // been resampled more often and is blurrier. When upscaling, a frame whose samples fall
    // far from this pixel's center counts for less.
    float speed = length(motion * output_size);
    float share = lerp(parameters.x, 0.25, saturate(speed / 16.0));
    float closeness = saturate(estimate_weight);
    share *= lerp(0.5, 1.0, closeness);
    float3 result = lerp(FromYCoCg(history), current, share);
    return float4(Unsqueeze(max(result, 0.0)), 1.0);
}

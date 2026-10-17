// Builds this frame's cloud image from the pixels the cloud pass walked this frame and the
// image of the frame before. The cloud pass walks one pixel of every 2 x 2 block per frame,
// in turn (clouds.frag.hlsl); every other pixel takes the previous image at the place its
// line of sight showed there, moved with the camera's turn. A pixel walked this frame is
// also mixed with what it was, which averages away the step pattern that otherwise shows as
// streaks. Where the previous image has nothing (a camera jump, the first frame, the edge
// of the screen), the pixel walked this frame in the same block stands in.

[[vk::combinedImageSampler]][[vk::binding(0, 2)]]
Texture2D<float4> walked_texture : register(t0, space2);
[[vk::combinedImageSampler]][[vk::binding(0, 2)]]
SamplerState walked_sampler : register(s0, space2);
[[vk::combinedImageSampler]][[vk::binding(1, 2)]]
Texture2D<float4> history_texture : register(t1, space2);
[[vk::combinedImageSampler]][[vk::binding(1, 2)]]
SamplerState history_sampler : register(s1, space2);

cbuffer CloudResolveUniforms : register(b0, space3) {
    float4x4 inverse_view_projection;   // this frame's camera, as the cloud pass used it
    float4x4 previous_view_projection;  // the previous frame's
    float4 camera_position;
    float4 params;  // x: which pixel of each 2 x 2 block was walked (0..3), y: 1 = every pixel was walked, z: 1 = the previous image is usable, w: share of a walked pixel's new value
};

struct FragmentInput {
    float4 clip_position : SV_Position;
    float2 uv : TEXCOORD0;
    float2 ndc : TEXCOORD1;
};

float4 main(FragmentInput input) : SV_Target0 {
    int2 pixel = int2(input.clip_position.xy);
    int2 cell = pixel & 1;
    int phase = (int)params.x;
    bool walked = params.y > 0.5 || (cell.x + cell.y * 2) == phase;
    float4 fresh = walked_texture.Load(int3(pixel, 0));

    // Where this pixel's line of sight was on the previous frame's image: clouds are
    // kilometers away, so only the camera's turn matters.
    float4 far_point = mul(inverse_view_projection, float4(input.ndc, 0.000001, 1.0));
    float3 direction = normalize(far_point.xyz / far_point.w - camera_position.xyz);
    float4 previous_clip = mul(previous_view_projection, float4(camera_position.xyz + direction * 5000.0, 1.0));
    float2 previous_uv = previous_clip.xy / previous_clip.w * float2(0.5, -0.5) + 0.5;
    bool usable = params.z > 0.5 && previous_clip.w > 0.0 && all(previous_uv >= 0.0) && all(previous_uv <= 1.0);
    float4 history = history_texture.SampleLevel(history_sampler, previous_uv, 0.0);

    if (walked) {
        return usable ? lerp(history, fresh, params.w) : fresh;
    }
    if (usable) {
        return history;
    }
    // The pixel walked this frame in the same 2 x 2 block.
    int2 stand_in = (pixel & ~1) + int2(phase & 1, phase >> 1);
    return walked_texture.Load(int3(stand_in, 0));
}

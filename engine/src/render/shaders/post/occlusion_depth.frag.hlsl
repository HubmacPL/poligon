// Occlusion culling, first half: shrinks the depth pass's image to 128 x 72 pixels, each the
// farthest depth (reversed depth: the smallest value) of the block of the image it covers, so
// that whatever lies behind that depth anywhere in the block is surely hidden.

[[vk::combinedImageSampler]][[vk::binding(0, 2)]]
Texture2D<float> depth_texture : register(t0, space2);
[[vk::combinedImageSampler]][[vk::binding(0, 2)]]
SamplerState depth_sampler : register(s0, space2);

cbuffer OcclusionUniforms : register(b0, space3) {
    float4 sizes;  // xy: the depth image's size in pixels, zw: the shrunk image's size
};

struct FragmentInput {
    float4 clip_position : SV_Position;
    float2 uv : TEXCOORD0;
    float2 ndc : TEXCOORD1;
};

float main(FragmentInput input) : SV_Target0 {
    int2 cell = int2(input.clip_position.xy);
    int2 first = int2(floor(float2(cell) * sizes.xy / sizes.zw));
    int2 last = min(int2(ceil(float2(cell + 1) * sizes.xy / sizes.zw)), int2(sizes.xy)) - 1;
    float farthest = 1.0;
    for (int y = first.y; y <= last.y; ++y) {
        for (int x = first.x; x <= last.x; ++x) {
            farthest = min(farthest, depth_texture.Load(int3(x, y, 0)));
        }
    }
    return farthest;
}

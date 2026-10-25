// 2D over the finished picture: the texture (white for plain rectangles, a glyph atlas for
// text) times the quad's color. The output image is sRGB-encoded; blending happens in linear
// light and the card encodes the result.

[[vk::combinedImageSampler]][[vk::binding(0, 2)]]
Texture2D<float4> overlay_texture : register(t0, space2);
[[vk::combinedImageSampler]][[vk::binding(0, 2)]]
SamplerState overlay_sampler : register(s0, space2);

struct FragmentInput {
    float4 clip_position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : TEXCOORD1;
};

float4 main(FragmentInput input) : SV_Target0 {
    return overlay_texture.Sample(overlay_sampler, input.uv) * input.color;
}

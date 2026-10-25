// 2D over the finished picture (Renderer::DrawRect, DrawSprite, DrawText). Every quad of the
// frame sits in one buffer, 48 bytes each: its rectangle in pixels of the output image, its
// texture rectangle and its linear color (OverlayQuad in overlay_state.h). A batch is drawn as
// six vertices per quad, one instance per quad, without a vertex buffer.

[[vk::binding(0, 0)]]
ByteAddressBuffer quads : register(t0, space0);

cbuffer OverlayUniforms : register(b0, space1) {
    float4 screen;  // xy: output size in pixels, z: index of this batch's first quad
};

struct VertexOutput {
    float4 clip_position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : TEXCOORD1;
};

VertexOutput main(uint vertex_index : SV_VertexID, uint instance : SV_InstanceID) {
    static const float2 kCorners[6] = {float2(0.0, 0.0), float2(1.0, 0.0), float2(1.0, 1.0),
                                       float2(0.0, 0.0), float2(1.0, 1.0), float2(0.0, 1.0)};
    uint address = ((uint)screen.z + instance) * 48;
    float4 rect = asfloat(quads.Load4(address));
    float4 uv = asfloat(quads.Load4(address + 16));
    float4 color = asfloat(quads.Load4(address + 32));
    float2 corner = kCorners[vertex_index];
    float2 pixel = rect.xy + corner * rect.zw;
    VertexOutput output;
    output.clip_position = float4(pixel.x / screen.x * 2.0 - 1.0, 1.0 - pixel.y / screen.y * 2.0, 0.0, 1.0);
    output.uv = lerp(uv.xy, uv.zw, corner);
    output.color = color;
    return output;
}

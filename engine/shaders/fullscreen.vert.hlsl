// Vertex shader for passes that cover the whole image (sky, tone mapping).
// Draw three vertices without any vertex buffer: one large triangle covers the screen.

struct VertexOutput {
    float4 clip_position : SV_Position;
    float2 uv : TEXCOORD0;   // (0, 0) top-left, (1, 1) bottom-right
    float2 ndc : TEXCOORD1;  // (-1, -1) bottom-left, (1, 1) top-right
};

VertexOutput main(uint vertex_index : SV_VertexID) {
    VertexOutput output;
    float2 corner = float2((vertex_index << 1) & 2, vertex_index & 2);  // (0,0), (2,0), (0,2)
    output.ndc = corner * 2.0 - 1.0;
    output.uv = float2(corner.x, 1.0 - corner.y);
    output.clip_position = float4(output.ndc, 1.0, 1.0);
    return output;
}

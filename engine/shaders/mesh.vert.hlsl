// Vertex shader for lit, single-color meshes.
// Register spaces follow SDL GPU rules: vertex shader uniform buffers live in space1.
// Vertex inputs use TEXCOORDn semantics, where n is the attribute location.

cbuffer FrameUniforms : register(b0, space1) {
    float4x4 view_projection;
};

cbuffer ObjectUniforms : register(b1, space1) {
    float4x4 model;
    float4 object_color;
};

struct VertexInput {
    float3 position : TEXCOORD0;
    float3 normal : TEXCOORD1;
};

struct VertexOutput {
    float4 clip_position : SV_Position;
    float3 world_normal : TEXCOORD0;
    float4 color : TEXCOORD1;
};

VertexOutput main(VertexInput input) {
    VertexOutput output;
    float4 world_position = mul(model, float4(input.position, 1.0));
    output.clip_position = mul(view_projection, world_position);
    // Correct for rotation and for scaling along the mesh axes; a general non-uniform
    // scale combined with rotation would need the inverse transpose of the model matrix.
    output.world_normal = mul((float3x3)model, input.normal);
    output.color = object_color;
    return output;
}

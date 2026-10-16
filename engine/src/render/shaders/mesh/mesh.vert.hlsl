// Vertex shader for lit meshes.
// Register spaces follow SDL GPU rules: vertex shader uniform buffers live in space1.
// Vertex inputs use TEXCOORDn semantics, where n is the attribute location.
#include "common.hlsli"

cbuffer FrameUniforms : register(b0, space1) {
    float4x4 view_projection;
    float4 wind;  // see ApplyWind in common.hlsli
};

cbuffer ObjectUniforms : register(b1, space1) {
    float4x4 model;
    float4 object_color;
    float4 uv_scale;  // xy: multiplier for texture coordinates
    float4 wind_response;  // x: bend, y: flutter (see ApplyWind)
};

struct VertexInput {
    float3 position : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float2 uv : TEXCOORD2;
};

struct VertexOutput {
    float4 clip_position : SV_Position;
    float3 world_normal : TEXCOORD0;
    float4 color : TEXCOORD1;
    float2 uv : TEXCOORD2;
    float3 world_position : TEXCOORD3;
    float fade : TEXCOORD4;  // see KeptByFade in common.hlsli
};

VertexOutput main(VertexInput input) {
    VertexOutput output;
    float4 world_position = mul(model, float4(input.position, 1.0));
    world_position.xyz = ApplyWind(world_position.xyz, float3(model._m03, model._m13, model._m23), input.position, wind, wind_response.xy);
    output.clip_position = mul(view_projection, world_position);
    // Correct for rotation and for scaling along the mesh axes; a general non-uniform
    // scale combined with rotation would need the inverse transpose of the model matrix.
    output.world_normal = mul((float3x3)model, input.normal);
    output.color = object_color;
    output.uv = input.uv * uv_scale.xy;
    output.world_position = world_position.xyz;
    output.fade = 1.0;
    return output;
}

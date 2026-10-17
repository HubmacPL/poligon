// Vertex shader for lit meshes.
// Register spaces follow SDL GPU rules: vertex shader uniform buffers live in space1.
// Vertex inputs use TEXCOORDn semantics, where n is the attribute location.
#include "common.hlsli"

cbuffer FrameUniforms : register(b0, space1) {
    float4x4 view_projection;
    float4 wind;     // see ApplyWind in common.hlsli
    float4 trample;  // see mesh_instanced.vert.hlsl
    // Motion vectors (depth pass only, motion.x = 1): where each vertex is without the
    // camera's sub-pixel shift, and where it was in the previous frame.
    float4x4 motion_view_projection;    // this frame's camera without its shift
    float4x4 previous_view_projection;  // the previous frame's, without its shift
    float4 previous_wind;               // the previous frame's wind
    float4 motion;                      // x: 1 = work out the motion
};

cbuffer ObjectUniforms : register(b1, space1) {
    float4x4 model;
    float4 object_color;
    float4 uv_scale;  // xy: multiplier for texture coordinates
    float4 wind_response;  // x: bend, y: flutter (see ApplyWind)
    float4x4 previous_model;  // the transform in the previous frame (motion vectors)
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
    float4 motion_clip : TEXCOORD5;    // without the camera's shift (motion vectors, depth pass only)
    float4 previous_clip : TEXCOORD6;  // where the vertex was in the previous frame
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
    output.motion_clip = 0.0;
    output.previous_clip = 0.0;
    if (motion.x > 0.0) {
        float4 previous_world = mul(previous_model, float4(input.position, 1.0));
        previous_world.xyz = ApplyWind(previous_world.xyz, float3(previous_model._m03, previous_model._m13, previous_model._m23),
                                       input.position, previous_wind, wind_response.xy);
        output.motion_clip = mul(motion_view_projection, world_position);
        output.previous_clip = mul(previous_view_projection, previous_world);
    }
    return output;
}

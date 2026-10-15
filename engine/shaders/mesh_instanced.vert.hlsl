// Vertex shader for instanced meshes: one mesh drawn many times in one call, each time
// with its own transform. This is how forests are drawn. Same outputs as mesh.vert.hlsl,
// so the same fragment shaders are used.
//
// The transforms of every instance drawn this frame sit in one buffer; `uv_scale.z` says
// where this call's instances start in it. The buffer is read as plain bytes, 64 per matrix
// (four columns of four floats), for the reason given in mesh_skinned.vert.hlsl. The last
// float of each matrix, 1 in an ordinary transform, carries the instance's fade between
// levels of detail (see KeptByFade in common.hlsli).
#include "common.hlsli"

[[vk::binding(0, 0)]]
ByteAddressBuffer instance_matrices : register(t0, space0);

cbuffer FrameUniforms : register(b0, space1) {
    float4x4 view_projection;
    float4 wind;  // see ApplyWind in common.hlsli
};

cbuffer ObjectUniforms : register(b1, space1) {
    float4x4 model;   // unused: every instance has its own
    float4 object_color;
    float4 uv_scale;  // xy: multiplier for texture coordinates, z: index of this call's first instance matrix
    float4 wind_response;  // x: bend, y: flutter (see ApplyWind)
};

struct VertexInput {
    float3 position : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float2 uv : TEXCOORD2;
    uint instance : SV_InstanceID;
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
    uint address = ((uint)uv_scale.z + input.instance) * 64;
    float3 column0 = asfloat(instance_matrices.Load3(address));
    float3 column1 = asfloat(instance_matrices.Load3(address + 16));
    float3 column2 = asfloat(instance_matrices.Load3(address + 32));
    float4 column3 = asfloat(instance_matrices.Load4(address + 48));

    VertexOutput output;
    float3 world_position = column0 * input.position.x + column1 * input.position.y + column2 * input.position.z + column3.xyz;
    world_position = ApplyWind(world_position, column3.xyz, input.position, wind, wind_response.xy);
    output.clip_position = mul(view_projection, float4(world_position, 1.0));
    // Correct for rotation and for scaling that is the same along all axes.
    output.world_normal = column0 * input.normal.x + column1 * input.normal.y + column2 * input.normal.z;
    output.color = object_color;
    output.uv = input.uv * uv_scale.xy;
    output.world_position = world_position;
    output.fade = column3.w;
    return output;
}

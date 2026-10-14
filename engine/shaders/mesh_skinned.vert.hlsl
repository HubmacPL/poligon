// Vertex shader for skinned meshes: each vertex follows up to four joints of a skeleton.
// Same outputs as mesh.vert.hlsl, so the same fragment shaders are used.
//
// The joint matrices of every skinned mesh drawn this frame sit in one buffer; `uv_scale.z`
// says where this mesh's joints start in it. SDL GPU rule: storage buffers of a vertex
// shader live in space0.

// Read as plain bytes: SDL GPU gives Direct3D 12 a view of the buffer without a structure,
// which a StructuredBuffer cannot read (it silently returns zeros). Each matrix is 64 bytes:
// its four columns, four floats each.
[[vk::binding(0, 0)]]
ByteAddressBuffer joint_matrices : register(t0, space0);

cbuffer FrameUniforms : register(b0, space1) {
    float4x4 view_projection;
};

cbuffer ObjectUniforms : register(b1, space1) {
    float4x4 model;
    float4 object_color;
    float4 uv_scale;  // xy: multiplier for texture coordinates, z: index of this mesh's first joint matrix
};

struct VertexInput {
    float3 position : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float2 uv : TEXCOORD2;
    uint4 joints : TEXCOORD3;
    float4 weights : TEXCOORD4;
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
    // Blend the positions and normals that each of the four joints would give the vertex.
    uint first_joint = (uint)uv_scale.z;
    float3 position = float3(0.0, 0.0, 0.0);
    float3 normal = float3(0.0, 0.0, 0.0);
    for (int i = 0; i < 4; ++i) {
        uint address = (first_joint + input.joints[i]) * 64;
        float3 column0 = asfloat(joint_matrices.Load3(address));
        float3 column1 = asfloat(joint_matrices.Load3(address + 16));
        float3 column2 = asfloat(joint_matrices.Load3(address + 32));
        float3 column3 = asfloat(joint_matrices.Load3(address + 48));
        float weight = input.weights[i];
        position += weight * (column0 * input.position.x + column1 * input.position.y + column2 * input.position.z + column3);
        normal += weight * (column0 * input.normal.x + column1 * input.normal.y + column2 * input.normal.z);
    }

    VertexOutput output;
    float4 world_position = mul(model, float4(position, 1.0));
    output.clip_position = mul(view_projection, world_position);
    output.world_normal = mul((float3x3)model, normal);
    output.color = object_color;
    output.uv = input.uv * uv_scale.xy;
    output.world_position = world_position.xyz;
    output.fade = 1.0;
    return output;
}

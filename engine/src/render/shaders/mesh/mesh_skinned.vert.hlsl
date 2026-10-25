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
    float4 uv_scale;  // xy: multiplier for texture coordinates, z: index of this mesh's first joint matrix, w: the same for its pose in the previous frame
    float4 wind_response;  // unused here
    float4x4 previous_model;  // the transform in the previous frame (motion vectors)
    float4x4 normal_matrix;   // inverse transpose of the model's 3 x 3 part: normals under any scale
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
    float4 motion_clip : TEXCOORD5;    // without the camera's shift (motion vectors, depth pass only)
    float4 previous_clip : TEXCOORD6;  // where the vertex was in the previous frame
};

// The vertex placed by the pose whose joint matrices start at `first_joint`: a blend of the
// positions and normals that each of its four joints would give it.
void Skin(VertexInput input, uint first_joint, out float3 position, out float3 normal) {
    position = float3(0.0, 0.0, 0.0);
    normal = float3(0.0, 0.0, 0.0);
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
}

VertexOutput main(VertexInput input) {
    float3 position;
    float3 normal;
    Skin(input, (uint)uv_scale.z, position, normal);

    VertexOutput output;
    float4 world_position = mul(model, float4(position, 1.0));
    output.clip_position = mul(view_projection, world_position);
    output.world_normal = mul((float3x3)normal_matrix, normal);
    output.color = object_color;
    output.uv = input.uv * uv_scale.xy;
    output.world_position = world_position.xyz;
    output.fade = 1.0;
    output.motion_clip = 0.0;
    output.previous_clip = 0.0;
    if (motion.x > 0.0) {
        float3 previous_position;
        float3 previous_normal;
        Skin(input, (uint)uv_scale.w, previous_position, previous_normal);
        output.motion_clip = mul(motion_view_projection, world_position);
        output.previous_clip = mul(previous_view_projection, mul(previous_model, float4(previous_position, 1.0)));
    }
    return output;
}

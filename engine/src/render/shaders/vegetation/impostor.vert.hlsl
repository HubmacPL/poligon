// Vertex shader for impostors: one card per instance, turned to face the camera, showing
// the four baked frames whose directions lie nearest to the direction of view.
//
// The card is a square of four vertices (-1..1 in x and y) scaled to the model's radius.
// Each pixel of the card is a point in front of the model's middle; for each of the four
// frames that point is projected onto that frame's picture plane, which gives where to read
// it in that frame. Instance matrices are read as in mesh_instanced.vert.hlsl, including
// the fade in their last float.
#include "impostor.hlsli"

[[vk::binding(0, 0)]]
ByteAddressBuffer instance_matrices : register(t0, space0);

cbuffer FrameUniforms : register(b0, space1) {
    float4x4 view_projection;
};

cbuffer ImpostorUniforms : register(b1, space1) {
    float4 center_radius;  // xyz: middle of the model in its own coordinates, w: radius
    float4 camera_frames;  // xyz: camera position, w: frames per side
    float4 batch;          // x: index of this call's first instance matrix
};

struct VertexInput {
    float3 position : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float2 uv : TEXCOORD2;
    uint instance : SV_InstanceID;
};

struct VertexOutput {
    float4 clip_position : SV_Position;
    float4 atlas_uv01 : TEXCOORD0;  // frames 0 and 1: position in the atlas
    float4 atlas_uv23 : TEXCOORD1;  // frames 2 and 3
    float4 frame_uv01 : TEXCOORD2;  // frames 0 and 1: position within the frame
    float4 frame_uv23 : TEXCOORD3;  // frames 2 and 3
    float3 world_position : TEXCOORD4;
    float3 axis_x : TEXCOORD5;      // the instance's rotation, to turn baked normals
    float3 axis_y : TEXCOORD6;
    float3 axis_z : TEXCOORD7;
    float3 blend_fade : TEXCOORD8;  // xy: weights between the frames, z: fade
};

VertexOutput main(VertexInput input) {
    uint address = ((uint)batch.x + input.instance) * 64;
    float3 column0 = asfloat(instance_matrices.Load3(address));
    float3 column1 = asfloat(instance_matrices.Load3(address + 16));
    float3 column2 = asfloat(instance_matrices.Load3(address + 32));
    float4 column3 = asfloat(instance_matrices.Load4(address + 48));

    // Uniform scale and rotation of the instance.
    float scale = length(column0);
    float3 axis_x = column0 / scale;
    float3 axis_y = column1 / scale;
    float3 axis_z = column2 / scale;
    float3 local_center = center_radius.xyz;
    float3 center = column0 * local_center.x + column1 * local_center.y + column2 * local_center.z + column3.xyz;
    float radius = center_radius.w * scale;

    // The card faces the camera; its corners reach the model's radius.
    float3 to_camera = normalize(camera_frames.xyz - center);
    float3 right;
    float3 up;
    FacingAxes(to_camera, right, up);
    float3 world_position = center + (right * input.position.x + up * input.position.y) * radius;

    // Direction of view and the card point in the model's own coordinates.
    float3 local_view = float3(dot(axis_x, to_camera), dot(axis_y, to_camera), dot(axis_z, to_camera));
    float3 offset = world_position - center;
    float3 local_point = float3(dot(axis_x, offset), dot(axis_y, offset), dot(axis_z, offset)) / scale;

    // The four frames around the direction of view.
    float frames = camera_frames.w;
    float2 grid = (HemiOctahedronGrid(local_view) * 0.5 + 0.5) * (frames - 1.0);
    float2 first = clamp(floor(grid), 0.0, frames - 2.0);
    float2 blend = saturate(grid - first);
    float2 atlas_uv[4];
    float2 frame_uv[4];
    [unroll] for (int i = 0; i < 4; ++i) {
        float2 cell = first + float2(i & 1, i >> 1);
        float3 frame_direction = HemiOctahedronDirection(cell / (frames - 1.0) * 2.0 - 1.0);
        float3 frame_right;
        float3 frame_up;
        FacingAxes(frame_direction, frame_right, frame_up);
        // Each frame shows a square of 2 x radius around the middle; v runs down the picture.
        frame_uv[i] = float2(dot(local_point, frame_right), -dot(local_point, frame_up)) / (2.0 * center_radius.w) + 0.5;
        atlas_uv[i] = (cell + frame_uv[i]) / frames;
    }

    VertexOutput output;
    output.clip_position = mul(view_projection, float4(world_position, 1.0));
    output.atlas_uv01 = float4(atlas_uv[0], atlas_uv[1]);
    output.atlas_uv23 = float4(atlas_uv[2], atlas_uv[3]);
    output.frame_uv01 = float4(frame_uv[0], frame_uv[1]);
    output.frame_uv23 = float4(frame_uv[2], frame_uv[3]);
    output.world_position = world_position;
    output.axis_x = axis_x;
    output.axis_y = axis_y;
    output.axis_z = axis_z;
    output.blend_fade = float3(blend, column3.w);
    return output;
}

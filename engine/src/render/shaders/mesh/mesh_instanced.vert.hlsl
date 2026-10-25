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

// Where plants have been walked over (Renderer::SetTrampleMap): 0 untouched, 1 flat.
[[vk::combinedImageSampler]][[vk::binding(0, 0)]]
Texture2D<float> trample_texture : register(t0, space0);
[[vk::combinedImageSampler]][[vk::binding(0, 0)]]
SamplerState trample_sampler : register(s0, space0);

[[vk::binding(1, 0)]]
ByteAddressBuffer instance_matrices : register(t1, space0);

cbuffer FrameUniforms : register(b0, space1) {
    float4x4 view_projection;
    float4 wind;     // see ApplyWind in common.hlsli
    float4 trample;  // xy: world x and z of the trample map's corner, z: 1 / its size in meters, w: 1 = on
    // Motion vectors (depth pass only, motion.x = 1): instances stand still, but the wind
    // moves them, so the previous frame's wind gives where each vertex was.
    float4x4 motion_view_projection;    // this frame's camera without its shift
    float4x4 previous_view_projection;  // the previous frame's, without its shift
    float4 previous_wind;               // the previous frame's wind
    float4 motion;                      // x: 1 = work out the motion
};

// A plant on trampled ground leans over, in a direction of its own, and gets lower.
float3 ApplyTrample(float3 world_position, float3 foot, float bend_response) {
    if (trample.w <= 0.0 || bend_response <= 0.0) {
        return world_position;  // no map, or something that does not bend (a stone, a stump)
    }
    float2 uv = (foot.xz - trample.xy) * trample.z;
    if (any(uv < 0.0) || any(uv > 1.0)) {
        return world_position;
    }
    float pressed = trample_texture.SampleLevel(trample_sampler, uv, 0.0) * 0.85;
    if (pressed <= 0.001) {
        return world_position;
    }
    float height = max(world_position.y - foot.y, 0.0);
    float angle = frac(sin(dot(foot.xz, float2(39.346, 11.135))) * 24634.6345) * 6.2832;
    float3 moved = world_position;
    moved.y = foot.y + height * (1.0 - pressed);
    moved.xz += float2(cos(angle), sin(angle)) * height * pressed * 0.8;
    return moved;
}

cbuffer ObjectUniforms : register(b1, space1) {
    float4x4 model;   // unused: every instance has its own
    float4 object_color;
    float4 uv_scale;  // xy: multiplier for texture coordinates, z: index of this call's first instance matrix
    float4 wind_response;  // x: bend, y: flutter (see ApplyWind)
    float4x4 previous_model;  // unused
    float4x4 normal_matrix;   // unused: the cofactor matrix is worked out per instance
    float4 noise_shift;       // xyz: added to camera-relative positions for world-fixed wind gusts
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
    float4 motion_clip : TEXCOORD5;    // without the camera's shift (motion vectors, depth pass only)
    float4 previous_clip : TEXCOORD6;  // where the vertex was in the previous frame
};

VertexOutput main(VertexInput input) {
    uint address = ((uint)uv_scale.z + input.instance) * 64;
    float3 column0 = asfloat(instance_matrices.Load3(address));
    float3 column1 = asfloat(instance_matrices.Load3(address + 16));
    float3 column2 = asfloat(instance_matrices.Load3(address + 32));
    float4 column3 = asfloat(instance_matrices.Load4(address + 48));

    VertexOutput output;
    float3 still_position = column0 * input.position.x + column1 * input.position.y + column2 * input.position.z + column3.xyz;
    still_position = ApplyTrample(still_position, column3.xyz, wind_response.x);
    float3 world_position = ApplyWind(still_position, column3.xyz, input.position, wind, wind_response.xy, noise_shift.xyz);
    output.clip_position = mul(view_projection, float4(world_position, 1.0));
    // The cofactor matrix (columns: cross products of the other two columns) is the inverse
    // transpose times the determinant, so after normalizing it gives normals square to the
    // surface under any scale, without storing a second matrix per instance.
    output.world_normal = cross(column1, column2) * input.normal.x + cross(column2, column0) * input.normal.y +
                          cross(column0, column1) * input.normal.z;
    output.color = object_color;
    output.uv = input.uv * uv_scale.xy;
    output.world_position = world_position;
    output.fade = column3.w;
    output.motion_clip = 0.0;
    output.previous_clip = 0.0;
    if (motion.x > 0.0) {
        float3 previous_position = ApplyWind(still_position, column3.xyz, input.position, previous_wind, wind_response.xy, noise_shift.xyz);
        output.motion_clip = mul(motion_view_projection, float4(world_position, 1.0));
        output.previous_clip = mul(previous_view_projection, float4(previous_position, 1.0));
    }
    return output;
}

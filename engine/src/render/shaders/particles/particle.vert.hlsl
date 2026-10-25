// Particles: one card per particle (Renderer::DrawParticles), facing the camera, read from a
// buffer of 64 bytes per particle (ParticleCard in particle_state.h): position and size,
// velocity and rotation, linear color, the atlas picture. Six vertices per card are made
// here, without a vertex buffer.
#include "common.hlsli"

[[vk::binding(0, 0)]]
ByteAddressBuffer cards : register(t0, space0);

cbuffer ParticleVertexUniforms : register(b0, space1) {
    float4x4 view_projection;
    float4 camera;  // xyz: camera position, w: index of this call's first card
    float4 atlas;   // xy: columns and rows of the texture's atlas, z: stretch along the velocity (seconds)
};

struct VertexOutput {
    float4 clip_position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : TEXCOORD1;
    float3 world_position : TEXCOORD2;
    float view_depth : TEXCOORD3;  // distance along the view, for the soft edge against the scene
};

VertexOutput main(uint vertex_index : SV_VertexID, uint instance : SV_InstanceID) {
    static const float2 kCorners[6] = {float2(0.0, 0.0), float2(1.0, 0.0), float2(1.0, 1.0),
                                       float2(0.0, 0.0), float2(1.0, 1.0), float2(0.0, 1.0)};
    uint address = ((uint)camera.w + instance) * 64;
    float4 position_size = asfloat(cards.Load4(address));
    float4 velocity_rotation = asfloat(cards.Load4(address + 16));
    float4 color = asfloat(cards.Load4(address + 32));
    float frame = asfloat(cards.Load(address + 48));

    float3 center = position_size.xyz;
    float3 to_camera = normalize(camera.xyz - center);
    float3 right;
    float3 up;
    FacingAxes(to_camera, right, up);
    float half_width = position_size.w * 0.5;
    float half_height = half_width;
    float3 velocity = velocity_rotation.xyz;
    float3 across_view = velocity - to_camera * dot(velocity, to_camera);
    if (atlas.z > 0.0 && dot(across_view, across_view) > 1e-6) {
        // Stretched along the velocity as seen on screen (rain streaks).
        up = normalize(across_view);
        right = normalize(cross(up, to_camera));
        half_height += length(velocity) * atlas.z * 0.5;
    } else {
        float s = sin(velocity_rotation.w);
        float c = cos(velocity_rotation.w);
        float3 turned_right = right * c + up * s;
        up = up * c - right * s;
        right = turned_right;
    }
    float2 corner = kCorners[vertex_index];
    float3 world = center + right * ((corner.x * 2.0 - 1.0) * half_width) + up * ((1.0 - corner.y * 2.0) * half_height);

    float2 grid = max(atlas.xy, float2(1.0, 1.0));
    float picture = floor(frame);
    float2 cell = float2(fmod(picture, grid.x), floor(picture / grid.x));
    VertexOutput output;
    output.clip_position = mul(view_projection, float4(world, 1.0));
    output.uv = (cell + corner) / grid;
    output.color = color;
    output.world_position = world;
    output.view_depth = output.clip_position.w;
    return output;
}

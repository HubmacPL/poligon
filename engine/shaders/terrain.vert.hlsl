// Vertex shader for terrain.
//
// The terrain is not one big mesh. One flat grid mesh (a square of 64 x 64 cells) is drawn
// many times, each copy stretched over a square piece of land ("patch"): small patches near
// the camera, large ones far away. This shader lifts every vertex to the height of the land
// at its place, read from an image that holds the whole height map.
//
// The grid mesh has a rim of extra vertices marked with position.y = -1. They are pushed
// down to form a "skirt" that hides the gaps where a small patch meets a large one.
//
// SDL GPU rule for vertex shaders: sampled textures come first in space0, storage buffers
// after them. Same outputs as mesh.vert.hlsl.

[[vk::combinedImageSampler]][[vk::binding(0, 0)]]
Texture2D<float> height_texture : register(t0, space0);
[[vk::combinedImageSampler]][[vk::binding(0, 0)]]
SamplerState height_sampler : register(s0, space0);

// One matrix per patch, read as plain bytes (see mesh_skinned.vert.hlsl for why).
// Column 0 holds the patch's size, column 3 its corner.
[[vk::binding(1, 0)]]
ByteAddressBuffer instance_matrices : register(t1, space0);

cbuffer FrameUniforms : register(b0, space1) {
    float4x4 view_projection;
};

cbuffer ObjectUniforms : register(b1, space1) {
    float4x4 model;   // unused
    float4 object_color;
    float4 uv_scale;  // z: index of this call's first patch matrix
};

cbuffer TerrainUniforms : register(b2, space1) {
    float4 map;          // xy: world x and z of the height map's first point, z: meters between points
    float4 map_heights;  // x: world height of image value 0, y: height difference of image value 1, zw: index of the last point along x and z
};

struct VertexInput {
    float3 position : TEXCOORD0;  // xz: 0..1 across the patch, y: -1 for skirt vertices
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
};

VertexOutput main(VertexInput input) {
    uint address = ((uint)uv_scale.z + input.instance) * 64;
    float patch_size = asfloat(instance_matrices.Load(address));
    float3 patch_corner = asfloat(instance_matrices.Load3(address + 48));

    // Where the vertex is on the map, in points; never beyond the map's edge.
    float2 point_position = (patch_corner.xz + input.position.xz * patch_size - map.xy) / map.z;
    point_position = clamp(point_position, float2(0.0, 0.0), map_heights.zw);
    float height = height_texture.Load(int3(int2(point_position + 0.5), 0)) * map_heights.y + map_heights.x;
    if (input.position.y < -0.5) {
        height -= patch_size * 0.06 + 0.5;
    }

    VertexOutput output;
    float3 world_position = float3(map.x + point_position.x * map.z, height, map.y + point_position.y * map.z);
    output.clip_position = mul(view_projection, float4(world_position, 1.0));
    // The fragment shader works out the real direction of the ground from the height image.
    output.world_normal = float3(0.0, 1.0, 0.0);
    output.color = object_color;
    output.uv = point_position;
    output.world_position = world_position;
    return output;
}

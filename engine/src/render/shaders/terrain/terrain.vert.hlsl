// Vertex shader for terrain.
//
// The terrain is not one big mesh. One flat grid mesh (a square of 64 x 64 cells) is drawn
// many times, each copy stretched over a square piece of land ("patch"): small patches near
// the camera, large ones far away. This shader lifts every vertex to the height of the land
// at its place, read from an image that holds the whole height map, or for a streamed
// terrain one page of an array of them (one page per patch of the world, see
// Renderer::CreateTerrainPages).
//
// The grid mesh has a rim of extra vertices marked with position.y = -1. They are pushed
// down to form a "skirt" that hides the gaps where a small patch meets a large one.
//
// SDL GPU rule for vertex shaders: sampled textures come first in space0, storage buffers
// after them. Same outputs as mesh.vert.hlsl.

[[vk::combinedImageSampler]][[vk::binding(0, 0)]]
Texture2DArray<float> height_texture : register(t0, space0);
[[vk::combinedImageSampler]][[vk::binding(0, 0)]]
SamplerState height_sampler : register(s0, space0);

// One matrix per patch, read as plain bytes (see mesh_skinned.vert.hlsl for why).
// Column 0 holds the patch's size (x) and the index of the page's last point that holds
// land along x and z (yz), column 3 its corner; column 1: x = its page, z = the
// page's height of image value 0, w = the page's height of image value 1 above that;
// column 2: xy = world x and z of the page's first point. All relative to the camera.
[[vk::binding(1, 0)]]
ByteAddressBuffer instance_matrices : register(t1, space0);

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
    float4x4 model;   // unused
    float4 object_color;
    float4 uv_scale;  // z: index of this call's first patch matrix
    float4 wind_response;  // unused here
};

cbuffer TerrainUniforms : register(b2, space1) {
    float4 map;          // z: meters between points, w: 1 = skirts hang down, 0 = skirts stay on the surface (shadow pass)
    float4 map_heights;  // zw: index of the last point along x and z (of every page)
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
    float fade : TEXCOORD4;  // see KeptByFade in common.hlsli
    float4 motion_clip : TEXCOORD5;    // without the camera's shift (motion vectors, depth pass only)
    float4 previous_clip : TEXCOORD6;  // where the vertex was in the previous frame
    nointerpolation float2 page : TEXCOORD7;  // x: the page, y: its height of image value 1
};

VertexOutput main(VertexInput input) {
    uint address = ((uint)uv_scale.z + input.instance) * 64;
    float3 size_and_last = asfloat(instance_matrices.Load3(address));
    float patch_size = size_and_last.x;
    float4 page_heights = asfloat(instance_matrices.Load4(address + 16));
    float2 page_corner = asfloat(instance_matrices.Load2(address + 32));
    float3 patch_corner = asfloat(instance_matrices.Load3(address + 48));
    int page = (int)(page_heights.x + 0.5);

    // Where the vertex is on its page, in points; never beyond the page's edge.
    float2 point_position = (patch_corner.xz + input.position.xz * patch_size - page_corner) / map.z;
    point_position = clamp(point_position, float2(0.0, 0.0), min(map_heights.zw, size_and_last.yz));
    float height = height_texture.Load(int4(int2(point_position + 0.5), page, 0)) * page_heights.w + page_heights.z;
    if (input.position.y < -0.5) {
        // Skirts hang down only in the picture. Seen from the sun they are walls standing
        // on every patch edge, which would shadow the slope below them in a grid pattern.
        height -= (patch_size * 0.06 + 0.5) * map.w;
    }

    VertexOutput output;
    float3 world_position = float3(page_corner.x + point_position.x * map.z, height, page_corner.y + point_position.y * map.z);
    output.clip_position = mul(view_projection, float4(world_position, 1.0));
    // The fragment shader works out the real direction of the ground from the height image.
    output.world_normal = float3(0.0, 1.0, 0.0);
    output.color = object_color;
    output.uv = point_position;
    output.world_position = world_position;
    output.fade = 1.0;
    output.page = float2((float)page, page_heights.w);
    // The ground stands still: only the cameras differ.
    output.motion_clip = 0.0;
    output.previous_clip = 0.0;
    if (motion.x > 0.0) {
        output.motion_clip = mul(motion_view_projection, float4(world_position, 1.0));
        output.previous_clip = mul(previous_view_projection, float4(world_position, 1.0));
    }
    return output;
}

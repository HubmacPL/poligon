// Decals: each is a box (Renderer::DrawDecal), drawn from the inside (back faces only, no
// depth test), so that every pixel of the scene that might lie inside it is visited once,
// also with the camera inside the box. The fragment shader finds the scene's point there
// from the depth image. Boxes come from a buffer, 128 bytes each (the box's transform and its
// inverse, DecalInstance in decal_state.h); 36 vertices per box are made here.

[[vk::binding(0, 0)]]
ByteAddressBuffer decals : register(t0, space0);

cbuffer DecalVertexUniforms : register(b0, space1) {
    float4x4 view_projection;
    float4 first;  // x: index of this call's first decal
};

struct VertexOutput {
    float4 clip_position : SV_Position;
    // The inverse of the box's transform, row by row, and its axes in the world.
    nointerpolation float4 inverse0 : TEXCOORD0;
    nointerpolation float4 inverse1 : TEXCOORD1;
    nointerpolation float4 inverse2 : TEXCOORD2;
    nointerpolation float3 axis_x : TEXCOORD3;
    nointerpolation float3 axis_up : TEXCOORD4;
    nointerpolation float3 axis_z : TEXCOORD5;
};

float4x4 LoadMatrix(uint address) {
    float4 c0 = asfloat(decals.Load4(address));
    float4 c1 = asfloat(decals.Load4(address + 16));
    float4 c2 = asfloat(decals.Load4(address + 32));
    float4 c3 = asfloat(decals.Load4(address + 48));
    return transpose(float4x4(c0, c1, c2, c3));  // stored column by column
}

VertexOutput main(uint vertex_index : SV_VertexID, uint instance : SV_InstanceID) {
    // Face f has the outward normal kNormals[f] and two edges u, v with cross(u, v) = normal,
    // so that its triangles turn counter-clockwise seen from outside.
    static const float3 kNormals[6] = {float3(1, 0, 0), float3(-1, 0, 0), float3(0, 1, 0), float3(0, -1, 0), float3(0, 0, 1), float3(0, 0, -1)};
    static const float3 kU[6] = {float3(0, 1, 0), float3(0, 0, 1), float3(0, 0, 1), float3(1, 0, 0), float3(1, 0, 0), float3(0, 1, 0)};
    static const float3 kV[6] = {float3(0, 0, 1), float3(0, 1, 0), float3(1, 0, 0), float3(0, 0, 1), float3(0, 1, 0), float3(1, 0, 0)};
    static const float2 kCorners[6] = {float2(0, 0), float2(1, 0), float2(1, 1), float2(0, 0), float2(1, 1), float2(0, 1)};
    uint face = vertex_index / 6;
    float2 corner = kCorners[vertex_index % 6];
    float3 local = 0.5 * kNormals[face] + (corner.x - 0.5) * kU[face] + (corner.y - 0.5) * kV[face];

    uint address = ((uint)first.x + instance) * 128;
    float4x4 transform = LoadMatrix(address);
    float4x4 inverse = LoadMatrix(address + 64);
    VertexOutput output;
    output.clip_position = mul(view_projection, mul(transform, float4(local, 1.0)));
    output.inverse0 = inverse[0];
    output.inverse1 = inverse[1];
    output.inverse2 = inverse[2];
    output.axis_x = normalize(transform._m00_m10_m20);
    output.axis_up = normalize(transform._m01_m11_m21);
    output.axis_z = normalize(transform._m02_m12_m22);
    return output;
}

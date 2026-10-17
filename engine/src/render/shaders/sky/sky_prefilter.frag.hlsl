// Draws one face of one level of the sky light map: the captured sky (sky_capture.frag.hlsl)
// blurred as a surface of the level's roughness reflects it. Each level is rougher: level 0
// is a mirror. The last level (roughness given as -1) is the light a matte surface facing
// each direction receives from the whole sky before it (cosine-weighted), for ambient light.
// The blur averages many directions around the face's direction, chosen as the GGX highlight shape spreads them (importance
// sampling), each read from a level of the captured sky that matches the area it stands
// for, so that few samples give a smooth result (Karis, "Real Shading in Unreal Engine 4",
// 2013; Colbert and Křivánek, "GPU-Based Importance Sampling", GPU Gems 3, 2007).
#include "common.hlsli"

[[vk::combinedImageSampler]][[vk::binding(0, 2)]]
TextureCube<float4> captured_texture : register(t0, space2);
[[vk::combinedImageSampler]][[vk::binding(0, 2)]]
SamplerState captured_sampler : register(s0, space2);

cbuffer SkyPrefilterUniforms : register(b0, space3) {
    float4 params;  // x: face 0..5, y: roughness, z: size of the captured sky's faces in pixels, w: samples
};

struct FragmentInput {
    float4 clip_position : SV_Position;
    float2 uv : TEXCOORD0;
    float2 ndc : TEXCOORD1;
};

static const float kPi = 3.14159265;

float2 Hammersley(uint i, uint count) {
    uint bits = reversebits(i);
    return float2(float(i) / float(count), float(bits) * 2.3283064365386963e-10);
}

float4 main(FragmentInput input) : SV_Target0 {
    float3 normal = CubeFaceDirection((int)params.x, input.uv);
    float roughness = params.y;
    if (roughness == 0.0) {
        return float4(captured_texture.SampleLevel(captured_sampler, normal, 0.0).rgb, 1.0);
    }
    // A frame around the normal.
    float3 up = abs(normal.y) < 0.999 ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);
    float3 tangent = normalize(cross(up, normal));
    float3 bitangent = cross(normal, tangent);
    float texel_angle = 4.0 * kPi / (6.0 * params.z * params.z);
    if (roughness < 0.0) {
        // Directions spread as the cosine weights them, so a plain average is the light received.
        uint diffuse_count = (uint)params.w;
        float3 diffuse = float3(0.0, 0.0, 0.0);
        for (uint i = 0; i < diffuse_count; ++i) {
            float2 xi = Hammersley(i, diffuse_count);
            float phi = 2.0 * kPi * xi.x;
            float cos_theta = sqrt(1.0 - xi.y);
            float sin_theta = sqrt(xi.y);
            float3 light = tangent * (sin_theta * cos(phi)) + bitangent * (sin_theta * sin(phi)) + normal * cos_theta;
            float pdf = cos_theta / kPi;
            float level = max(0.5 * log2(1.0 / (float(diffuse_count) * pdf + 0.0001) / texel_angle) + 1.0, 0.0);
            diffuse += captured_texture.SampleLevel(captured_sampler, light, level).rgb;
        }
        return float4(diffuse / float(diffuse_count), 1.0);
    }

    float a = roughness * roughness;
    float a2 = a * a;
    uint count = (uint)params.w;
    float3 sum = float3(0.0, 0.0, 0.0);
    float weight = 0.0;
    for (uint i = 0; i < count; ++i) {
        float2 xi = Hammersley(i, count);
        float phi = 2.0 * kPi * xi.x;
        float cos_theta = sqrt((1.0 - xi.y) / (1.0 + (a2 - 1.0) * xi.y));
        float sin_theta = sqrt(1.0 - cos_theta * cos_theta);
        float3 half_vector = tangent * (sin_theta * cos(phi)) + bitangent * (sin_theta * sin(phi)) + normal * cos_theta;
        // The view is taken along the normal, as is usual for such maps.
        float3 light = 2.0 * dot(normal, half_vector) * half_vector - normal;
        float n_dot_l = dot(normal, light);
        if (n_dot_l <= 0.0) {
            continue;
        }
        // The share of all directions this sample stands for, against one captured texel.
        float denominator = cos_theta * cos_theta * (a2 - 1.0) + 1.0;
        float distribution = a2 / (kPi * denominator * denominator);
        float pdf = distribution * 0.25;
        float sample_angle = 1.0 / (float(count) * pdf + 0.0001);
        float level = max(0.5 * log2(sample_angle / texel_angle) + 1.0, 0.0);
        sum += captured_texture.SampleLevel(captured_sampler, light, level).rgb * n_dot_l;
        weight += n_dot_l;
    }
    return float4(sum / max(weight, 0.0001), 1.0);
}

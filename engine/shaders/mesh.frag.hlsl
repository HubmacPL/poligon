// Fragment shader for lit meshes.
//
// Material: the metallic-roughness model used by glTF (base color, metallic, roughness,
// normal map, ambient occlusion). The lighting itself is in lighting.hlsli.
//
// Register spaces follow SDL GPU rules: fragment shader textures and samplers are in space2,
// uniform buffers in space3. For Vulkan a texture and its sampler must be declared as one
// combined binding; Direct3D 12 ignores the vk:: attributes.
#define LIGHTING_SHADOW_BINDING 3
#define LIGHTING_SHADOW_T t3
#define LIGHTING_SHADOW_S s3
#define LIGHTING_AO_BINDING 4
#define LIGHTING_AO_T t4
#define LIGHTING_AO_S s4
#include "lighting.hlsli"

[[vk::combinedImageSampler]][[vk::binding(0, 2)]]
Texture2D<float4> base_texture : register(t0, space2);
[[vk::combinedImageSampler]][[vk::binding(0, 2)]]
SamplerState base_sampler : register(s0, space2);

[[vk::combinedImageSampler]][[vk::binding(1, 2)]]
Texture2D<float4> normal_texture : register(t1, space2);
[[vk::combinedImageSampler]][[vk::binding(1, 2)]]
SamplerState normal_sampler : register(s1, space2);

// Red: ambient occlusion, green: roughness, blue: metallic (the glTF layout).
[[vk::combinedImageSampler]][[vk::binding(2, 2)]]
Texture2D<float4> orm_texture : register(t2, space2);
[[vk::combinedImageSampler]][[vk::binding(2, 2)]]
SamplerState orm_sampler : register(s2, space2);

cbuffer MaterialUniforms : register(b1, space3) {
    float4 material_factors;  // x: metallic, y: roughness, z: alpha cutoff, w: unused
    float4 material_flags;    // x: has normal map, y: has ORM texture, z: ORM red is occlusion, w: thin foliage
};

struct FragmentInput {
    float4 clip_position : SV_Position;
    float3 world_normal : TEXCOORD0;
    float4 color : TEXCOORD1;
    float2 uv : TEXCOORD2;
    float3 world_position : TEXCOORD3;
    bool is_front_face : SV_IsFrontFace;
};

// Normal from the normal map, turned into world space. The tangent directions are derived
// from how position and texture coordinates change across the pixel, so meshes need no
// stored tangents.
float3 ApplyNormalMap(float3 normal, float3 world_position, float2 uv) {
    float3 map = normal_texture.Sample(normal_sampler, uv).xyz * 2.0 - 1.0;
    // Normal maps follow the OpenGL convention: red tilts the surface along u, green towards
    // the top of the image. The frame built below comes out mirrored in our screen space
    // (y runs down the screen), and v runs down the image; together that means the red
    // direction must be negated and the green one kept. tests/scene_render_test.cpp checks both.
    map.x = -map.x;

    float3 dp1 = ddx(world_position);
    float3 dp2 = ddy(world_position);
    float2 duv1 = ddx(uv);
    float2 duv2 = ddy(uv);
    float3 dp2_perp = cross(dp2, normal);
    float3 dp1_perp = cross(normal, dp1);
    float3 tangent = dp2_perp * duv1.x + dp1_perp * duv2.x;
    float3 bitangent = dp2_perp * duv1.y + dp1_perp * duv2.y;
    float scale = max(dot(tangent, tangent), dot(bitangent, bitangent));
    if (scale <= 0.0) {
        return normal;
    }
    float inverse_length = rsqrt(scale);
    return normalize(tangent * (map.x * inverse_length) + bitangent * (map.y * inverse_length) + normal * map.z);
}

float4 main(FragmentInput input) : SV_Target0 {
    // Color textures are stored sRGB-encoded; the texture format makes this sample linear.
    float4 texel = base_texture.Sample(base_sampler, input.uv);
    float alpha = texel.a * input.color.a;

    // Alpha cutout: pixels below the cutoff are not drawn (leaves, fences). With multisampling
    // the edge is smoothed by covering only some of the pixel's samples ("alpha to coverage").
    float alpha_cutoff = material_factors.z;
    float coverage = 1.0;
    if (alpha_cutoff > 0.0) {
        if (options.y > 0.5) {
            coverage = saturate((alpha - alpha_cutoff) / max(fwidth(alpha), 0.0001) + 0.5);
            if (coverage <= 0.0) {
                discard;
            }
        } else if (alpha < alpha_cutoff) {
            discard;
        }
    }

    Surface surface;
    surface.base_color = texel.rgb * input.color.rgb;
    surface.metallic = material_factors.x;
    surface.roughness = material_factors.y;
    surface.occlusion = 1.0;
    if (material_flags.y > 0.5) {
        float3 orm = orm_texture.Sample(orm_sampler, input.uv).rgb;
        surface.roughness *= orm.g;
        surface.metallic *= orm.b;
        if (material_flags.z > 0.5) {
            surface.occlusion = orm.r;
        }
    }

    // The back side of a double-sided surface is lit as if its normal pointed the other way.
    surface.surface_normal = normalize(input.world_normal) * (input.is_front_face ? 1.0 : -1.0);
    surface.normal = surface.surface_normal;
    if (material_flags.x > 0.5) {
        surface.normal = ApplyNormalMap(surface.surface_normal, input.world_position, input.uv);
    }
    surface.world_position = input.world_position;
    surface.pixel = input.clip_position.xy;
    surface.thin_foliage = material_flags.w > 0.5;

    // The render target is linear and wide-range; tone mapping happens in a later pass.
    return float4(ShadeSurface(surface), coverage);
}

// Fragment shader for lit meshes.
//
// Material: the metallic-roughness model used by glTF (base color, metallic, roughness,
// normal map, ambient occlusion, emission). The lighting itself is in lighting.hlsli.
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
#define LIGHTING_CLOUD_BINDING 5
#define LIGHTING_CLOUD_T t5
#define LIGHTING_CLOUD_S s5
#define LIGHTING_SKY_BINDING 6
#define LIGHTING_SKY_T t6
#define LIGHTING_SKY_S s6
#define LIGHTING_RADIANCE_BINDING 7
#define LIGHTING_RADIANCE_T t7
#define LIGHTING_RADIANCE_S s7
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

// Emitted light, multiplied by Material::emissive (a color texture).
[[vk::combinedImageSampler]][[vk::binding(8, 2)]]
Texture2D<float4> emissive_texture : register(t8, space2);
[[vk::combinedImageSampler]][[vk::binding(8, 2)]]
SamplerState emissive_sampler : register(s8, space2);

cbuffer MaterialUniforms : register(b1, space3) {
    float4 material_factors;  // x: metallic, y: roughness, z: alpha cutoff, w: the frame's noise offset
    float4 material_flags;    // x: has normal map, y: has ORM texture, z: ORM red is occlusion, w: thin foliage (1; 2: back faces keep the normal)
    float4 material_shading;  // x: how far the normal turns towards straight up
    float4 material_emissive; // rgb: emitted light, w: 1 = multiplied by the emissive texture
};

struct FragmentInput {
    float4 clip_position : SV_Position;
    float3 world_normal : TEXCOORD0;
    float4 color : TEXCOORD1;
    float2 uv : TEXCOORD2;
    float3 world_position : TEXCOORD3;
    float fade : TEXCOORD4;  // see KeptByFade in common.hlsli
    bool is_front_face : SV_IsFrontFace;
};

#ifdef EARLY_DEPTH_TEST
// The depth pass has drawn the final depth; test it before running the shader. Discards
// below then only drop color (depth is not written in this pass).
[earlydepthstencil]
#endif
float4 main(FragmentInput input) : SV_Target0 {
    if (!KeptByFade(input.fade, input.clip_position.xy + float2(material_factors.w, 0.0))) {
        discard;
    }
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

    // The back side of a double-sided surface is lit as if its normal pointed the other way,
    // unless the material keeps it (grass blades with normals pointing up).
    bool flip_normal = !input.is_front_face && material_flags.w < 1.5;
    surface.surface_normal = normalize(input.world_normal) * (flip_normal ? -1.0 : 1.0);
    if (material_shading.x > 0.0) {
        // Grass and low plants: lit like the ground they stand on.
        surface.surface_normal = normalize(lerp(surface.surface_normal, float3(0.0, 1.0, 0.0), material_shading.x) + float3(0.0, 1e-4, 0.0));
    }
    surface.normal = surface.surface_normal;
    if (material_flags.x > 0.5) {
        // x and y from the map, z rebuilt (two-channel compressed maps hold no z).
        float2 map_xy = normal_texture.Sample(normal_sampler, input.uv).xy * 2.0 - 1.0;
        float3 map = float3(map_xy, sqrt(saturate(1.0 - dot(map_xy, map_xy))));
        surface.normal = ApplyNormalMapSample(map, surface.surface_normal, input.world_position, input.uv);
    }
    surface.world_position = input.world_position;
    surface.pixel = input.clip_position.xy;
    surface.thin_foliage = material_flags.w > 0.5;
    surface.emission = material_emissive.rgb;
    if (material_emissive.w > 0.5) {
        surface.emission *= emissive_texture.Sample(emissive_sampler, input.uv).rgb;
    }

    // The render target is linear and wide-range; tone mapping happens in a later pass.
    return float4(ShadeSurface(surface), coverage);
}

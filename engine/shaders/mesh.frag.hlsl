// Fragment shader for lit meshes.
//
// Material: the metallic-roughness model used by glTF (base color, metallic, roughness,
// normal map, ambient occlusion). Lighting: one directional light (the sun) with shadows,
// ambient light from the sky above and the ground below, and fog.
//
// Register spaces follow SDL GPU rules: fragment shader textures and samplers are in space2,
// uniform buffers in space3. For Vulkan a texture and its sampler must be declared as one
// combined binding; Direct3D 12 ignores the vk:: attributes.
#include "common.hlsli"

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

// All shadow cascades side by side in one depth image (2 x 2 tiles).
[[vk::combinedImageSampler]][[vk::binding(3, 2)]]
Texture2D<float> shadow_texture : register(t3, space2);
[[vk::combinedImageSampler]][[vk::binding(3, 2)]]
SamplerComparisonState shadow_sampler : register(s3, space2);

cbuffer FrameUniforms : register(b0, space3) {
    float4x4 shadow_matrices[4];  // world position -> shadow image coordinates and depth, per cascade
    float4 cascade_far;           // distance from the camera where each cascade ends
    float4 camera_position;
    float4 camera_forward;
    float4 light_direction;       // xyz: direction the light travels, normalized
    float4 light_color;
    float4 ambient_sky;           // light arriving from above
    float4 ambient_ground;        // light arriving from below
    float4 sky_zenith;
    float4 sky_horizon;
    float4 sky_ground;
    float4 fog;                   // rgb: color, a: density per meter
    float4 options;               // x: shadows on, y: alpha to coverage on, z: sky reflections on, w: shadow texel size
};

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

static const float kPi = 3.14159265;

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

// Part of the sun's light that reaches this point: 1 = fully lit, 0 = in shadow.
float SunVisibility(float3 world_position, float3 normal) {
    if (options.x < 0.5) {
        return 1.0;
    }
    float view_depth = dot(world_position - camera_position.xyz, camera_forward.xyz);
    int cascade = 4;
    for (int i = 3; i >= 0; --i) {
        if (view_depth < cascade_far[i]) {
            cascade = i;
        }
    }
    if (cascade == 4) {
        return 1.0;  // beyond the shadow distance
    }

    // Move the lookup point a little off the surface, more in the coarser cascades,
    // so that a surface does not shadow itself.
    float3 offset_position = world_position + normal * (0.03 * (cascade + 1.0));
    float4 shadow_position = mul(shadow_matrices[cascade], float4(offset_position, 1.0));
    float texel = options.w;

    // Average 3 x 3 comparisons for a soft edge.
    float lit = 0.0;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            lit += shadow_texture.SampleCmpLevelZero(shadow_sampler, shadow_position.xy + float2(x, y) * texel,
                                                     shadow_position.z - 0.0008);
        }
    }
    return lit / 9.0;
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

    float3 base_color = texel.rgb * input.color.rgb;
    float metallic = material_factors.x;
    float roughness = material_factors.y;
    float occlusion = 1.0;
    if (material_flags.y > 0.5) {
        float3 orm = orm_texture.Sample(orm_sampler, input.uv).rgb;
        roughness *= orm.g;
        metallic *= orm.b;
        if (material_flags.z > 0.5) {
            occlusion = orm.r;
        }
    }
    roughness = clamp(roughness, 0.045, 1.0);

    // The back side of a double-sided surface is lit as if its normal pointed the other way.
    float3 normal = normalize(input.world_normal) * (input.is_front_face ? 1.0 : -1.0);
    float3 surface_normal = normal;
    if (material_flags.x > 0.5) {
        normal = ApplyNormalMap(normal, input.world_position, input.uv);
    }

    float3 to_camera = normalize(camera_position.xyz - input.world_position);
    float3 to_light = -light_direction.xyz;
    float n_dot_l = saturate(dot(normal, to_light));
    float n_dot_v = max(dot(normal, to_camera), 0.0001);

    float3 diffuse_color = base_color * (1.0 - metallic);
    // Reflectance when looking straight at the surface: 4% for non-metals, the base color for metals.
    float3 specular_color = lerp(float3(0.04, 0.04, 0.04), base_color, metallic);

    float3 color = float3(0.0, 0.0, 0.0);

    // --- Sun ---
    if (dot(light_color.rgb, light_color.rgb) > 0.0) {
        float visibility = SunVisibility(input.world_position, surface_normal);
        float3 sun = light_color.rgb * visibility;

        // Specular highlight (GGX microfacet model).
        float3 half_vector = normalize(to_light + to_camera);
        float n_dot_h = saturate(dot(normal, half_vector));
        float v_dot_h = saturate(dot(to_camera, half_vector));
        float a = roughness * roughness;
        float a2 = a * a;
        float denominator = n_dot_h * n_dot_h * (a2 - 1.0) + 1.0;
        float distribution = a2 / (kPi * denominator * denominator);
        float k = (roughness + 1.0) * (roughness + 1.0) / 8.0;
        float geometry = (n_dot_l / (n_dot_l * (1.0 - k) + k)) * (n_dot_v / (n_dot_v * (1.0 - k) + k));
        float3 fresnel = specular_color + (1.0 - specular_color) * pow(1.0 - v_dot_h, 5.0);
        float3 specular = distribution * geometry * fresnel / max(4.0 * n_dot_l * n_dot_v, 0.0001);

        // Light units are chosen so that a white, rough surface facing the sun reflects exactly
        // the sun's color; the highlight is scaled by pi to match.
        color += (diffuse_color * (1.0 - fresnel) + specular * kPi) * sun * n_dot_l;

        // Thin foliage lets some light through: a leaf lit from behind glows on the near side.
        if (material_flags.w > 0.5) {
            float back_light = saturate(dot(-normal, to_light));
            color += diffuse_color * sun * back_light * 0.35;
        }
    }

    // --- Ambient light: from the sky above and the ground below ---
    float sky_amount = normal.y * 0.5 + 0.5;
    float3 ambient = lerp(ambient_ground.rgb, ambient_sky.rgb, sky_amount);
    color += diffuse_color * ambient * occlusion;

    // Smooth surfaces also mirror the sky faintly.
    if (options.z > 0.5) {
        float3 reflected = reflect(-to_camera, normal);
        float3 sky = SkyColor(reflected, sky_zenith.rgb, sky_horizon.rgb, sky_ground.rgb);
        float3 fresnel = specular_color + (max(1.0 - roughness, specular_color) - specular_color) * pow(1.0 - n_dot_v, 5.0);
        color += sky * fresnel * (1.0 - roughness) * occlusion;
    }

    // --- Fog: distant things fade into the fog color ---
    if (fog.a > 0.0) {
        float distance_to_camera = length(camera_position.xyz - input.world_position);
        float fog_amount = 1.0 - exp(-distance_to_camera * fog.a);
        color = lerp(color, fog.rgb, fog_amount);
    }

    // The render target is linear and wide-range; tone mapping happens in a later pass.
    return float4(color, coverage);
}

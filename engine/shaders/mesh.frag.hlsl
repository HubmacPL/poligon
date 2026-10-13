// Fragment shader for lit meshes: base texture times color, one directional light plus ambient.
// Register spaces follow SDL GPU rules: fragment shader textures, samplers (space2) and
// uniform buffers (space3).

// For Vulkan the texture and its sampler must be declared as one combined binding;
// Direct3D 12 ignores the vk:: attributes.
[[vk::combinedImageSampler]][[vk::binding(0, 2)]]
Texture2D<float4> base_texture : register(t0, space2);
[[vk::combinedImageSampler]][[vk::binding(0, 2)]]
SamplerState base_sampler : register(s0, space2);

cbuffer LightUniforms : register(b0, space3) {
    float4 light_direction;  // xyz: direction the light travels, normalized
    float4 light_color;
    float4 ambient_color;
};

struct FragmentInput {
    float4 clip_position : SV_Position;
    float3 world_normal : TEXCOORD0;
    float4 color : TEXCOORD1;
    float2 uv : TEXCOORD2;
    float alpha_cutoff : TEXCOORD3;
    bool is_front_face : SV_IsFrontFace;
};

float4 main(FragmentInput input) : SV_Target0 {
    // Color textures are stored sRGB-encoded; the texture format makes this sample linear.
    float4 texel = base_texture.Sample(base_sampler, input.uv);

    // Alpha cutout: pixels below the cutoff are not drawn at all (leaves, fences).
    if (texel.a * input.color.a < input.alpha_cutoff) {
        discard;
    }

    float3 base_color = texel.rgb * input.color.rgb;
    // The back side of a double-sided surface is lit as if its normal pointed the other way.
    float3 normal = normalize(input.world_normal) * (input.is_front_face ? 1.0 : -1.0);
    float diffuse = saturate(dot(normal, -light_direction.xyz));
    float3 linear_color = base_color * (ambient_color.rgb + light_color.rgb * diffuse);
    // The render target has an sRGB format: the GPU encodes this linear value when storing it.
    return float4(linear_color, 1.0);
}

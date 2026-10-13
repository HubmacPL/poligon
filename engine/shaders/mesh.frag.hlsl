// Fragment shader for lit, single-color meshes: one directional light plus ambient.
// Register spaces follow SDL GPU rules: fragment shader uniform buffers live in space3.

cbuffer LightUniforms : register(b0, space3) {
    float4 light_direction;  // xyz: direction the light travels, normalized
    float4 light_color;
    float4 ambient_color;
};

struct FragmentInput {
    float4 clip_position : SV_Position;
    float3 world_normal : TEXCOORD0;
    float4 color : TEXCOORD1;
};

float4 main(FragmentInput input) : SV_Target0 {
    float3 normal = normalize(input.world_normal);
    float diffuse = saturate(dot(normal, -light_direction.xyz));
    float3 linear_color = input.color.rgb * (ambient_color.rgb + light_color.rgb * diffuse);
    // The render target stores sRGB-encoded bytes, so encode here (gamma 2.2 approximation).
    float3 encoded_color = pow(saturate(linear_color), 1.0 / 2.2);
    return float4(encoded_color, 1.0);
}

// XeGTAO's result (visibility times 255 in an unsigned integer image) as the ambient occlusion
// image the lighting reads, with the strength applied (Environment::ambient_occlusion_strength).

// An unsigned integer image, which SDL GPU does not let be sampled: a read-only storage image
// (a storage image in SPIR-V, a shader resource in DXIL), read with Load.
#ifdef __spirv__
[[vk::binding(0, 2)]]
RWTexture2D<uint> gtao_texture : register(u0, space2);
uint LoadResult(int2 pixel) { return gtao_texture[pixel]; }
#else
Texture2D<uint> gtao_texture : register(t0, space2);
uint LoadResult(int2 pixel) { return gtao_texture.Load(int3(pixel, 0)); }
#endif

cbuffer ApplyUniforms : register(b0, space3) {
    float4 settings;  // x: strength
};

struct FragmentInput {
    float4 clip_position : SV_Position;
    float2 uv : TEXCOORD0;
    float2 ndc : TEXCOORD1;
};

float main(FragmentInput input) : SV_Target0 {
    float visibility = saturate(float(LoadResult(int2(input.clip_position.xy))) / 255.0);
    return lerp(1.0, visibility, saturate(settings.x));
}

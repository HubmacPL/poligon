// Fragment shader for terrain.
//
// The direction of the ground comes from the height image, per pixel, so it does not get
// coarser where distant patches have few vertices. The look comes from two ground textures
// that repeat every few meters, mixed in large irregular patches, and a plain earth color
// on slopes too steep for anything to grow. Lighting is the same as for meshes
// (lighting.hlsli).
#define LIGHTING_SHADOW_BINDING 5
#define LIGHTING_SHADOW_T t5
#define LIGHTING_SHADOW_S s5
#define LIGHTING_AO_BINDING 6
#define LIGHTING_AO_T t6
#define LIGHTING_AO_S s6
#include "lighting.hlsli"

[[vk::combinedImageSampler]][[vk::binding(0, 2)]]
Texture2D<float> height_texture : register(t0, space2);
[[vk::combinedImageSampler]][[vk::binding(0, 2)]]
SamplerState height_sampler : register(s0, space2);

[[vk::combinedImageSampler]][[vk::binding(1, 2)]]
Texture2D<float4> first_color_texture : register(t1, space2);
[[vk::combinedImageSampler]][[vk::binding(1, 2)]]
SamplerState first_color_sampler : register(s1, space2);
[[vk::combinedImageSampler]][[vk::binding(2, 2)]]
Texture2D<float4> first_normal_texture : register(t2, space2);
[[vk::combinedImageSampler]][[vk::binding(2, 2)]]
SamplerState first_normal_sampler : register(s2, space2);

[[vk::combinedImageSampler]][[vk::binding(3, 2)]]
Texture2D<float4> second_color_texture : register(t3, space2);
[[vk::combinedImageSampler]][[vk::binding(3, 2)]]
SamplerState second_color_sampler : register(s3, space2);
[[vk::combinedImageSampler]][[vk::binding(4, 2)]]
Texture2D<float4> second_normal_texture : register(t4, space2);
[[vk::combinedImageSampler]][[vk::binding(4, 2)]]
SamplerState second_normal_sampler : register(s4, space2);

cbuffer TerrainMaterialUniforms : register(b1, space3) {
    float4 map;          // xy: world x and z of the height map's first point, z: meters between points
    float4 map_heights;  // y: height difference of image value 1, zw: 1 / number of points along x and z
    float4 tiling;       // x, y: meters after which the first and second texture repeat, z, w: cosine of the slope where the steep color starts and where it is complete
    float4 steep_color;  // rgb: color of steep slopes, a: roughness of the whole terrain
};

struct FragmentInput {
    float4 clip_position : SV_Position;
    float3 world_normal : TEXCOORD0;
    float4 color : TEXCOORD1;
    float2 uv : TEXCOORD2;  // position on the height map, in points
    float3 world_position : TEXCOORD3;
};

// Smooth noise: random values at whole coordinates, blended in between. Used to mix the
// ground textures in patches whose outlines follow no grid.
float Hash(float2 p) {
    return frac(sin(dot(p, float2(127.1, 311.7))) * 43758.5453);
}

float ValueNoise(float2 p) {
    float2 cell = floor(p);
    float2 f = frac(p);
    f = f * f * (3.0 - 2.0 * f);
    return lerp(lerp(Hash(cell), Hash(cell + float2(1.0, 0.0)), f.x),
                lerp(Hash(cell + float2(0.0, 1.0)), Hash(cell + float2(1.0, 1.0)), f.x), f.y);
}

float HeightAt(float2 point_position) {
    return height_texture.SampleLevel(height_sampler, (point_position + 0.5) * map_heights.zw, 0.0) * map_heights.y;
}

float4 main(FragmentInput input) : SV_Target0 {
    // The direction of the ground, from the heights one point to each side.
    float3 ground_normal = normalize(float3(HeightAt(input.uv - float2(1.0, 0.0)) - HeightAt(input.uv + float2(1.0, 0.0)),
                                            2.0 * map.z,
                                            HeightAt(input.uv - float2(0.0, 1.0)) - HeightAt(input.uv + float2(0.0, 1.0))));

    // Which of the two textures: large soft patches from two sizes of noise.
    float2 place = input.world_position.xz;
    float patches = ValueNoise(place * 0.021) * 0.65 + ValueNoise(place * 0.09 + 17.0) * 0.35;
    float second_share = smoothstep(0.42, 0.58, patches);

    // Each texture is read twice: at its own size, and seven times larger. Far from the
    // camera the large reading takes over, which hides the repetition of the small one.
    float distance_to_camera = length(camera_position.xyz - input.world_position);
    float far_share = saturate((distance_to_camera - 25.0) / 90.0) * 0.75;
    float2 first_uv = place / tiling.x;
    float2 second_uv = place / tiling.y;
    float3 first_color = lerp(first_color_texture.Sample(first_color_sampler, first_uv).rgb,
                              first_color_texture.Sample(first_color_sampler, first_uv / 7.0).rgb, far_share);
    float3 second_color = lerp(second_color_texture.Sample(second_color_sampler, second_uv).rgb,
                               second_color_texture.Sample(second_color_sampler, second_uv / 7.0).rgb, far_share);
    float3 first_map = first_normal_texture.Sample(first_normal_sampler, first_uv).xyz * 2.0 - 1.0;
    float3 second_map = second_normal_texture.Sample(second_normal_sampler, second_uv).xyz * 2.0 - 1.0;
    float3 base_color = lerp(first_color, second_color, second_share);
    float3 detail = lerp(first_map, second_map, second_share);

    // Steep slopes show bare earth, slightly mottled.
    float steep_share = saturate((tiling.z - ground_normal.y) / max(tiling.z - tiling.w, 0.001));
    float mottle = 0.8 + 0.4 * ValueNoise(place * 0.7);
    base_color = lerp(base_color, steep_color.rgb * mottle, steep_share);

    // Fine relief from the normal maps. Texture u runs along world x, v along world z, and a
    // normal map's green points to the top of the image, which is -z here. The relief fades
    // with distance, where it would only flicker.
    float relief = (1.0 - steep_share * 0.6) * (1.0 - far_share);
    float3 normal = normalize(ground_normal + float3(detail.x, 0.0, -detail.y) * relief);

    Surface surface;
    surface.base_color = base_color * input.color.rgb;
    surface.metallic = 0.0;
    surface.roughness = steep_color.a;
    surface.occlusion = 1.0;
    surface.normal = normal;
    surface.surface_normal = ground_normal;
    surface.world_position = input.world_position;
    surface.pixel = input.clip_position.xy;
    surface.thin_foliage = false;
    return float4(ShadeSurface(surface), 1.0);
}

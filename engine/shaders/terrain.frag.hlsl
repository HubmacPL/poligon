// Fragment shader for terrain.
//
// The direction of the ground comes from the height image, per pixel, so it does not get
// coarser where distant patches have few vertices. The look comes from up to eight ground
// textures (layers, kept in two texture arrays), mixed by two weight images stretched over
// the whole land, and a plain color on slopes too steep for anything to grow. Where layers
// meet, the one whose texture is brighter there (stones, the tops of clumps) wins over a
// short distance ("height blending"), so a border looks like one material poking through
// another rather than a fade. Lighting is the same as for meshes (lighting.hlsli).
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

// Layer i's color and normal map are image i of these arrays.
[[vk::combinedImageSampler]][[vk::binding(1, 2)]]
Texture2DArray<float4> layer_colors : register(t1, space2);
[[vk::combinedImageSampler]][[vk::binding(1, 2)]]
SamplerState layer_color_sampler : register(s1, space2);
[[vk::combinedImageSampler]][[vk::binding(2, 2)]]
Texture2DArray<float4> layer_normals : register(t2, space2);
[[vk::combinedImageSampler]][[vk::binding(2, 2)]]
SamplerState layer_normal_sampler : register(s2, space2);

// Weights of layers 0 to 3 (red, green, blue, alpha) and 4 to 7.
[[vk::combinedImageSampler]][[vk::binding(3, 2)]]
Texture2D<float4> low_weights : register(t3, space2);
[[vk::combinedImageSampler]][[vk::binding(3, 2)]]
SamplerState low_weight_sampler : register(s3, space2);
[[vk::combinedImageSampler]][[vk::binding(4, 2)]]
Texture2D<float4> high_weights : register(t4, space2);
[[vk::combinedImageSampler]][[vk::binding(4, 2)]]
SamplerState high_weight_sampler : register(s4, space2);

cbuffer TerrainMaterialUniforms : register(b1, space3) {
    float4 map;             // xy: world x and z of the height map's first point, z: meters between points
    float4 map_heights;     // y: height difference of image value 1, zw: 1 / number of points along x and z
    float4 steep;           // x, y: cosine of the slope where the steep color starts and where it is complete, z: number of layers, w: 1 = layer textures given
    float4 steep_color;     // rgb: color of steep slopes, a: roughness of the whole terrain
    float4 tile_scales[2];  // per layer: 1 / meters after which its texture repeats
    float4 weight_map;      // xy: height map point -> weight image coordinate scale, zw: offset
};

struct FragmentInput {
    float4 clip_position : SV_Position;
    float3 world_normal : TEXCOORD0;
    float4 color : TEXCOORD1;
    float2 uv : TEXCOORD2;  // position on the height map, in points
    float3 world_position : TEXCOORD3;
};

// Smooth noise: random values at whole coordinates, blended in between.
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
    float2 place = input.world_position.xz;
    // Texture reads below happen inside branches that differ from pixel to pixel, so the
    // rate of change that picks the mipmap is worked out here, outside them.
    float2 place_dx = ddx(place);
    float2 place_dy = ddy(place);

    // Each texture is read twice: at its own size, and seven times larger. Far from the
    // camera the large reading takes over, which hides the repetition of the small one.
    float distance_to_camera = length(camera_position.xyz - input.world_position);
    float far_share = saturate((distance_to_camera - 25.0) / 90.0) * 0.75;

    float2 weight_uv = input.uv * weight_map.xy + weight_map.zw;
    float4 low = low_weights.SampleLevel(low_weight_sampler, weight_uv, 0.0);
    float4 high = high_weights.SampleLevel(high_weight_sampler, weight_uv, 0.0);
    float weights[8] = {low.x, low.y, low.z, low.w, high.x, high.y, high.z, high.w};
    int layer_count = (int)steep.z;

    // The layers that show here: their colors, and how high each stands (its weight plus
    // how bright its texture is at this spot).
    float3 colors[8];
    float heights[8];
    float highest = -1.0;
    [unroll] for (int i = 0; i < 8; ++i) {
        colors[i] = float3(1.0, 1.0, 1.0);
        heights[i] = -1.0;
        if (i < layer_count && weights[i] > 0.004) {
            if (steep.w > 0.5) {
                float scale = tile_scales[i / 4][i % 4];
                float2 uv = place * scale;
                float3 near_color = layer_colors.SampleGrad(layer_color_sampler, float3(uv, i), place_dx * scale, place_dy * scale).rgb;
                float3 far_color = layer_colors.SampleGrad(layer_color_sampler, float3(uv / 7.0, i), place_dx * scale / 7.0,
                                                           place_dy * scale / 7.0).rgb;
                colors[i] = lerp(near_color, far_color, far_share);
            }
            heights[i] = weights[i] + dot(colors[i], float3(0.3, 0.59, 0.11)) * 0.6;
            highest = max(highest, heights[i]);
        }
    }
    if (highest < 0.0) {
        // No weight anywhere (outside the weight image's layers): layer 0.
        heights[0] = 1.0;
        highest = 1.0;
        if (steep.w > 0.5) {
            colors[0] = layer_colors.SampleGrad(layer_color_sampler, float3(place * tile_scales[0].x, 0.0),
                                                place_dx * tile_scales[0].x, place_dy * tile_scales[0].x).rgb;
        }
    }

    // Only layers within a short step of the highest one show, sharing by how far above
    // that step they stand.
    float3 base_color = float3(0.0, 0.0, 0.0);
    float3 detail = float3(0.0, 0.0, 0.0);
    float total = 0.0;
    [unroll] for (int j = 0; j < 8; ++j) {
        float share = max(heights[j] - (highest - 0.2), 0.0);
        if (share > 0.0) {
            base_color += colors[j] * share;
            if (steep.w > 0.5) {
                float scale = tile_scales[j / 4][j % 4];
                float3 layer_normal =
                    layer_normals.SampleGrad(layer_normal_sampler, float3(place * scale, j), place_dx * scale, place_dy * scale).xyz;
                detail += (layer_normal * 2.0 - 1.0) * share;
            }
            total += share;
        }
    }
    base_color /= max(total, 0.0001);
    detail = steep.w > 0.5 ? detail / max(total, 0.0001) : float3(0.0, 0.0, 1.0);

    // Steep slopes show bare earth, slightly mottled.
    float steep_share = saturate((steep.x - ground_normal.y) / max(steep.x - steep.y, 0.001));
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

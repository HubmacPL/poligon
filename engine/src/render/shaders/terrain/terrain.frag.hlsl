// Fragment shader for terrain.
//
// The direction of the ground comes from the height image, per pixel, so it does not get
// coarser where distant patches have few vertices. The look comes from up to 32 ground
// textures (layers, kept in two texture arrays), mixed by two images stretched over the
// whole land (the weights of layers 0 to 7, or four layer numbers per point and their
// weights), and a plain color on slopes too steep for anything to grow. Where layers
// meet, the one whose texture is brighter there (stones, the tops of clumps) wins over a
// short distance ("height blending"), so a border looks like one material poking through
// another rather than a fade. Lighting is the same as for meshes (lighting.hlsli).
#define LIGHTING_SHADOW_BINDING 5
#define LIGHTING_SHADOW_T t5
#define LIGHTING_SHADOW_S s5
#define LIGHTING_AO_BINDING 6
#define LIGHTING_AO_T t6
#define LIGHTING_AO_S s6
#define LIGHTING_CLOUD_BINDING 7
#define LIGHTING_CLOUD_T t7
#define LIGHTING_CLOUD_S s7
#define LIGHTING_SKY_BINDING 8
#define LIGHTING_SKY_T t8
#define LIGHTING_SKY_S s8
#define LIGHTING_RADIANCE_BINDING 9
#define LIGHTING_RADIANCE_T t9
#define LIGHTING_RADIANCE_S s9
#define LIGHTING_SPOT_SHADOW_BINDING 10
#define LIGHTING_SPOT_SHADOW_T t10
#define LIGHTING_SPOT_SHADOW_S s10
// The lights' storage buffer comes after the 11 textures.
#define LIGHTING_LIGHT_DATA_BINDING 11
#define LIGHTING_LIGHT_DATA_T t11
#include "lighting.hlsli"

[[vk::combinedImageSampler]][[vk::binding(0, 2)]]
Texture2DArray<float> height_texture : register(t0, space2);
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

// Weights of layers 0 to 3 (red, green, blue, alpha) and 4 to 7; in the layer index mode
// four layer numbers (as bytes) and their weights.
[[vk::combinedImageSampler]][[vk::binding(3, 2)]]
Texture2DArray<float4> low_weights : register(t3, space2);
[[vk::combinedImageSampler]][[vk::binding(3, 2)]]
SamplerState low_weight_sampler : register(s3, space2);
[[vk::combinedImageSampler]][[vk::binding(4, 2)]]
Texture2DArray<float4> high_weights : register(t4, space2);
[[vk::combinedImageSampler]][[vk::binding(4, 2)]]
SamplerState high_weight_sampler : register(s4, space2);

cbuffer TerrainMaterialUniforms : register(b1, space3) {
    float4 map;             // z: meters between points
    float4 map_heights;     // x: 1 = every page has its own weights, y: 1 = layer index mode, zw: 1 / number of points along x and z (of a page)
    float4 steep;           // x, y: cosine of the slope where the steep color starts and where it is complete, z: number of layers, w: 1 = layer textures given
    float4 steep_color;     // rgb: color of steep slopes, a: roughness of the whole terrain
    float4 tile_scales[8];  // per layer: 1 / meters after which its texture repeats
    float4 weight_map;      // xy: height map point -> weight image coordinate scale, zw: offset
    // Per layer: where its repeat stands at the camera (xy at its own size, zw seven times
    // larger), so that camera-relative positions tile as world ones.
    float4 tile_phases[32];
    float4 terrain_shift;   // xy: camera-relative x, z -> the terrain's own (for the mottling)
};

struct FragmentInput {
    float4 clip_position : SV_Position;
    float3 world_normal : TEXCOORD0;
    float4 color : TEXCOORD1;
    float2 uv : TEXCOORD2;  // position on the height map, in points
    float3 world_position : TEXCOORD3;
    float fade : TEXCOORD4;
    float4 motion_clip : TEXCOORD5;
    float4 previous_clip : TEXCOORD6;
    nointerpolation float2 page : TEXCOORD7;  // x: the page of the height (and weight) arrays, y: its height of image value 1
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

float HeightAt(float2 point_position, float2 page) {
    return height_texture.SampleLevel(height_sampler, float3((point_position + 0.5) * map_heights.zw, page.x), 0.0) * page.y;
}

// Layers that may show at a pixel: up to eight (layer, weight) pairs, weight 0 = unused.
#define CANDIDATES 8

// The layer index mode: the four map points around `weight_uv` each name four layers with
// weights. Equal layers are merged, the points weighed by distance (bilinearly), and the
// four strongest layers kept, their weights adding up to 1. Points are read without
// filtering: blending layer numbers would make up others.
void GatherIndexedLayers(float2 weight_uv, float weight_layer, int layer_count, out int layers[CANDIDATES],
                         out float amounts[CANDIDATES]) {
    uint width;
    uint height;
    uint elements;
    low_weights.GetDimensions(width, height, elements);
    float2 texel = weight_uv * float2(width, height) - 0.5;
    float2 cell = floor(texel);
    float2 f = texel - cell;
    int2 last = int2(width, height) - 1;

    // Up to 16 different layers from four points of four.
    int merged_layers[16];
    float merged[16];
    [unroll] for (int m = 0; m < 16; ++m) {
        merged_layers[m] = -1;
        merged[m] = 0.0;
    }
    [unroll] for (int corner = 0; corner < 4; ++corner) {
        int2 offset = int2(corner & 1, corner >> 1);
        int2 at = clamp(int2(cell) + offset, int2(0, 0), last);
        float share = (offset.x == 1 ? f.x : 1.0 - f.x) * (offset.y == 1 ? f.y : 1.0 - f.y);
        float4 numbers = low_weights.Load(int4(at, (int)weight_layer, 0));
        float4 point_amounts = high_weights.Load(int4(at, (int)weight_layer, 0));
        point_amounts *= share / max(dot(point_amounts, float4(1.0, 1.0, 1.0, 1.0)), 0.0001);
        [unroll] for (int c = 0; c < 4; ++c) {
            int layer = (int)(numbers[c] * 255.0 + 0.5);
            float amount = point_amounts[c];
            if (amount > 0.0 && layer < layer_count) {
                // Into the slot already holding this layer, or else the first free one.
                bool placed = false;
                [unroll] for (int k = 0; k < corner * 4 + c + 1; ++k) {
                    if (!placed && (merged_layers[k] == layer || merged_layers[k] < 0)) {
                        merged_layers[k] = layer;
                        merged[k] += amount;
                        placed = true;
                    }
                }
            }
        }
    }

    // The four strongest.
    float total = 0.0;
    [unroll] for (int pick = 0; pick < CANDIDATES; ++pick) {
        layers[pick] = 0;
        amounts[pick] = 0.0;
        if (pick < 4) {
            float best = 0.0;
            int best_layer = -1;
            [unroll] for (int k = 0; k < 16; ++k) {
                if (merged[k] > best) {
                    best = merged[k];
                    best_layer = merged_layers[k];
                }
            }
            [unroll] for (int j = 0; j < 16; ++j) {
                merged[j] = merged_layers[j] == best_layer ? 0.0 : merged[j];
            }
            layers[pick] = max(best_layer, 0);
            amounts[pick] = best;
            total += best;
        }
    }
    [unroll] for (int n = 0; n < 4; ++n) {
        amounts[n] /= max(total, 0.0001);
    }
}

float4 main(FragmentInput input) : SV_Target0 {
    // The direction of the ground, from the heights one point to each side.
    float3 ground_normal = normalize(float3(HeightAt(input.uv - float2(1.0, 0.0), input.page) - HeightAt(input.uv + float2(1.0, 0.0), input.page),
                                            2.0 * map.z,
                                            HeightAt(input.uv - float2(0.0, 1.0), input.page) - HeightAt(input.uv + float2(0.0, 1.0), input.page)));
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
    // map_heights.x: 1 = every page has its own weights, 0 = one weight image for all.
    float weight_layer = map_heights.x > 0.5 ? input.page.x : 0.0;
    int layer_count = (int)steep.z;

    // The layers that may show here, found one of two ways (map_heights.y).
    int layers[CANDIDATES];
    float weights[CANDIDATES];
    if (map_heights.y > 0.5) {
        GatherIndexedLayers(weight_uv, weight_layer, layer_count, layers, weights);
    } else {
        float4 low = low_weights.SampleLevel(low_weight_sampler, float3(weight_uv, weight_layer), 0.0);
        float4 high = high_weights.SampleLevel(high_weight_sampler, float3(weight_uv, weight_layer), 0.0);
        float layer_weights[8] = {low.x, low.y, low.z, low.w, high.x, high.y, high.z, high.w};
        [unroll] for (int w = 0; w < CANDIDATES; ++w) {
            layers[w] = w;
            weights[w] = w < layer_count ? layer_weights[w] : 0.0;
        }
    }

    // Their colors, and how high each stands (its weight plus how bright its texture is at
    // this spot).
    float3 colors[CANDIDATES];
    float heights[CANDIDATES];
    float highest = -1.0;
    [unroll] for (int i = 0; i < CANDIDATES; ++i) {
        colors[i] = float3(1.0, 1.0, 1.0);
        heights[i] = -1.0;
        if (weights[i] > 0.004) {
            if (steep.w > 0.5) {
                int layer = layers[i];
                float scale = tile_scales[layer / 4][layer % 4];
                float2 uv = place * scale + tile_phases[layer].xy;
                float2 far_uv = place * scale / 7.0 + tile_phases[layer].zw;
                float3 near_color = layer_colors.SampleGrad(layer_color_sampler, float3(uv, layer), place_dx * scale, place_dy * scale).rgb;
                float3 far_color = layer_colors.SampleGrad(layer_color_sampler, float3(far_uv, layer), place_dx * scale / 7.0,
                                                           place_dy * scale / 7.0).rgb;
                colors[i] = lerp(near_color, far_color, far_share);
            }
            heights[i] = weights[i] + dot(colors[i], float3(0.3, 0.59, 0.11)) * 0.6;
            highest = max(highest, heights[i]);
        }
    }
    if (highest < 0.0) {
        // No weight anywhere (outside the weight image's layers): layer 0.
        layers[0] = 0;
        heights[0] = 1.0;
        highest = 1.0;
        if (steep.w > 0.5) {
            colors[0] = layer_colors.SampleGrad(layer_color_sampler, float3(place * tile_scales[0].x + tile_phases[0].xy, 0.0),
                                                place_dx * tile_scales[0].x, place_dy * tile_scales[0].x).rgb;
        }
    }

    // Only layers within a short step of the highest one show, sharing by how far above
    // that step they stand.
    float3 base_color = float3(0.0, 0.0, 0.0);
    float3 detail = float3(0.0, 0.0, 0.0);
    float total = 0.0;
    [unroll] for (int j = 0; j < CANDIDATES; ++j) {
        float share = max(heights[j] - (highest - 0.2), 0.0);
        if (share > 0.0) {
            base_color += colors[j] * share;
            if (steep.w > 0.5) {
                int layer = layers[j];
                float scale = tile_scales[layer / 4][layer % 4];
                float3 layer_normal = layer_normals.SampleGrad(layer_normal_sampler, float3(place * scale + tile_phases[layer].xy, layer),
                                                               place_dx * scale, place_dy * scale).xyz;
                float2 normal_xy = layer_normal.xy * 2.0 - 1.0;  // z rebuilt: two-channel maps hold none
                detail += float3(normal_xy, sqrt(saturate(1.0 - dot(normal_xy, normal_xy)))) * share;
            }
            total += share;
        }
    }
    base_color /= max(total, 0.0001);
    detail = steep.w > 0.5 ? detail / max(total, 0.0001) : float3(0.0, 0.0, 1.0);

    // Steep slopes show bare earth, slightly mottled.
    float steep_share = saturate((steep.x - ground_normal.y) / max(steep.x - steep.y, 0.001));
    float mottle = 0.8 + 0.4 * ValueNoise((place + terrain_shift.xy) * 0.7);
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
    surface.emission = float3(0.0, 0.0, 0.0);
    return float4(ShadeSurface(surface), 1.0);
}

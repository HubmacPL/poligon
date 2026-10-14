// Shared by the impostor shaders. An impostor is a flat card that shows pictures of a model
// taken from many directions (Renderer::BakeImpostor); it stands in for the model far away.
//
// The pictures are taken from frames_per_side x frames_per_side directions spread over the
// half sphere above the model ("hemi-octahedral" layout): grid point g (each coordinate -1
// at the first frame, 1 at the last) maps to a direction, the middle of the grid looks
// straight down, the edge of the grid looks from the horizon. renderer.cpp holds the same
// two functions for baking; they must agree.

// Grid point (-1..1, -1..1) to the direction from the model's middle towards the camera.
float3 HemiOctahedronDirection(float2 grid) {
    float3 direction = float3((grid.x - grid.y) * 0.5, 0.0, (grid.x + grid.y) * 0.5);
    direction.y = 1.0 - abs(direction.x) - abs(direction.z);
    return normalize(direction);
}

// The inverse: a direction (below the horizon counts as on it) to its grid point.
float2 HemiOctahedronGrid(float3 direction) {
    direction.y = max(direction.y, 0.0);
    direction /= abs(direction.x) + abs(direction.y) + abs(direction.z);
    return float2(direction.x + direction.z, direction.z - direction.x);
}

// The picture's right and up for a camera looking back along `direction`. Up stays as
// close to the model's up as possible.
void ImpostorFrameAxes(float3 direction, out float3 right, out float3 up) {
    float3 side = cross(float3(0.0, 1.0, 0.0), direction);
    float side_length = length(side);
    right = side_length > 0.0001 ? side / side_length : float3(1.0, 0.0, 0.0);
    up = cross(direction, right);
}

#ifdef IMPOSTOR_FRAGMENT
// The atlases, for fragment shaders. A fragment shader that defines IMPOSTOR_FRAGMENT (and
// IMPOSTOR_NORMALS when it needs the normals) before including this file gets them as
// textures 0 and 1. They are globals because textures passed to functions as arguments do
// not compile to SPIR-V.
[[vk::combinedImageSampler]][[vk::binding(0, 2)]]
Texture2D<float4> color_atlas : register(t0, space2);
[[vk::combinedImageSampler]][[vk::binding(0, 2)]]
SamplerState color_sampler : register(s0, space2);
#ifdef IMPOSTOR_NORMALS
[[vk::combinedImageSampler]][[vk::binding(1, 2)]]
Texture2D<float4> normal_atlas : register(t1, space2);
[[vk::combinedImageSampler]][[vk::binding(1, 2)]]
SamplerState normal_sampler : register(s1, space2);
#endif

struct ImpostorTexel {
    float4 color;   // rgb: base color (linear), a: coverage
    float3 normal;  // in the model's own coordinates (zero without IMPOSTOR_NORMALS)
};

// Blends one pixel from four neighboring frames. `atlas_uv` holds each frame's position in
// the atlas, `frame_uv` the position within the frame (outside 0..1 there is nothing:
// the point lies beyond that picture's edge), `blend` the weights along x and y.
ImpostorTexel SampleImpostor(float2 atlas_uv[4], float2 frame_uv[4], float2 blend) {
    float weights[4] = {(1.0 - blend.x) * (1.0 - blend.y), blend.x * (1.0 - blend.y), (1.0 - blend.x) * blend.y,
                        blend.x * blend.y};
    float3 color = float3(0.0, 0.0, 0.0);
    float3 normal = float3(0.0, 0.0, 0.0);
    float coverage = 0.0;
    [unroll] for (int i = 0; i < 4; ++i) {
        bool inside = all(frame_uv[i] >= 0.0) && all(frame_uv[i] <= 1.0);
        float4 texel = color_atlas.Sample(color_sampler, atlas_uv[i]);
        float weight = inside ? weights[i] * texel.a : 0.0;
        color += texel.rgb * weight;
        coverage += weight;
#ifdef IMPOSTOR_NORMALS
        normal += (normal_atlas.Sample(normal_sampler, atlas_uv[i]).xyz * 2.0 - 1.0) * weight;
#endif
    }
    ImpostorTexel result;
    result.color = float4(color / max(coverage, 0.0001), coverage);
    result.normal = normal;
    return result;
}
#endif

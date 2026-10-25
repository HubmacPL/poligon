#pragma once

// Decals recorded this frame (Renderer::DrawDecal) and what draws them: resources/decals.cpp
// records and makes the pipeline, passes/decal_pass.cpp draws them onto the scene.

#include <vector>

#include "dynamic_buffer.h"
#include "poligon/core/math.h"
#include "poligon/render/decal.h"

namespace poligon {
namespace render_internal {

struct DecalCommand {
    glm::mat4 transform;
    DecalMaterial material;
};

// One decal as the decal shaders read it (128 bytes; see decal.vert.hlsl).
struct DecalInstance {
    glm::mat4 transform;
    glm::mat4 inverse;
};

struct DecalUniforms {
    glm::mat4 inverse_view_projection;
    glm::vec4 screen;    // xy: drawn size in pixels, z: 1 = normal map, w: opacity
    glm::vec4 color;     // linear color and alpha
    glm::vec4 surface;   // x: metallic, y: roughness, z: edge fade, w: cosine of the largest angle
};

struct DecalState {
    std::vector<DecalCommand> commands;
    std::vector<DecalInstance> instances;  // filled by the pass, in drawing order
    DynamicBuffer buffer;
    SDL_GPUGraphicsPipeline* pipeline = nullptr;
};

// True when two decals can be drawn in one call (the same material).
bool SameDecalMaterial(const DecalMaterial& a, const DecalMaterial& b);

} // namespace render_internal
} // namespace poligon

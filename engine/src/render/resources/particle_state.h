#pragma once

// Particles recorded this frame (Renderer::DrawParticles) and what draws them:
// resources/particles.cpp records them and makes the pipelines, passes/particle_pass.cpp
// draws them over the scene.

#include <vector>

#include "dynamic_buffer.h"
#include "poligon/render/particles.h"
#include "poligon/render/renderer.h"

namespace poligon {
namespace render_internal {

// One particle as the particle shaders read it (64 bytes; see particle.vert.hlsl).
struct ParticleCard {
    glm::vec4 position_size;
    glm::vec4 velocity_rotation;
    glm::vec4 color;
    glm::vec4 frame;  // x: picture of the atlas
};

// One DrawParticles call: its cards lie at [first, first + count) of ParticleState::cards,
// sorted from back to front.
struct ParticleCall {
    TextureHandle texture;
    ParticleOptions options;
    Uint32 first = 0;
    Uint32 count = 0;
};

struct ParticleUniforms {
    glm::vec4 screen;       // xy: size of the image drawn into, zw: size of the depth image
    glm::vec4 projection;   // projection_info for ViewDistance (common.hlsli)
    glm::vec4 settings;     // x: soft distance, y: 1 = lit, z: 1 = additive
};

struct ParticleState {
    std::vector<ParticleCall> calls;
    std::vector<ParticleCard> cards;
    std::vector<std::pair<float, Uint32>> order;  // scratch for sorting
    DynamicBuffer buffer;
    SDL_GPUGraphicsPipeline* alpha_pipeline = nullptr;
    SDL_GPUGraphicsPipeline* additive_pipeline = nullptr;
};

} // namespace render_internal
} // namespace poligon

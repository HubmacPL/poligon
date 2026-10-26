#pragma once

// Point and spot lights recorded this frame (Renderer::DrawLight): resources/lights.cpp records
// them, keeps the nearest kMaxLights, assigns them to screen tiles and builds LightUniforms,
// which every lit fragment shader reads (lighting.hlsli); passes/spot_shadow_pass.cpp draws
// the shadows of up to kMaxShadowedSpotLights spot lights.

#include <SDL3/SDL_gpu.h>

#include <vector>

#include "dynamic_buffer.h"
#include "poligon/core/math.h"

namespace poligon {
namespace render_internal {

// The screen is cut into this many tiles whatever its size (40 x 40 pixels at 1920 x 1080);
// each tile holds a 64-bit mask of the lights that may reach it.
constexpr int kLightTilesX = 48;
constexpr int kLightTilesY = 27;
// Each spot light shadow is one quarter of its own depth image.
constexpr int kSpotShadowSize = 1024;

struct LightRecord {
    glm::vec3 position{0.0f};  // relative to the camera
    glm::vec3 direction{0.0f, -1.0f, 0.0f};
    glm::vec3 color{0.0f};     // color times intensity
    float range = 0.0f;
    float cos_inner = -2.0f;   // a point light: below -1, so every direction is inside
    float cos_outer = -2.0f;
    bool spot = false;
    bool shadows = false;
};

// One light as the shaders read it (64 bytes).
struct GpuLight {
    glm::vec4 position_range;
    glm::vec4 color_spot;         // w: 1 = spot
    glm::vec4 direction_outer;    // w: cosine of the outer angle
    glm::vec4 inner_shadow;       // x: cosine of the inner angle, y: shadow tile (-1 = none)
};

// The lights and the tiles' masks, read by the shaders from a storage buffer (light_data in
// lights.hlsli: lights from byte 0, tiles from byte 4096). Too large for a uniform buffer: SDL
// GPU's Vulkan backend shows a shader only the first 4 KB of one.
struct LightData {
    GpuLight lights[64];
    glm::uvec4 tiles[kLightTilesX * kLightTilesY / 2];  // two tiles per element: (low, high) bits each
};

// Must match LightUniforms in lights.hlsli. Pushed as fragment uniform buffer 3.
struct LightUniforms {
    glm::vec4 info;                     // x: light count, y, z: tiles across and down, w: 0
    glm::vec4 pixel_to_tile;            // xy: tiles per pixel of the image the pass draws
    glm::mat4 spot_shadow_matrices[4];  // world position -> shadow image coordinates and depth
};

struct LightState {
    std::vector<LightRecord> records;  // this frame, in the order drawn
    LightData data{};                  // built at the start of EndFrame
    LightUniforms uniforms{};
    DynamicBuffer buffer;              // `data` on the graphics card
    // Spot lights with shadows this frame (indices into uniforms.lights) and their cameras.
    int shadowed_count = 0;
    int shadowed_lights[4] = {0, 0, 0, 0};
    glm::mat4 spot_view_projection[4];
    SDL_GPUTexture* spot_shadow_atlas = nullptr;  // 2 x 2 tiles of kSpotShadowSize, made on first use
};

} // namespace render_internal
} // namespace poligon

#pragma once

// The sky from the atmosphere model (Environment::sky_model = SkyModel::Atmosphere): its
// tables on the graphics card and what the frame takes from it. resources/atmosphere.cpp
// makes the tables and applies the model to the frame's light; passes/atmosphere_pass.cpp
// builds the sky view table each frame; the sky and the sky light map read it.

#include <SDL3/SDL_gpu.h>

#include "poligon/core/math.h"
#include "poligon/render/sky.h"

namespace poligon {
namespace render_internal {

struct AtmosphereState {
    // Built on the first frame that uses the model (render/sky/sky.cpp computes them).
    SDL_GPUTexture* transmittance = nullptr;
    SDL_GPUTexture* multiple_scattering = nullptr;
    SDL_GPUTexture* sky_view = nullptr;  // drawn every frame with the model on
    SDL_GPUGraphicsPipeline* sky_view_pipeline = nullptr;
    // This frame: the model is on, and the sun's color above the air (DirectionalLight::color
    // as the game gave it; the frame's light is that times what the air lets through).
    bool on = false;
    glm::vec3 sun_above_air{0.0f};
    // ComputeSkyLight for sky_direction, kept while the sun stays.
    SkyLight sky;
    glm::vec3 sky_direction{0.0f};
    bool sky_valid = false;
};

// Must match atmosphere_sky_view.frag.hlsl.
struct SkyViewUniforms {
    glm::vec4 to_sun;
};

} // namespace render_internal
} // namespace poligon

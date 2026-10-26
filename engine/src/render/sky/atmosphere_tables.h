#pragma once

// The atmosphere model's tables, built once on the processor (sky.cpp) and uploaded to the
// graphics card for the sky shaders (resources/atmosphere.cpp, shaders/sky/atmosphere.hlsli).
// The layouts follow Hillaire's "A Scalable and Production Ready Sky and Atmosphere Rendering
// Technique" (2020) and its MIT-licensed code (github.com/sebh/UnrealEngineSkyAtmosphere).

#include <vector>

#include "poligon/core/math.h"

namespace poligon {
namespace render_internal {

// Transmittance: the share of light let through from a point to the top of the atmosphere,
// by height (rows) and the cosine of the angle from straight up (columns), Bruneton's mapping.
constexpr int kTransmittanceWidth = 256;
constexpr int kTransmittanceHeight = 64;
// Multiple scattering: light scattered two or more times, per unit of scattering, by the
// sun's cosine from straight up (columns) and height (rows).
constexpr int kMultipleScatteringSize = 32;
// The sky view table the shaders build every frame (192 x 108 in Hillaire's code).
constexpr int kSkyViewWidth = 192;
constexpr int kSkyViewHeight = 108;

struct AtmosphereTables {
    std::vector<glm::vec3> transmittance;        // kTransmittanceWidth * kTransmittanceHeight, row by row
    std::vector<glm::vec3> multiple_scattering;  // kMultipleScatteringSize squared
};

// Built on the first call (thread-safe), then shared.
const AtmosphereTables& GetAtmosphereTables();

} // namespace render_internal
} // namespace poligon

#pragma once

// The sun's place in the sky and the light of a clear sky, from a model of the atmosphere
// (Rayleigh and Mie scattering with ozone, after Hillaire 2020; see rendering.md, "Sky from
// the atmosphere"). The renderer uses these when Environment::sky_model is
// SkyModel::Atmosphere; games may call them too (a clock, a sundial, the light at a time).

#include "poligon/core/math.h"

namespace poligon {

// The direction sunlight travels (from the sun towards the ground, as
// DirectionalLight::direction) at `time_of_day` hours of local solar time (12 = the sun at
// its highest), on `day_of_year` (1 = 1 January, 172 = the summer solstice; fractions allowed),
// at `latitude_degrees` (north positive). World axes: +x east, -z north, +y up. Uses the
// usual approximation of the sun's declination (within about half a degree), without the
// equation of time: solar time, not clock time.
glm::vec3 ComputeSunDirection(float time_of_day, float day_of_year, float latitude_degrees);

// The light of a clear sky for a sun direction (DirectionalLight::direction), relative to the
// sun's light above the atmosphere: multiply each by the sun's color there. The values are
// in the units the renderer lights with: a white matte surface facing the sun shows
// sun_at_ground; one under an even sky of color c shows c.
struct SkyLight {
    // The share of sunlight that reaches the ground, per color (0 when the sun has set).
    glm::vec3 sun_at_ground{0.0f};
    // The sky's color straight up, at eye level (averaged around the horizon) and the light
    // from the ground below the horizon (ground of albedo 0.3 lit by sun and sky).
    glm::vec3 zenith{0.0f};
    glm::vec3 horizon{0.0f};
    glm::vec3 ground{0.0f};
};

// Seen from sea level. The first call builds the model's tables (a few tens of
// milliseconds); later calls take well under a millisecond.
SkyLight ComputeSkyLight(const glm::vec3& sun_direction);

} // namespace poligon

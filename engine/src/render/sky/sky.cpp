// The sun's place (ComputeSunDirection) and the atmosphere model on the processor: the tables
// the sky shaders read (GetAtmosphereTables) and the light of a clear sky (ComputeSkyLight).
//
// The model and its numbers are Hillaire's ("A Scalable and Production Ready Sky and
// Atmosphere Rendering Technique", 2020), ported from his MIT-licensed code,
// github.com/sebh/UnrealEngineSkyAtmosphere, Copyright (c) 2020 Epic Games, Inc. (the license
// text is in shaders/sky/atmosphere.hlsli). Earth of radius 6360 km, air up to 100 km;
// Rayleigh scattering thinning out over 8 km, Mie over 1.2 km, an ozone layer around 25 km.
// Lengths are in kilometers, the planet's center at 0, y up. Differences from that code: y
// is up instead of z; the ground under the multiple scattering has albedo 0.3 instead of 0.

#include "poligon/render/sky.h"

#include <algorithm>
#include <cmath>

#include "atmosphere_tables.h"

namespace poligon {
namespace render_internal {
namespace {

constexpr float kPi = 3.14159265f;
constexpr float kBottomRadius = 6360.0f;
constexpr float kTopRadius = 6460.0f;
constexpr float kRayleighScaleHeight = 8.0f;
constexpr float kMieScaleHeight = 1.2f;
constexpr float kMiePhaseG = 0.8f;
constexpr float kGroundAlbedo = 0.3f;
constexpr float kPlanetRadiusOffset = 0.01f;
// Where the sky is seen from: 200 m above sea level (also in atmosphere.hlsli).
constexpr float kViewHeight = 0.2f;
const glm::vec3 kRayleighScattering(0.005802f, 0.013558f, 0.033100f);
const glm::vec3 kMieScattering(0.003996f);
const glm::vec3 kMieExtinction(0.004440f);
const glm::vec3 kOzoneAbsorption(0.000650f, 0.001881f, 0.000085f);

struct Medium {
    glm::vec3 scattering;
    glm::vec3 extinction;
    glm::vec3 mie_scattering;
    glm::vec3 rayleigh_scattering;
};

Medium SampleMedium(const glm::vec3& position) {
    const float height = glm::length(position) - kBottomRadius;
    const float mie = std::exp(-height / kMieScaleHeight);
    const float rayleigh = std::exp(-height / kRayleighScaleHeight);
    const float ozone =
        glm::clamp(height < 25.0f ? height / 15.0f - 2.0f / 3.0f : -height / 15.0f + 8.0f / 3.0f, 0.0f, 1.0f);
    Medium medium;
    medium.mie_scattering = mie * kMieScattering;
    medium.rayleigh_scattering = rayleigh * kRayleighScattering;
    medium.scattering = medium.mie_scattering + medium.rayleigh_scattering;
    medium.extinction = mie * kMieExtinction + medium.rayleigh_scattering + ozone * kOzoneAbsorption;
    return medium;
}

// Distance along the ray to the nearest crossing of a sphere around `center`, or -1.
float RaySphereNearest(const glm::vec3& origin, const glm::vec3& direction, const glm::vec3& center, float radius) {
    const glm::vec3 offset = origin - center;
    const float b = 2.0f * glm::dot(direction, offset);
    const float c = glm::dot(offset, offset) - radius * radius;
    const float delta = b * b - 4.0f * c;
    if (delta < 0.0f) {
        return -1.0f;
    }
    const float near_hit = (-b - std::sqrt(delta)) * 0.5f;
    const float far_hit = (-b + std::sqrt(delta)) * 0.5f;
    if (near_hit < 0.0f && far_hit < 0.0f) {
        return -1.0f;
    }
    if (near_hit < 0.0f) {
        return std::max(0.0f, far_hit);
    }
    if (far_hit < 0.0f) {
        return std::max(0.0f, near_hit);
    }
    return std::max(0.0f, std::min(near_hit, far_hit));
}

// How far a ray goes through the air: to the top of the atmosphere or to the ground.
float AirLength(const glm::vec3& origin, const glm::vec3& direction, float& bottom) {
    bottom = RaySphereNearest(origin, direction, glm::vec3(0.0f), kBottomRadius);
    const float top = RaySphereNearest(origin, direction, glm::vec3(0.0f), kTopRadius);
    if (bottom < 0.0f) {
        return std::max(top, 0.0f);
    }
    return top > 0.0f ? std::min(top, bottom) : 0.0f;
}

// Bruneton's mapping of (radius, cosine from straight up) to the transmittance table.
glm::vec2 TransmittanceUv(float radius, float cos_zenith) {
    const float h = std::sqrt(std::max(0.0f, kTopRadius * kTopRadius - kBottomRadius * kBottomRadius));
    const float rho = std::sqrt(std::max(0.0f, radius * radius - kBottomRadius * kBottomRadius));
    const float discriminant = radius * radius * (cos_zenith * cos_zenith - 1.0f) + kTopRadius * kTopRadius;
    const float d = std::max(0.0f, -radius * cos_zenith + std::sqrt(std::max(discriminant, 0.0f)));
    const float d_min = kTopRadius - radius;
    const float d_max = rho + h;
    return glm::vec2((d - d_min) / (d_max - d_min), rho / h);
}

void TransmittanceParams(glm::vec2 uv, float& radius, float& cos_zenith) {
    const float h = std::sqrt(kTopRadius * kTopRadius - kBottomRadius * kBottomRadius);
    const float rho = h * uv.y;
    radius = std::sqrt(rho * rho + kBottomRadius * kBottomRadius);
    const float d_min = kTopRadius - radius;
    const float d_max = rho + h;
    const float d = d_min + uv.x * (d_max - d_min);
    cos_zenith = d == 0.0f ? 1.0f : (h * h - rho * rho - d * d) / (2.0f * radius * d);
    cos_zenith = glm::clamp(cos_zenith, -1.0f, 1.0f);
}

// A texel's middle maps to the table's edge (as fromUnitToSubUvs in Hillaire's code).
float UnitToSubUv(float u, float resolution) {
    return (u + 0.5f / resolution) * (resolution / (resolution + 1.0f));
}
float SubUvToUnit(float u, float resolution) {
    return (u - 0.5f / resolution) * (resolution / (resolution - 1.0f));
}

// Bilinear, clamped at the edges, as the graphics card samples the same table.
glm::vec3 SampleTable(const std::vector<glm::vec3>& table, int width, int height, glm::vec2 uv) {
    const float x = glm::clamp(uv.x * static_cast<float>(width) - 0.5f, 0.0f, static_cast<float>(width - 1));
    const float y = glm::clamp(uv.y * static_cast<float>(height) - 0.5f, 0.0f, static_cast<float>(height - 1));
    const int x0 = static_cast<int>(x);
    const int y0 = static_cast<int>(y);
    const int x1 = std::min(x0 + 1, width - 1);
    const int y1 = std::min(y0 + 1, height - 1);
    const float fx = x - static_cast<float>(x0);
    const float fy = y - static_cast<float>(y0);
    const auto at = [&](int column, int row) { return table[static_cast<size_t>(row * width + column)]; };
    return glm::mix(glm::mix(at(x0, y0), at(x1, y0), fx), glm::mix(at(x0, y1), at(x1, y1), fx), fy);
}

glm::vec3 TransmittanceToTop(const AtmosphereTables& tables, float radius, float cos_zenith) {
    return SampleTable(tables.transmittance, kTransmittanceWidth, kTransmittanceHeight, TransmittanceUv(radius, cos_zenith));
}

glm::vec3 MultipleScatteringAt(const AtmosphereTables& tables, float radius, float cos_sun_zenith) {
    glm::vec2 uv(glm::clamp(cos_sun_zenith * 0.5f + 0.5f, 0.0f, 1.0f),
                 glm::clamp((radius - kBottomRadius) / (kTopRadius - kBottomRadius), 0.0f, 1.0f));
    const float size = static_cast<float>(kMultipleScatteringSize);
    uv = glm::vec2(UnitToSubUv(uv.x, size), UnitToSubUv(uv.y, size));
    return SampleTable(tables.multiple_scattering, kMultipleScatteringSize, kMultipleScatteringSize, uv);
}

float RayleighPhase(float cos_angle) {
    return 3.0f / (16.0f * kPi) * (1.0f + cos_angle * cos_angle);
}

// Cornette-Shanks, as Hillaire's code; cos_angle between the view and the way to the sun.
float MiePhase(float cos_angle) {
    const float g = kMiePhaseG;
    const float k = 3.0f / (8.0f * kPi) * (1.0f - g * g) / (2.0f + g * g);
    return k * (1.0f + cos_angle * cos_angle) / std::pow(1.0f + g * g - 2.0f * g * cos_angle, 1.5f);
}

struct Scattering {
    glm::vec3 luminance{0.0f};
    glm::vec3 multiple_as_one{0.0f};  // light scattered on, for unit light coming in (MultiScatAs1)
};

// IntegrateScatteredLuminance with a fixed number of steps, for a sun of illuminance 1.
// `with_phase`: Rayleigh and Mie phase functions (else even scattering); `multiple`: add the
// multiple scattering table; `ground`: add light bounced off the ground where the ray ends.
Scattering IntegrateScattering(const AtmosphereTables& tables, const glm::vec3& origin, const glm::vec3& direction,
                               const glm::vec3& to_sun, int steps, bool with_phase, bool multiple, bool ground) {
    Scattering result;
    float bottom = 0.0f;
    const float length = AirLength(origin, direction, bottom);
    if (length <= 0.0f) {
        return result;
    }
    const float cos_angle = glm::dot(to_sun, direction);
    const float mie_phase = MiePhase(cos_angle);
    const float rayleigh_phase = RayleighPhase(cos_angle);
    const float even_phase = 1.0f / (4.0f * kPi);
    glm::vec3 throughput(1.0f);
    float t = 0.0f;
    for (int step = 0; step < steps; ++step) {
        const float next_t = length * (static_cast<float>(step) + 0.3f) / static_cast<float>(steps);
        const float dt = next_t - t;
        t = next_t;
        const glm::vec3 point = origin + direction * t;
        const Medium medium = SampleMedium(point);
        const glm::vec3 step_transmittance = glm::exp(-medium.extinction * dt);
        const float radius = glm::length(point);
        const glm::vec3 up = point / radius;
        const float cos_sun = glm::dot(to_sun, up);
        const glm::vec3 to_top = TransmittanceToTop(tables, radius, cos_sun);
        const glm::vec3 phase_scattering = with_phase ? medium.mie_scattering * mie_phase + medium.rayleigh_scattering * rayleigh_phase
                                                      : medium.scattering * even_phase;
        const float earth = RaySphereNearest(point, to_sun, up * kPlanetRadiusOffset, kBottomRadius);
        const float lit = earth >= 0.0f ? 0.0f : 1.0f;
        const glm::vec3 multiple_light = multiple ? MultipleScatteringAt(tables, radius, cos_sun) : glm::vec3(0.0f);
        const glm::vec3 source = lit * to_top * phase_scattering + multiple_light * medium.scattering;
        // Each step integrated exactly for its constant medium (Hillaire 2015, slide 28).
        const glm::vec3 kept = glm::vec3(1.0f) - step_transmittance;
        result.multiple_as_one += throughput * medium.scattering * kept / medium.extinction;
        result.luminance += throughput * source * kept / medium.extinction;
        throughput *= step_transmittance;
    }
    if (ground && bottom > 0.0f && length == bottom) {
        const glm::vec3 point = origin + direction * bottom;
        const float radius = glm::length(point);
        const glm::vec3 up = point / radius;
        const float cos_sun = glm::dot(to_sun, up);
        result.luminance += TransmittanceToTop(tables, radius, cos_sun) * throughput * std::max(cos_sun, 0.0f) * kGroundAlbedo / kPi;
    }
    return result;
}

AtmosphereTables BuildTables() {
    AtmosphereTables tables;
    tables.transmittance.resize(static_cast<size_t>(kTransmittanceWidth * kTransmittanceHeight));
    for (int y = 0; y < kTransmittanceHeight; ++y) {
        for (int x = 0; x < kTransmittanceWidth; ++x) {
            float radius = 0.0f;
            float cos_zenith = 0.0f;
            TransmittanceParams(glm::vec2((static_cast<float>(x) + 0.5f) / kTransmittanceWidth,
                                          (static_cast<float>(y) + 0.5f) / kTransmittanceHeight),
                                radius, cos_zenith);
            const glm::vec3 origin(0.0f, radius, 0.0f);
            const glm::vec3 direction(std::sqrt(std::max(0.0f, 1.0f - cos_zenith * cos_zenith)), cos_zenith, 0.0f);
            float bottom = 0.0f;
            const float length = AirLength(origin, direction, bottom);
            constexpr int kSteps = 40;
            glm::vec3 optical_depth(0.0f);
            float t = 0.0f;
            for (int step = 0; step < kSteps; ++step) {
                const float next_t = length * (static_cast<float>(step) + 0.3f) / kSteps;
                const float dt = next_t - t;
                t = next_t;
                optical_depth += SampleMedium(origin + direction * t).extinction * dt;
            }
            tables.transmittance[static_cast<size_t>(y * kTransmittanceWidth + x)] = glm::exp(-optical_depth);
        }
    }

    // Light scattered twice or more, from 64 directions around each point, summed as a
    // geometric series of orders (Hillaire 2020, equations 5 to 10).
    tables.multiple_scattering.resize(static_cast<size_t>(kMultipleScatteringSize * kMultipleScatteringSize));
    const float size = static_cast<float>(kMultipleScatteringSize);
    for (int y = 0; y < kMultipleScatteringSize; ++y) {
        for (int x = 0; x < kMultipleScatteringSize; ++x) {
            const float u = SubUvToUnit((static_cast<float>(x) + 0.5f) / size, size);
            const float v = SubUvToUnit((static_cast<float>(y) + 0.5f) / size, size);
            const float cos_sun = u * 2.0f - 1.0f;
            const glm::vec3 to_sun(std::sqrt(glm::clamp(1.0f - cos_sun * cos_sun, 0.0f, 1.0f)), cos_sun, 0.0f);
            const float radius = kBottomRadius + glm::clamp(v + kPlanetRadiusOffset, 0.0f, 1.0f) *
                                                     (kTopRadius - kBottomRadius - kPlanetRadiusOffset);
            const glm::vec3 origin(0.0f, radius, 0.0f);
            glm::vec3 luminance(0.0f);
            glm::vec3 multiple_as_one(0.0f);
            for (int sample = 0; sample < 64; ++sample) {
                const float a = (0.5f + static_cast<float>(sample / 8)) / 8.0f;
                const float b = (0.5f + static_cast<float>(sample % 8)) / 8.0f;
                const float theta = 2.0f * kPi * a;
                const float phi = std::acos(1.0f - 2.0f * b);
                const glm::vec3 direction(std::cos(theta) * std::sin(phi), std::cos(phi), std::sin(theta) * std::sin(phi));
                const Scattering scattering = IntegrateScattering(tables, origin, direction, to_sun, 20, false, false, true);
                luminance += scattering.luminance;
                multiple_as_one += scattering.multiple_as_one;
            }
            // The 64 samples average over the sphere; times the even phase function's 1 / (4 pi)
            // that is the same as summing them times 4 pi / 64 and dividing by 4 pi.
            luminance /= 64.0f;
            multiple_as_one /= 64.0f;
            tables.multiple_scattering[static_cast<size_t>(y * kMultipleScatteringSize + x)] =
                luminance / (glm::vec3(1.0f) - multiple_as_one);
        }
    }
    return tables;
}

} // namespace

const AtmosphereTables& GetAtmosphereTables() {
    static const AtmosphereTables tables = BuildTables();
    return tables;
}

} // namespace render_internal

glm::vec3 ComputeSunDirection(float time_of_day, float day_of_year, float latitude_degrees) {
    const float declination = glm::radians(-23.44f) * std::cos(2.0f * render_internal::kPi / 365.0f * (day_of_year + 10.0f));
    const float hour_angle = glm::radians(15.0f * (time_of_day - 12.0f));
    const float latitude = glm::radians(latitude_degrees);
    const float up = std::sin(latitude) * std::sin(declination) + std::cos(latitude) * std::cos(declination) * std::cos(hour_angle);
    const float east = -std::cos(declination) * std::sin(hour_angle);
    const float north = std::cos(latitude) * std::sin(declination) - std::sin(latitude) * std::cos(declination) * std::cos(hour_angle);
    // North is -z.
    return -glm::normalize(glm::vec3(east, up, -north));
}

SkyLight ComputeSkyLight(const glm::vec3& sun_direction) {
    using namespace render_internal;
    const AtmosphereTables& tables = GetAtmosphereTables();
    const glm::vec3 to_sun = -glm::normalize(sun_direction);
    const float radius = kBottomRadius + kViewHeight;
    const glm::vec3 origin(0.0f, radius, 0.0f);
    SkyLight light;
    // The sun's disc sinks below the horizon (lowered by the view's height) over its own width.
    const float horizon_cos = -std::sqrt(std::max(0.0f, 1.0f - (kBottomRadius / radius) * (kBottomRadius / radius)));
    const float above = glm::smoothstep(-0.0047f, 0.0047f, to_sun.y - horizon_cos);
    light.sun_at_ground = TransmittanceToTop(tables, radius, to_sun.y) * above;
    // Radiance to the renderer's units: a white matte surface under an even sky of radiance
    // L shows pi L when one under a sun of illuminance 1 shows 1.
    constexpr int kSteps = 32;
    light.zenith = kPi * IntegrateScattering(tables, origin, glm::vec3(0.0f, 1.0f, 0.0f), to_sun, kSteps, true, true, false).luminance;
    const float elevation = glm::radians(1.5f);
    glm::vec3 horizon(0.0f);
    for (int i = 0; i < 8; ++i) {
        const float azimuth = 2.0f * kPi * static_cast<float>(i) / 8.0f;
        const glm::vec3 direction(std::cos(azimuth) * std::cos(elevation), std::sin(elevation), std::sin(azimuth) * std::cos(elevation));
        horizon += IntegrateScattering(tables, origin, direction, to_sun, kSteps, true, true, false).luminance;
    }
    light.horizon = kPi * horizon / 8.0f;
    light.ground = kGroundAlbedo * (light.sun_at_ground * std::max(to_sun.y, 0.0f) + (light.zenith + light.horizon) * 0.5f);
    return light;
}

} // namespace poligon

// The sky from a model of the atmosphere: Rayleigh and Mie scattering with ozone, single
// scattering walked along the view with a transmittance table and a table of multiple
// scattering (both built on the processor, render/sky/sky.cpp), gathered into a small sky view
// table every frame (atmosphere_sky_view.frag.hlsl) that the sky and the sky light map read.
// Lengths in kilometers, the planet's center at 0, y up.
//
// Ported from Hillaire, "A Scalable and Production Ready Sky and Atmosphere Rendering
// Technique" (2020), code at github.com/sebh/UnrealEngineSkyAtmosphere, under this license:
//
//   MIT License
//
//   Copyright (c) 2020 Epic Games, Inc.
//
//   Permission is hereby granted, free of charge, to any person obtaining a copy
//   of this software and associated documentation files (the "Software"), to deal
//   in the Software without restriction, including without limitation the rights
//   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//   copies of the Software, and to permit persons to whom the Software is
//   furnished to do so, subject to the following conditions:
//
//   The above copyright notice and this permission notice shall be included in all
//   copies or substantial portions of the Software.
//
//   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//   OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
//   SOFTWARE.
//
// A shader that includes this defines where its tables are bound first:
// ATMOSPHERE_TRANSMITTANCE_BINDING/_T/_S and, for the sky view table,
// ATMOSPHERE_SKY_VIEW_BINDING/_T/_S; ATMOSPHERE_MULTIPLE_BINDING/_T/_S only where the sky
// view table is built.
#ifndef POLIGON_ATMOSPHERE_HLSLI
#define POLIGON_ATMOSPHERE_HLSLI

static const float kAtmospherePi = 3.14159265;
static const float kBottomRadius = 6360.0;
static const float kTopRadius = 6460.0;
static const float kPlanetRadiusOffset = 0.01;
// Where the sky is seen from: 200 m above sea level (as kViewHeight in sky.cpp).
static const float kViewRadius = 6360.2;
static const float3 kRayleighScattering = float3(0.005802, 0.013558, 0.033100);
static const float3 kMieScattering = float3(0.003996, 0.003996, 0.003996);
static const float3 kMieExtinction = float3(0.004440, 0.004440, 0.004440);
static const float3 kOzoneAbsorption = float3(0.000650, 0.001881, 0.000085);
static const float kMiePhaseG = 0.8;
static const float2 kSkyViewSize = float2(192.0, 108.0);
static const float kMultipleScatteringSize = 32.0;

[[vk::combinedImageSampler]][[vk::binding(ATMOSPHERE_TRANSMITTANCE_BINDING, 2)]]
Texture2D<float4> transmittance_texture : register(ATMOSPHERE_TRANSMITTANCE_T, space2);
[[vk::combinedImageSampler]][[vk::binding(ATMOSPHERE_TRANSMITTANCE_BINDING, 2)]]
SamplerState transmittance_sampler : register(ATMOSPHERE_TRANSMITTANCE_S, space2);

#ifdef ATMOSPHERE_SKY_VIEW_BINDING
[[vk::combinedImageSampler]][[vk::binding(ATMOSPHERE_SKY_VIEW_BINDING, 2)]]
Texture2D<float4> sky_view_texture : register(ATMOSPHERE_SKY_VIEW_T, space2);
[[vk::combinedImageSampler]][[vk::binding(ATMOSPHERE_SKY_VIEW_BINDING, 2)]]
SamplerState sky_view_sampler : register(ATMOSPHERE_SKY_VIEW_S, space2);
#endif

#ifdef ATMOSPHERE_MULTIPLE_BINDING
[[vk::combinedImageSampler]][[vk::binding(ATMOSPHERE_MULTIPLE_BINDING, 2)]]
Texture2D<float4> multiple_scattering_texture : register(ATMOSPHERE_MULTIPLE_T, space2);
[[vk::combinedImageSampler]][[vk::binding(ATMOSPHERE_MULTIPLE_BINDING, 2)]]
SamplerState multiple_scattering_sampler : register(ATMOSPHERE_MULTIPLE_S, space2);
#endif

// Distance along the ray to the nearest crossing of a sphere around `center`, or -1.
float RaySphereNearest(float3 origin, float3 direction, float3 center, float radius) {
    float3 offset = origin - center;
    float b = 2.0 * dot(direction, offset);
    float c = dot(offset, offset) - radius * radius;
    float delta = b * b - 4.0 * c;
    if (delta < 0.0) {
        return -1.0;
    }
    float near_hit = (-b - sqrt(delta)) * 0.5;
    float far_hit = (-b + sqrt(delta)) * 0.5;
    if (near_hit < 0.0 && far_hit < 0.0) {
        return -1.0;
    }
    if (near_hit < 0.0) {
        return max(0.0, far_hit);
    }
    if (far_hit < 0.0) {
        return max(0.0, near_hit);
    }
    return max(0.0, min(near_hit, far_hit));
}

// A texel's middle maps to the table's edge (fromUnitToSubUvs and back).
float UnitToSubUv(float u, float resolution) {
    return (u + 0.5 / resolution) * (resolution / (resolution + 1.0));
}
float SubUvToUnit(float u, float resolution) {
    return (u - 0.5 / resolution) * (resolution / (resolution - 1.0));
}

// The share of light let through from a point at `radius` to the top of the atmosphere,
// looking `cos_zenith` from straight up (Bruneton's mapping, LutTransmittanceParamsToUv).
float3 TransmittanceToTop(float radius, float cos_zenith) {
    float h = sqrt(max(0.0, kTopRadius * kTopRadius - kBottomRadius * kBottomRadius));
    float rho = sqrt(max(0.0, radius * radius - kBottomRadius * kBottomRadius));
    float discriminant = radius * radius * (cos_zenith * cos_zenith - 1.0) + kTopRadius * kTopRadius;
    float d = max(0.0, -radius * cos_zenith + sqrt(max(discriminant, 0.0)));
    float d_min = kTopRadius - radius;
    float d_max = rho + h;
    float2 uv = float2((d - d_min) / (d_max - d_min), rho / h);
    return transmittance_texture.SampleLevel(transmittance_sampler, uv, 0.0).rgb;
}

// The sun's light reaching the viewer through the air along `to_sun` (0 below the horizon).
float3 SunTransmittance(float3 to_sun) {
    float horizon_cos = -sqrt(max(0.0, 1.0 - (kBottomRadius / kViewRadius) * (kBottomRadius / kViewRadius)));
    return TransmittanceToTop(kViewRadius, to_sun.y) * smoothstep(-0.0047, 0.0047, to_sun.y - horizon_cos);
}

// The sky view table's mapping (SkyViewLutParamsToUv and back): rows from straight up to the
// horizon (denser near it) and on to straight down, columns from towards the sun to away.
float2 SkyViewUv(bool towards_ground, float cos_zenith, float cos_light_view, float radius) {
    float horizon_distance = sqrt(radius * radius - kBottomRadius * kBottomRadius);
    float beta = acos(horizon_distance / radius);
    float zenith_horizon_angle = kAtmospherePi - beta;
    float2 uv;
    if (!towards_ground) {
        float coord = 1.0 - sqrt(max(1.0 - acos(cos_zenith) / zenith_horizon_angle, 0.0));
        uv.y = coord * 0.5;
    } else {
        float coord = sqrt(max((acos(cos_zenith) - zenith_horizon_angle) / beta, 0.0));
        uv.y = coord * 0.5 + 0.5;
    }
    uv.x = sqrt(-cos_light_view * 0.5 + 0.5);
    return float2(UnitToSubUv(uv.x, kSkyViewSize.x), UnitToSubUv(uv.y, kSkyViewSize.y));
}

void SkyViewParams(float2 uv, float radius, out float cos_zenith, out float cos_light_view) {
    uv = float2(SubUvToUnit(uv.x, kSkyViewSize.x), SubUvToUnit(uv.y, kSkyViewSize.y));
    float horizon_distance = sqrt(radius * radius - kBottomRadius * kBottomRadius);
    float beta = acos(horizon_distance / radius);
    float zenith_horizon_angle = kAtmospherePi - beta;
    if (uv.y < 0.5) {
        float coord = 1.0 - 2.0 * uv.y;
        coord = 1.0 - coord * coord;
        cos_zenith = cos(zenith_horizon_angle * coord);
    } else {
        float coord = uv.y * 2.0 - 1.0;
        cos_zenith = cos(zenith_horizon_angle + beta * coord * coord);
    }
    float x = uv.x * uv.x;
    cos_light_view = -(x * 2.0 - 1.0);
}

#ifdef ATMOSPHERE_SKY_VIEW_BINDING
// The sky's light in a world direction for a sun of illuminance 1, from the sky view table.
float3 AtmosphereSky(float3 direction, float3 to_sun) {
    float3 position = float3(0.0, kViewRadius, 0.0);
    float cos_zenith = direction.y;
    float3 side = cross(float3(0.0, 1.0, 0.0), direction);
    float side_length = length(side);
    float cos_light_view = 1.0;
    if (side_length > 1e-5) {
        side /= side_length;
        float3 forward = normalize(cross(side, float3(0.0, 1.0, 0.0)));
        float2 light_on_plane = float2(dot(to_sun, forward), dot(to_sun, side));
        float plane_length = length(light_on_plane);
        cos_light_view = plane_length > 1e-5 ? light_on_plane.x / plane_length : 1.0;
    }
    bool towards_ground = RaySphereNearest(position, direction, float3(0.0, 0.0, 0.0), kBottomRadius) >= 0.0;
    float2 uv = SkyViewUv(towards_ground, cos_zenith, cos_light_view, kViewRadius);
    return sky_view_texture.SampleLevel(sky_view_sampler, uv, 0.0).rgb;
}
#endif

#ifdef ATMOSPHERE_MULTIPLE_BINDING
float RayleighPhase(float cos_angle) {
    return 3.0 / (16.0 * kAtmospherePi) * (1.0 + cos_angle * cos_angle);
}

// Cornette-Shanks; cos_angle between the view and the way to the sun.
float MiePhase(float cos_angle) {
    float g = kMiePhaseG;
    float k = 3.0 / (8.0 * kAtmospherePi) * (1.0 - g * g) / (2.0 + g * g);
    return k * (1.0 + cos_angle * cos_angle) / pow(1.0 + g * g - 2.0 * g * cos_angle, 1.5);
}

float3 MultipleScatteringAt(float radius, float cos_sun_zenith) {
    float2 uv = saturate(float2(cos_sun_zenith * 0.5 + 0.5, (radius - kBottomRadius) / (kTopRadius - kBottomRadius)));
    uv = float2(UnitToSubUv(uv.x, kMultipleScatteringSize), UnitToSubUv(uv.y, kMultipleScatteringSize));
    return multiple_scattering_texture.SampleLevel(multiple_scattering_sampler, uv, 0.0).rgb;
}

// Light scattered towards `origin` along `direction` (IntegrateScatteredLuminance with a
// varying number of steps, 4 to 14, spread more densely near the viewer), for a sun of
// illuminance 1 along `to_sun`.
float3 IntegrateSky(float3 origin, float3 direction, float3 to_sun) {
    float bottom = RaySphereNearest(origin, direction, float3(0.0, 0.0, 0.0), kBottomRadius);
    float top = RaySphereNearest(origin, direction, float3(0.0, 0.0, 0.0), kTopRadius);
    float length_through = 0.0;
    if (bottom < 0.0) {
        if (top < 0.0) {
            return float3(0.0, 0.0, 0.0);
        }
        length_through = top;
    } else if (top > 0.0) {
        length_through = min(top, bottom);
    }
    float step_count = lerp(4.0, 14.0, saturate(length_through * 0.01));
    float whole_steps = floor(step_count);
    float whole_length = length_through * whole_steps / step_count;
    float cos_angle = dot(to_sun, direction);
    float mie_phase = MiePhase(cos_angle);
    float rayleigh_phase = RayleighPhase(cos_angle);
    float3 luminance = float3(0.0, 0.0, 0.0);
    float3 throughput = float3(1.0, 1.0, 1.0);
    for (float s = 0.0; s < step_count; s += 1.0) {
        float t0 = s / whole_steps;
        float t1 = (s + 1.0) / whole_steps;
        t0 = whole_length * t0 * t0;
        t1 = t1 > 1.0 ? length_through : whole_length * t1 * t1;
        float t = t0 + (t1 - t0) * 0.3;
        float dt = t1 - t0;
        float3 position = origin + direction * t;
        float radius = length(position);
        float height = radius - kBottomRadius;
        float mie = exp(-height / 1.2);
        float rayleigh = exp(-height / 8.0);
        float ozone = saturate(height < 25.0 ? height / 15.0 - 2.0 / 3.0 : -height / 15.0 + 8.0 / 3.0);
        float3 mie_scattering = mie * kMieScattering;
        float3 rayleigh_scattering = rayleigh * kRayleighScattering;
        float3 scattering = mie_scattering + rayleigh_scattering;
        float3 extinction = mie * kMieExtinction + rayleigh_scattering + ozone * kOzoneAbsorption;
        float3 step_transmittance = exp(-extinction * dt);
        float3 up = position / radius;
        float cos_sun = dot(to_sun, up);
        float3 to_top = TransmittanceToTop(radius, cos_sun);
        float lit = RaySphereNearest(position, to_sun, up * kPlanetRadiusOffset, kBottomRadius) >= 0.0 ? 0.0 : 1.0;
        float3 source = lit * to_top * (mie_scattering * mie_phase + rayleigh_scattering * rayleigh_phase) +
                        MultipleScatteringAt(radius, cos_sun) * scattering;
        luminance += throughput * source * (1.0 - step_transmittance) / extinction;
        throughput *= step_transmittance;
    }
    return luminance;
}
#endif

#endif

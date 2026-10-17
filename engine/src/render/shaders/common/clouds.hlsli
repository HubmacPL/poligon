// Volumetric clouds shared by the cloud pass (clouds.frag.hlsl) and the cloud shadow pass
// (cloud_shadow.frag.hlsl), after Schneider (Horizon Zero Dawn, 2015) and Hillaire
// (Frostbite, 2016): a layer of air between two heights holds clouds whose shape comes from
// a weather map (where clouds are, and what type) and two tileable noise volumes; light is
// lost through them by Beer's law.
//
// A shader that includes this file first binds the noise textures at slots
// CLOUDS_SHAPE_BINDING, CLOUDS_DETAIL_BINDING and CLOUDS_WEATHER_BINDING (each also as
// CLOUDS_..._T and CLOUDS_..._S register names), and has the CloudUniforms below.

[[vk::combinedImageSampler]][[vk::binding(CLOUDS_SHAPE_BINDING, 2)]]
Texture3D<float4> cloud_shape_texture : register(CLOUDS_SHAPE_T, space2);
[[vk::combinedImageSampler]][[vk::binding(CLOUDS_SHAPE_BINDING, 2)]]
SamplerState cloud_shape_sampler : register(CLOUDS_SHAPE_S, space2);
[[vk::combinedImageSampler]][[vk::binding(CLOUDS_DETAIL_BINDING, 2)]]
Texture3D<float4> cloud_detail_texture : register(CLOUDS_DETAIL_T, space2);
[[vk::combinedImageSampler]][[vk::binding(CLOUDS_DETAIL_BINDING, 2)]]
SamplerState cloud_detail_sampler : register(CLOUDS_DETAIL_S, space2);
[[vk::combinedImageSampler]][[vk::binding(CLOUDS_WEATHER_BINDING, 2)]]
Texture2D<float4> cloud_weather_texture : register(CLOUDS_WEATHER_T, space2);
[[vk::combinedImageSampler]][[vk::binding(CLOUDS_WEATHER_BINDING, 2)]]
SamplerState cloud_weather_sampler : register(CLOUDS_WEATHER_S, space2);

cbuffer CloudUniforms : register(b0, space3) {
    float4x4 inverse_view_projection;
    float4 camera_position;  // xyz; w: the frame's noise offset (moves the step pattern each frame)
    float4 light_direction;  // xyz: direction the sunlight travels
    float4 light_color;
    float4 sky_zenith;
    float4 sky_horizon;
    float4 layer;            // x: bottom height, y: top height, z: coverage 0..1, w: density per meter
    float4 shape;            // x: meters across the shape volume, y: meters across the weather map, z: wind offset x, w: wind offset z
    float4 quality;          // x: steps along a view ray, y: steps towards the sun, z: farthest distance walked, w: target width in pixels
    float4 shadow_area;      // xy: world x and z of the cloud shadow image's corner, z: its size in meters, w: target height in pixels
    float4 weather;          // x: how much the weather map varies the coverage (Environment::cloud_variation)
};

float RemapRange(float value, float old_low, float old_high, float new_low, float new_high) {
    return new_low + saturate((value - old_low) / max(old_high - old_low, 0.0001)) * (new_high - new_low);
}

// How dense the cloud is at a point (0 = clear air). `detailed` adds the fine erosion.
float CloudDensity(float3 position, bool detailed) {
    float height = saturate((position.y - layer.x) / max(layer.y - layer.x, 1.0));
    float2 wind = shape.zw;
    float4 weather_sample = cloud_weather_texture.SampleLevel(cloud_weather_sampler, (position.xz + wind * 0.5) / shape.y, 0.0);
    // How much of the sky is cloud here: the coverage setting, more or less by the weather map.
    float coverage = saturate(layer.z + (weather_sample.r - 0.5) * weather.x);
    if (coverage <= 0.01) {
        return 0.0;
    }
    // Their profile by height: flat clouds sit low in the layer, towering ones fill it.
    float type = weather_sample.g;
    float bottom_fade = RemapRange(height, 0.0, 0.08 + 0.1 * (1.0 - type), 0.0, 1.0);
    float top_fade = RemapRange(height, lerp(0.35, 0.95, type), 1.0, 1.0, 0.0);
    float profile = bottom_fade * top_fade;
    float4 noise = cloud_shape_texture.SampleLevel(cloud_shape_sampler, (position + float3(wind.x, 0.0, wind.y)) / shape.x, 0.0);
    float worley = noise.g * 0.625 + noise.b * 0.25 + noise.a * 0.125;
    float base = RemapRange(noise.r, worley - 1.0, 1.0, 0.0, 1.0);
    float density = RemapRange(base * profile, 1.0 - coverage, 1.0, 0.0, 1.0);
    if (detailed && density > 0.0) {
        float3 detail_noise = cloud_detail_texture.SampleLevel(cloud_detail_sampler,
                                                               (position + float3(wind.x, 0.0, wind.y) * 1.5) / (shape.x * 0.18), 0.0).rgb;
        float detail = detail_noise.r * 0.625 + detail_noise.g * 0.25 + detail_noise.b * 0.125;
        // Wispy at the bottom, billowy at the top.
        float erosion = lerp(detail, 1.0 - detail, saturate(height * 4.0));
        density = RemapRange(density, erosion * 0.35, 1.0, 0.0, 1.0);
    }
    return density * layer.w;
}

// Distances along a ray where it enters and leaves the cloud layer (flat Earth); false if it
// never does within the farthest distance.
bool LayerSpan(float3 origin, float3 direction, out float enter, out float leave) {
    enter = 0.0;
    leave = 0.0;
    if (abs(direction.y) < 0.0001) {
        return origin.y > layer.x && origin.y < layer.y;
    }
    float to_bottom = (layer.x - origin.y) / direction.y;
    float to_top = (layer.y - origin.y) / direction.y;
    enter = max(min(to_bottom, to_top), 0.0);
    leave = min(max(to_bottom, to_top), quality.z);
    return leave > enter;
}

// Share of sunlight that gets through the clouds above a point, along the sun's direction.
float CloudTransmittanceTowardsSun(float3 position, int steps) {
    float3 to_sun = -light_direction.xyz;
    if (to_sun.y <= 0.01) {
        return 1.0;
    }
    float enter;
    float leave;
    if (!LayerSpan(position, to_sun, enter, leave)) {
        return 1.0;
    }
    float step_length = (leave - enter) / steps;
    float optical_depth = 0.0;
    for (int i = 0; i < steps; ++i) {
        optical_depth += CloudDensity(position + to_sun * (enter + (i + 0.5) * step_length), false) * step_length;
    }
    return exp(-optical_depth);
}

// Share of sunlight that reaches the ground under the clouds, for the cloud shadow image.
// Unlike CloudTransmittanceTowardsSun (light inside a cloud) it uses the clouds' detailed
// shape, the one the sky shows, so that shadows fall only under the clouds one sees; with
// the base shape alone, a long slanting path through the layer meets some cloud almost
// everywhere. Light scattered forward inside a cloud still reaches the ground, so a thin
// cloud's shadow is not black: transmittance from three scattering orders (Wrenninge,
// "Oz: The Great and Volumetric", 2013, as in Hillaire's Frostbite clouds, 2016).
float CloudShadowTowardsSun(float3 position, int steps) {
    float3 to_sun = -light_direction.xyz;
    if (to_sun.y <= 0.01) {
        return 1.0;
    }
    float enter;
    float leave;
    if (!LayerSpan(position, to_sun, enter, leave)) {
        return 1.0;
    }
    float step_length = (leave - enter) / steps;
    float optical_depth = 0.0;
    for (int i = 0; i < steps; ++i) {
        optical_depth += CloudDensity(position + to_sun * (enter + (i + 0.5) * step_length), true) * step_length;
    }
    return (exp(-optical_depth) + 0.5 * exp(-0.5 * optical_depth) + 0.25 * exp(-0.25 * optical_depth)) / 1.75;
}

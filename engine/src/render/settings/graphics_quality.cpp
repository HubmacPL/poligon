#include "poligon/render/graphics_quality.h"

#include <cstring>

namespace poligon {

GraphicsQuality GetGraphicsPreset(GraphicsPreset preset) {
    GraphicsQuality quality;
    quality.preset = preset;
    switch (preset) {
    case GraphicsPreset::Low:
        quality.render_scale = 0.67f;
        quality.shadow_map_size = 1024;
        quality.far_shadow_scale = 0.5f;
        quality.msaa_samples = 1;
        quality.ambient_occlusion = false;
        quality.volumetric_fog = false;
        quality.shadow_distance_scale = 0.6f;
        quality.ambient_occlusion_samples = 8;
        quality.fog_steps = 16;
        quality.water_reflection_steps = 0;
        quality.cloud_steps = 20;
        quality.cloud_light_steps = 3;
        quality.vegetation_detail = 0.6f;
        quality.vegetation_shadow = 0.5f;
        quality.ground_cover_density = 0.35f;
        quality.ground_cover_radius = 0.6f;
        quality.terrain_detail = 0.6f;
        break;
    case GraphicsPreset::Medium:
        quality.render_scale = 0.8f;
        quality.shadow_map_size = 1024;
        quality.far_shadow_scale = 1.0f;
        quality.msaa_samples = 1;
        quality.shadow_distance_scale = 0.8f;
        quality.ambient_occlusion_samples = 12;
        quality.fog_steps = 20;
        quality.water_reflection_steps = 12;
        quality.cloud_steps = 32;
        quality.cloud_light_steps = 4;
        quality.vegetation_detail = 0.8f;
        quality.vegetation_shadow = 0.75f;
        quality.ground_cover_density = 0.65f;
        quality.ground_cover_radius = 0.8f;
        quality.terrain_detail = 0.8f;
        break;
    case GraphicsPreset::High:
        break;
    case GraphicsPreset::Ultra:
        quality.render_scale = 1.0f;
        quality.shadow_map_size = 4096;
        quality.shadow_distance_scale = 1.5f;
        quality.ambient_occlusion_samples = 32;
        quality.fog_steps = 48;
        quality.water_reflection_steps = 48;
        quality.cloud_steps = 96;
        quality.cloud_light_steps = 6;
        quality.cloud_resolution = 0.5f;
        quality.vegetation_detail = 1.5f;
        quality.vegetation_shadow = 1.5f;
        quality.ground_cover_density = 1.5f;
        quality.ground_cover_radius = 1.3f;
        quality.terrain_detail = 1.5f;
        break;
    }
    return quality;
}

const char* GetGraphicsPresetName(GraphicsPreset preset) {
    switch (preset) {
    case GraphicsPreset::Low:
        return "low";
    case GraphicsPreset::Medium:
        return "medium";
    case GraphicsPreset::High:
        return "high";
    case GraphicsPreset::Ultra:
        return "ultra";
    }
    return "high";
}

bool ParseGraphicsPreset(const char* name, GraphicsPreset& preset) {
    for (const GraphicsPreset candidate :
         {GraphicsPreset::Low, GraphicsPreset::Medium, GraphicsPreset::High, GraphicsPreset::Ultra}) {
        if (std::strcmp(name, GetGraphicsPresetName(candidate)) == 0) {
            preset = candidate;
            return true;
        }
    }
    return false;
}

void ApplyGraphicsQuality(const GraphicsQuality& quality, Environment& environment, PostSettings& post) {
    environment.shadows = environment.shadows && quality.shadows;
    environment.ambient_occlusion = environment.ambient_occlusion && quality.ambient_occlusion;
    environment.volumetric_fog = environment.volumetric_fog && quality.volumetric_fog;
    environment.shadow_distance *= quality.shadow_distance_scale;
    environment.ambient_occlusion_samples = quality.ambient_occlusion_samples;
    environment.fog_steps = quality.fog_steps;
    environment.water_reflection_steps = quality.water_reflection_steps;
    environment.clouds = environment.clouds && quality.clouds;
    environment.cloud_steps = quality.cloud_steps;
    environment.cloud_light_steps = quality.cloud_light_steps;
    post.bloom = post.bloom && quality.bloom;
    post.temporal_aa = post.temporal_aa && quality.temporal_aa;
}

void ApplyGraphicsQuality(const GraphicsQuality& quality, RendererConfig& config) {
    config.shadow_map_size = quality.shadow_map_size;
    config.far_shadow_scale = quality.far_shadow_scale;
    config.cloud_resolution = quality.cloud_resolution;
    config.msaa_samples = quality.temporal_aa ? 1 : quality.msaa_samples;
    config.render_scale = quality.temporal_aa ? quality.render_scale : 1.0f;
}

} // namespace poligon

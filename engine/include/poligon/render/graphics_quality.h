#pragma once

#include "poligon/render/renderer.h"

namespace poligon {

// Graphics quality: how much work the picture may cost, separate from how the scene looks.
// A scene sets its look (fog color, shadow range, how far plants are drawn); the quality
// switches costly effects off or scales them down. Four ready presets (low, medium, high,
// ultra) can be picked in the render settings file ("quality.preset medium"), on the
// command line (--quality medium) or in the settings window, and every value can then be
// changed on its own.
//
// Some values apply to the renderer when it is created (shadow image size, multisampling);
// the switches and step counts apply to every frame (ApplyGraphicsQuality); the scales are
// for the game's systems to use (Vegetation::SetDistanceScale, GroundCover, Terrain).

enum class GraphicsPreset { Low, Medium, High, Ultra };

struct GraphicsQuality {
    // The preset these values started from (written to the file, so that a file saved
    // after small changes still says which preset they were made to).
    GraphicsPreset preset = GraphicsPreset::High;

    // --- When the renderer is created ---
    // Pixels on each side of each of the four shadow images, and the share of that the two
    // far cascades use (RendererConfig::far_shadow_scale).
    int shadow_map_size = 1536;
    float far_shadow_scale = 0.5f;
    // Multisampling, used only when temporal anti-aliasing is off.
    int msaa_samples = 4;
    // Share of the output's width and height the scene is drawn at, brought back to full
    // size by temporal anti-aliasing (RendererConfig::render_scale); 1 with TAA off.
    float render_scale = 0.9f;

    // --- Every frame (ApplyGraphicsQuality) ---
    // Effects the scene asked for are drawn only when these allow them.
    bool shadows = true;
    bool ambient_occlusion = true;
    bool volumetric_fog = true;
    bool bloom = true;
    bool temporal_aa = true;
    // Multiplies the scene's shadow range.
    float shadow_distance_scale = 1.0f;
    // Samples per pixel of ambient occlusion, steps per pixel of volumetric fog.
    int ambient_occlusion_samples = 20;
    int fog_steps = 32;
    // Steps of each ray water reflects through the picture; 0 = water mirrors only the sky.
    int water_reflection_steps = 24;
    // Clouds: drawn at all, steps along a line of sight and towards the sun, and the share of
    // the drawn size their image has (RendererConfig::cloud_resolution).
    bool clouds = true;
    int cloud_steps = 48;
    int cloud_light_steps = 5;
    float cloud_resolution = 0.25f;

    // --- For the game's systems ---
    // Multiply how far plants keep each level of detail, and how far they cast shadows.
    float vegetation_detail = 1.0f;
    float vegetation_shadow = 1.0f;
    // Multiply the density of ground cover and the radius it grows in around the camera.
    float ground_cover_density = 1.0f;
    float ground_cover_radius = 1.0f;
    // Multiplies the terrain's detail (TerrainConfig::detail).
    float terrain_detail = 1.0f;
};

// The values of a preset. Medium is meant to hold 60 frames per second on a GTX 980 at
// 1920 x 1080; high comes close; ultra has no limit but runs on that card.
GraphicsQuality GetGraphicsPreset(GraphicsPreset preset);
// "low", "medium", "high", "ultra".
const char* GetGraphicsPresetName(GraphicsPreset preset);
// Returns false for an unknown name.
bool ParseGraphicsPreset(const char* name, GraphicsPreset& preset);

// Limits a frame's environment and post-processing to what the quality allows: switches
// are combined with "and", the shadow range is scaled, step counts are set.
void ApplyGraphicsQuality(const GraphicsQuality& quality, Environment& environment, PostSettings& post);
// Shadow image size and multisampling for a renderer about to be created.
void ApplyGraphicsQuality(const GraphicsQuality& quality, RendererConfig& config);

} // namespace poligon

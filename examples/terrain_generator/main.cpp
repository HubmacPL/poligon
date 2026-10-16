// Example: land from the terrain generator, with ground layers chosen by its maps. A small
// piece (1 x 1 km) is generated, four plain-colored layers (grass, earth, sand, stones) are
// given rules (where it is wet, steep, by the water...), and the land is drawn from above a
// lake. Real games use texture images for the layers and keep the generated land in a file
// (LoadOrGenerateTerrain).
// Usage: example_terrain_generator OUTPUT.png
#include <cstdio>

#include "poligon/render/renderer.h"
#include "poligon/world/terrain.h"
#include "poligon/world/terrain_generator.h"

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: example_terrain_generator OUTPUT.png\n");
        return 2;
    }
    // A small, quick land; the defaults make 6 x 6 km and take about two minutes.
    poligon::TerrainGeneratorSettings settings;
    settings.seed = 3;
    settings.size = 1024.0f;
    settings.relief = 40.0f;
    settings.initial_relief = 25.0f;
    settings.initial_hill_size = 500.0f;
    settings.erosion_steps = 60;
    settings.basin_count = 1;
    settings.basin_radius = 180.0f;
    settings.basin_sinking = 0.8f;
    settings.droplets_per_square_meter = 0.05f;
    poligon::TerrainMaps maps;
    poligon::GenerateTerrain(settings, maps);
    std::printf("generated %d x %d points, %d lakes\n", maps.height.width, maps.height.depth,
                static_cast<int>(maps.lakes.size()));

    poligon::RendererConfig config;
    config.width = 640;
    config.height = 360;
    config.headless = true;
    config.post.exposure = 0.6f;
    poligon::Renderer renderer(config);

    // Four layers. Rules say where each one covers the land; the strongest wins.
    const poligon::Image colors[4] = {poligon::MakeSolidImage(4, 4, 90, 120, 50),    // grass
                                      poligon::MakeSolidImage(4, 4, 100, 75, 50),    // wet earth
                                      poligon::MakeSolidImage(4, 4, 190, 175, 130),  // sand
                                      poligon::MakeSolidImage(4, 4, 110, 110, 105)}; // stones
    const poligon::Image normal = poligon::MakeSolidImage(4, 4, 128, 128, 255);
    const poligon::Image normals[4] = {normal, normal, normal, normal};
    poligon::TerrainLayerRule rules[4];
    rules[0].strength = 0.3f;                            // grass wherever nothing else fits
    rules[1].conditions.moisture = {0.8f, 1.0f, 0.1f};              // wet ground
    rules[2].conditions.water_distance = {0.0f, 3.0f, 2.0f};        // the shore
    rules[2].conditions.water_depth = {0.0f, 0.5f, 0.3f};
    rules[2].strength = 1.5f;
    rules[3].conditions.water_depth = {0.5f, 1000.0f, 0.3f};        // the lake bed
    rules[3].strength = 2.0f;
    poligon::Image weights[2];
    poligon::MakeTerrainLayerWeights(maps, rules, 4, 1u, weights);

    poligon::TextureOptions data;
    data.is_color = false;
    poligon::TerrainConfig terrain_config;
    terrain_config.origin = {-512.0f, 0.0f, -512.0f};  // the land's middle at the world's origin
    terrain_config.material.colors = renderer.CreateTextureArray(colors, 4);
    terrain_config.material.normals = renderer.CreateTextureArray(normals, 4, data);
    terrain_config.material.layer_count = 4;
    data.generate_mipmaps = false;
    terrain_config.material.weights[0] = renderer.CreateTexture(weights[0], data);
    terrain_config.material.weights[1] = renderer.CreateTexture(weights[1], data);
    terrain_config.material.steep_start_degrees = 90.0f;
    poligon::Terrain terrain(renderer, maps.height, terrain_config);

    // Look at the largest lake from 150 m up, from its south.
    glm::vec3 target(0.0f, 0.0f, 0.0f);
    if (!maps.lakes.empty()) {
        target = {maps.lakes[0].center.x - 512.0f, maps.lakes[0].level, maps.lakes[0].center.y - 512.0f};
    }
    poligon::Camera camera;
    camera.position = target + glm::vec3(0.0f, 150.0f, 250.0f);
    camera.target = target;
    camera.far_plane = 3000.0f;
    poligon::DirectionalLight sun;
    sun.direction = {-0.5f, -0.7f, -0.3f};
    sun.color = {2.4f, 2.3f, 2.1f, 1.0f};
    poligon::Environment environment;
    environment.draw_sky = true;
    environment.sky_light = true;
    environment.shadow_distance = 600.0f;
    renderer.BeginFrame(camera, sun, environment);
    terrain.Draw(camera);
    renderer.EndFrame();

    if (!renderer.SaveFrameToPng(argv[1])) {
        std::fprintf(stderr, "cannot write %s\n", argv[1]);
        return 1;
    }
    std::printf("saved %s\n", argv[1]);
    return 0;
}

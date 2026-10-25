// Example: ground cover. Grass tufts and pebbles are not placed by hand or stored for the
// whole map: cells of land near the camera get their plants computed from rules (slope,
// patchiness) and the cell's position, and are dropped when the camera moves away.
//
// The "grass" is two crossed green cards, the "pebble" a flattened cube.
// Usage: example_ground_cover OUTPUT.png
#include <cstdio>

#include "poligon/world/ground_cover.h"
#include "poligon/world/height_map.h"
#include "poligon/render/renderer.h"
#include "poligon/world/terrain.h"

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: example_ground_cover OUTPUT.png\n");
        return 2;
    }
    const glm::mat4 identity(1.0f);

    poligon::RendererConfig config;
    config.width = 640;
    config.height = 360;
    config.headless = true;
    config.post.exposure = 0.5f;
    poligon::Renderer renderer(config);

    // Gentle hills to grow on.
    const poligon::HeightMap map = poligon::MakeNoiseHeightMap(101, 101, 4.0f, 11u, 60.0f, 6.0f);
    poligon::TerrainConfig terrain_config;
    terrain_config.origin = {-200.0f, 0.0f, -200.0f};
    terrain_config.material.tile_sizes[0] = 4.0f;
    poligon::Terrain terrain(renderer, map, terrain_config);

    // A grass tuft: two crossed upright cards, 0.5 m tall, foot at the origin.
    const poligon::MeshVertex card_vertices[8] = {
        {{-0.25f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}}, {{0.25f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},
        {{0.25f, 0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},  {{-0.25f, 0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
        {{0.0f, 0.0f, -0.25f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f}}, {{0.0f, 0.0f, 0.25f}, {1.0f, 0.0f, 0.0f}, {1.0f, 1.0f}},
        {{0.0f, 0.5f, 0.25f}, {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}},  {{0.0f, 0.5f, -0.25f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
    };
    const std::uint32_t card_indices[12] = {0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7};
    const poligon::MeshHandle tuft = renderer.CreateMesh(card_vertices, 8, card_indices, 12);
    const poligon::MeshHandle cube = renderer.CreateCubeMesh();

    poligon::Material grass;
    grass.color = {0.30f, 0.55f, 0.20f, 1.0f};
    grass.double_sided = true;
    grass.wind_bend = 6.0f;
    grass.wind_flutter = 1.5f;
    poligon::Material stone;
    stone.color = {0.45f, 0.43f, 0.40f, 1.0f};

    // Grass: dense, in soft patches, tilted with the ground, gone beyond 60 m.
    poligon::GroundCoverLayer grass_layer;
    grass_layer.species.levels.resize(1);
    grass_layer.species.levels[0].parts.push_back({tuft, grass, identity});
    grass_layer.species.levels[0].max_distance = 60.0f;
    grass_layer.species.shadow_distance = 15.0f;
    grass_layer.density = 3.0f;
    grass_layer.patch_threshold = 0.35f;
    grass_layer.patch_softness = 0.3f;
    grass_layer.align_to_ground = true;
    // Pebbles: sparse, anywhere, half sunk into the ground.
    poligon::GroundCoverLayer pebble_layer;
    pebble_layer.species.levels.resize(1);
    pebble_layer.species.levels[0].parts.push_back({cube, stone, glm::scale(identity, {0.3f, 0.15f, 0.25f})});
    pebble_layer.species.levels[0].max_distance = 40.0f;
    pebble_layer.species.shadow_distance = 15.0f;
    pebble_layer.density = 0.05f;
    pebble_layer.scale_min = 0.5f;
    pebble_layer.scale_max = 1.5f;
    pebble_layer.sink_meters = 0.04f;
    pebble_layer.seed = 1;

    poligon::GroundCover cover(terrain, 80.0f);  // plants exist within 80 m of the camera
    cover.AddLayer(grass_layer);
    cover.AddLayer(pebble_layer);

    poligon::Camera camera;
    camera.position = {0.0f, terrain.GetHeight(0.0f, 0.0f) + 1.7f, 0.0f};
    camera.target = camera.position + glm::dvec3(0.0, -0.15, -1.0);
    poligon::DirectionalLight sun;
    sun.direction = {-0.5f, -1.0f, -0.4f};
    sun.color = {2.6f, 2.45f, 2.2f, 1.0f};
    poligon::Environment environment;
    environment.draw_sky = true;
    environment.sky_light = true;
    environment.shadow_distance = 30.0f;
    environment.wind_strength = 1.0f;
    environment.wind_time = 1.0f;

    // Every frame: Update builds and drops cells as the camera moves, Draw draws what exists.
    cover.Update(camera.position);
    renderer.BeginFrame(camera, sun, environment);
    terrain.Draw(camera);
    cover.Draw(renderer, camera);
    renderer.EndFrame();

    const poligon::GroundCoverStats& stats = cover.GetStats();
    std::printf("%d cells alive, %d plants and pebbles, %d instances in view\n", stats.cells_alive, stats.instances_alive,
                renderer.GetFrameStats().instances_drawn);
    if (!renderer.SaveFrameToPng(argv[1])) {
        std::fprintf(stderr, "cannot write %s\n", argv[1]);
        return 1;
    }
    std::printf("saved %s\n", argv[1]);
    return 0;
}

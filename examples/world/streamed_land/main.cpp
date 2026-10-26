// Example: a streamed world. Generated land is cut into 128 m patches (poligon/world/land.h);
// a WorldStreamer loads, around the camera, the terrain, its collisions, the lakes, plants
// placed by a rule and sky occlusion, each as its own source with its own window. The camera
// then moves half a kilometer away and the windows follow it.
// Usage: example_streamed_land OUTPUT.png
#include <cstdio>

#include "poligon/physics/physics.h"
#include "poligon/render/renderer.h"
#include "poligon/world/land.h"
#include "poligon/world/plant_rules.h"
#include "poligon/world/plants.h"
#include "poligon/world/sky_occlusion.h"
#include "poligon/world/streaming.h"
#include "poligon/world/terrain.h"
#include "poligon/world/water.h"

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: example_streamed_land OUTPUT.png\n");
        return 2;
    }
    // 1 km of land, generated whole (a game reads it cooked, patch by patch).
    poligon::TerrainGeneratorSettings generator;
    generator.seed = 4;
    generator.size = 1024.0f;
    generator.erosion_steps = 40;
    generator.initial_hill_size = 400.0f;
    generator.relief = 40.0f;
    generator.basin_count = 2;
    generator.droplets_per_square_meter = 0.03f;
    poligon::TerrainMaps maps;
    poligon::GenerateTerrain(generator, maps);
    poligon::TerrainLayerRule layers[2];
    layers[1].conditions.slope_degrees = {14.0f, 90.0f, 4.0f};
    const glm::dvec3 origin(-512.0, 0.0, -512.0);  // the land's middle at the world's origin
    poligon::LandFromMaps land(maps, 128.0, origin, layers, 2, 1u);

    poligon::RendererConfig config;
    config.width = 640;
    config.height = 360;
    config.headless = true;
    poligon::Renderer renderer(config);
    const poligon::Image colors[2] = {poligon::MakeSolidImage(4, 4, 80, 120, 50), poligon::MakeSolidImage(4, 4, 130, 110, 85)};
    poligon::TerrainConfig terrain_config;
    terrain_config.material.colors = renderer.CreateTextureArray(colors, 2);
    terrain_config.material.layer_count = 2;
    poligon::Terrain terrain(renderer, land.GetInfo(), terrain_config, 64);  // pages: the 7 x 7 window and a ring kept beyond it

    // Plants: one kind (a dark box with a trunk), placed by a rule from each patch's maps.
    const poligon::MeshHandle cube = renderer.CreateCubeMesh();
    poligon::Material leaves;
    leaves.color = {0.1f, 0.3f, 0.1f, 1.0f};
    poligon::Vegetation vegetation;
    vegetation.SetCellOrigin({origin.x, origin.z});  // cells must fit the patches
    poligon::PlantKind tree;
    tree.settings.collider = poligon::PlantCollider::Trunk;
    poligon::PlantVariant variant;
    variant.species.levels.resize(1);
    variant.species.levels[0].parts.push_back({cube, leaves, glm::translate(glm::mat4(1.0f), {0.0f, 4.0f, 0.0f}) *
                                                                  glm::scale(glm::mat4(1.0f), {3.0f, 8.0f, 3.0f})});
    variant.species.levels[0].max_distance = 600.0f;
    variant.species.shadow_distance = 100.0f;
    variant.height = 8.0f;
    variant.half_width = {1.5f, 1.5f};
    variant.species_id = vegetation.AddSpecies(variant.species);
    tree.variants.push_back(variant);
    poligon::PlantRule rule;
    rule.spacing = 7.0f;
    rule.density = 0.012f;
    rule.conditions.slope_degrees = {0.0f, 20.0f, 4.0f};
    rule.conditions.water_distance = {4.0f, 1e9f, 2.0f};
    // Called on the loading thread too: it only reads.
    const poligon::Planter planter = [&](const poligon::LandPatch& patch, glm::vec2 low, glm::vec2 high,
                                         std::vector<poligon::PlacedPlant>& plants) {
        for (const auto& [index, placement] : poligon::PlacePlantGroupInRectangle(patch.maps, {&rule}, glm::vec2(512.0f), 2000.0f, low,
                                                                                   high, {1})) {
            poligon::PlacedPlant plant;
            plant.kind = &tree;
            plant.foot = origin + glm::dvec3(placement.position.x, placement.height, placement.position.y);
            plant.turn = placement.turn;
            plant.scale = placement.scale;
            plants.push_back(plant);
        }
    };

    // The sources and their windows: what is drawn follows the camera in the background,
    // the collisions follow the player before each tick.
    poligon::PhysicsWorld physics;
    poligon::TerrainPatchSource terrain_source(terrain, land);
    poligon::TerrainPhysicsSource terrain_collisions(physics, land);
    poligon::WaterPatchSource water(renderer, land);
    poligon::PlantPatchSource plants(vegetation, land, planter);
    poligon::PlantColliderSource trunks(physics, land, planter);
    poligon::SkyOcclusionPatchSource sky(land, 3, 4.0f, 120.0f);
    sky.SetCanopy(planter);
    poligon::StreamingGridSettings grid_settings = land.GetInfo().grid;
    grid_settings.origin = {origin.x, origin.z};
    const poligon::StreamingGrid grid(grid_settings);
    poligon::WorldStreamer streamer(grid);
    streamer.AddSource(terrain_source, 3, poligon::StreamingMode::Background);
    streamer.AddSource(water, 3, poligon::StreamingMode::Background);
    streamer.AddSource(plants, 2, poligon::StreamingMode::Background);
    streamer.AddSource(sky, 2, poligon::StreamingMode::Background);
    streamer.AddSource(terrain_collisions, 1, poligon::StreamingMode::Immediate);
    streamer.AddSource(trunks, 1, poligon::StreamingMode::Immediate);
    renderer.SetSkyOcclusion(&sky);

    poligon::DirectionalLight sun;
    sun.direction = {-0.4f, -0.6f, -0.5f};
    sun.color = {2.4f, 2.3f, 2.1f, 1.0f};
    poligon::Environment environment;
    environment.draw_sky = true;
    environment.sky_light = true;
    environment.shadow_distance = 150.0f;
    poligon::Camera camera;
    for (const double x : {-300.0, 200.0}) {
        // The player stands here: collisions first (deterministic), then the drawn windows.
        const glm::dvec3 player(x, terrain.GetHeight(x, 0.0), 0.0);
        streamer.RequirePatches({player});
        streamer.Update(player);
        streamer.FinishLoading();  // a game would draw on while the rest arrives
        camera.position = glm::dvec3(x, terrain.GetHeight(x, 0.0) + 25.0, 0.0);
        camera.target = camera.position + glm::dvec3(150.0, -30.0, -120.0);
        camera.far_plane = 3000.0f;
        std::printf("camera at x = %.0f: %d terrain patches, %d plants, %d lakes' surfaces, %d bodies\n", x, terrain.GetPatchCount(),
                    vegetation.GetInstanceCount(), water.GetSurfaceCount(), terrain_collisions.GetBodyCount() + trunks.GetBodyCount());
    }
    renderer.BeginFrame(camera, sun, environment);
    terrain.Draw(camera);
    water.Draw(renderer, poligon::WaterMaterial{});
    vegetation.Draw(renderer, camera);
    renderer.EndFrame();
    if (!renderer.SaveFrameToPng(argv[1])) {
        std::fprintf(stderr, "cannot write %s\n", argv[1]);
        return 1;
    }
    std::printf("saved %s\n", argv[1]);
    return 0;
}

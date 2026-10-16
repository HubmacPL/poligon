// Example: plants placed by rules that read the terrain generator's maps, water plants made
// by code, grass made by code, and a path trampled through tall plants. A small land with a
// pond is generated; rules put reeds in its shallows, water lilies where it is deeper, a
// field of cattails along one side, through which a path is walked flat, and short grass
// on dry ground around the camera.
// Usage: example_plant_rules OUTPUT.png
#include <cstdio>

#include "poligon/world/plant_rules.h"
#include "poligon/world/plants.h"
#include "poligon/world/procedural_plants.h"
#include "poligon/render/renderer.h"
#include "poligon/world/terrain.h"
#include "poligon/world/trample.h"
#include "poligon/world/water.h"

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: example_plant_rules OUTPUT.png\n");
        return 2;
    }
    poligon::TerrainGeneratorSettings land;
    land.seed = 3;
    land.size = 512.0f;
    land.relief = 25.0f;
    land.initial_relief = 15.0f;
    land.initial_hill_size = 300.0f;
    land.erosion_steps = 40;
    land.basin_count = 1;
    land.basin_radius = 110.0f;
    land.basin_sinking = 0.8f;
    land.droplets_per_square_meter = 0.03f;
    poligon::TerrainMaps maps;
    poligon::GenerateTerrain(land, maps);
    const glm::vec2 pond = maps.lakes.empty() ? glm::vec2(256.0f) : maps.lakes[0].center;

    poligon::RendererConfig config;
    config.width = 640;
    config.height = 360;
    config.headless = true;
    config.post.exposure = 0.6f;
    poligon::Renderer renderer(config);
    poligon::TerrainConfig terrain_config;
    terrain_config.material.steep_color = {0.4f, 0.33f, 0.22f, 1.0f};
    poligon::Terrain terrain(renderer, maps.height, terrain_config);
    const std::vector<poligon::WaterSurface> water = poligon::CreateLakeSurfaces(renderer, maps, terrain_config.origin);

    // Kinds made by code; each model holds three clumps, which become three variants.
    poligon::Vegetation vegetation;
    poligon::PlantSettings settings;
    settings.split_variants = true;
    settings.level_distances = {200.0f};
    settings.wind_bend = 3.0f;  // bends in the wind and lies down where trampled
    const poligon::LoadedModel reed_model = poligon::UploadLoadedModel(renderer, poligon::MakeReedModel(1u));
    const poligon::LoadedModel cattail_model = poligon::UploadLoadedModel(renderer, poligon::MakeCattailModel(2u));
    const poligon::LoadedModel lily_model = poligon::UploadLoadedModel(renderer, poligon::MakeWaterLilyModel(3u));
    const poligon::PlantKind reeds = poligon::MakePlantKind(reed_model, settings, vegetation);
    const poligon::PlantKind cattails = poligon::MakePlantKind(cattail_model, settings, vegetation);
    // Grass: three tufts of real blades, 0.15 to 0.35 m (see MakeGrassModel).
    const poligon::LoadedModel grass_model = poligon::UploadLoadedModel(renderer, poligon::MakeGrassModel(4u, 3, 0.15f, 0.35f, 10, 16));
    settings.level_distances = {40.0f};
    const poligon::PlantKind grass = poligon::MakePlantKind(grass_model, settings, vegetation);
    settings.level_distances = {200.0f};
    settings.wind_bend = 0.0f;
    const poligon::PlantKind lilies = poligon::MakePlantKind(lily_model, settings, vegetation);

    // Rules, as a level file would hold them.
    std::vector<poligon::PlantRule> rules;
    std::string error;
    const std::vector<std::pair<int, std::string>> lines = {
        {1, "plant reeds"},         {2, "spacing 1.5"}, {3, "density 0.4"},  {4, "water_depth 0 0.4 0.15"},
        {5, "water_distance 0 2 1"}, {6, "plant lilies"}, {7, "spacing 2"},    {8, "density 0.2"},
        {9, "water_depth 0.6 3 0.3"}, {10, "on_water 1"}, {11, "plant cattails"}, {12, "spacing 1.2"},
        {13, "density 0.6"},         {14, "water_depth 0 0 0.01"}, {15, "water_distance 2 9 1"},
        {16, "plant grass"},         {17, "spacing 0.35"}, {18, "density 0.8"}, {19, "water_depth 0 0 0.01"},
        {20, "water_distance 9 1e9 1"}};
    if (!poligon::ParsePlantRules(lines, rules, error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    // Looking at the pond from dry ground 20 m back from its shore, along a line from its middle.
    glm::vec2 stand = pond;
    for (float distance = 0.0f; distance < 250.0f; distance += 1.0f) {
        stand = pond + glm::vec2(0.7071f, 0.7071f) * distance;
        if (maps.GetWaterDepth(stand.x, stand.y) == 0.0f && maps.Sample(maps.water_distance, stand.x, stand.y) > 20.0f) {
            break;
        }
    }
    // The water plants around the pond, the grass only within 25 m of the camera.
    const poligon::PlantKind* kinds[4] = {&reeds, &lilies, &cattails, &grass};
    const glm::vec2 centers[4] = {pond, pond, pond, stand};
    const float radii[4] = {150.0f, 150.0f, 150.0f, 25.0f};
    for (size_t r = 0; r < rules.size(); ++r) {
        const poligon::PlantKind& kind = *kinds[r];
        for (const poligon::PlantPlacement& placement :
             poligon::PlacePlants(maps, rules[r], centers[r], radii[r], static_cast<int>(kind.variants.size()))) {
            vegetation.AddInstance(kind.variants[static_cast<size_t>(placement.variant)].species_id,
                                   glm::translate(glm::mat4(1.0f), {placement.position.x, placement.height, placement.position.y}) *
                                       glm::rotate(glm::mat4(1.0f), placement.turn, {0.0f, 1.0f, 0.0f}) *
                                       glm::scale(glm::mat4(1.0f), glm::vec3(placement.scale)));
        }
    }
    std::printf("%d plants placed by %zu rules\n", vegetation.GetInstanceCount(), rules.size());

    // A path walked flat from the camera towards the pond.
    poligon::Camera camera;
    camera.position = glm::vec3(stand.x, maps.height.GetHeight(stand.x, stand.y) + 2.5f, stand.y);
    camera.target = glm::vec3(pond.x, maps.height.GetHeight(pond.x, pond.y), pond.y);
    camera.far_plane = 2000.0f;
    poligon::TrampleMap trample(128.0f, 256);
    trample.Recenter(pond);
    for (int step = 0; step <= 60; ++step) {
        const float t = static_cast<float>(step) / 60.0f;
        trample.Trample(glm::mix(glm::vec2(camera.position.x, camera.position.z), pond, t), 0.7f, 1.0f);
    }
    renderer.SetTrampleMap(&trample);

    poligon::DirectionalLight sun;
    sun.direction = {-0.4f, -0.6f, -0.5f};
    sun.color = {2.6f, 2.45f, 2.2f, 1.0f};
    poligon::Environment environment;
    environment.draw_sky = true;
    environment.sky_light = true;
    environment.shadow_distance = 120.0f;
    renderer.BeginFrame(camera, sun, environment);
    terrain.Draw(camera);
    for (const poligon::WaterSurface& surface : water) {
        renderer.DrawWater(surface.mesh, surface.transform, poligon::WaterMaterial{});
    }
    vegetation.Draw(renderer, camera);
    renderer.EndFrame();
    if (!renderer.SaveFrameToPng(argv[1])) {
        std::fprintf(stderr, "cannot write %s\n", argv[1]);
        return 1;
    }
    std::printf("saved %s\n", argv[1]);
    return 0;
}

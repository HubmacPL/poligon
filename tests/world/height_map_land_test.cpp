// Checks land from a height map made elsewhere (LandFromHeightMap, poligon/world/land.h),
// without the terrain generator: 2 x 2 patches of 128 m with ground given per patch as
// layer indices are cooked into files, read back equal to the bit, and streamed around the
// camera. The streamed land has the map's heights, each patch shows its own layer, physics,
// sky occlusion and the far background work from the heights alone, and plant rules on maps
// the land does not have (moisture, water) plant nothing.
// Usage: height_map_land_test d3d12|vulkan OUTPUT_DIRECTORY
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

#include "poligon/assets/assets.h"
#include "poligon/physics/physics.h"
#include "poligon/render/renderer.h"
#include "poligon/world/land.h"
#include "poligon/world/plant_rules.h"
#include "poligon/world/sky_occlusion.h"
#include "poligon/world/streaming.h"
#include "poligon/world/terrain.h"
#include "test_support.h"

namespace {

constexpr int kSize = 256;

// Each patch's ground: layer (x + 2 z) everywhere, in the first channel; the other channels
// unused (weight 0) but naming layer 3, which must not show.
class PatchLayers : public poligon::PatchGroundSource {
public:
    void GetGround(poligon::PatchCoord patch, poligon::LandPatchGround& ground) const override {
        const int points = 65;  // 128 m / 2 m + 1
        ground.layer_indices = poligon::MakeSolidImage(points, points, static_cast<std::uint8_t>(patch.x + 2 * patch.z), 3, 3, 3);
        ground.layer_weights = poligon::MakeSolidImage(points, points, 255, 0, 0, 0);
    }
};

bool SamePatch(const poligon::LandPatch& a, const poligon::LandPatch& b) {
    return a.patch == b.patch && a.maps.height.heights == b.maps.height.heights && a.maps.height.first_x == b.maps.height.first_x &&
           a.maps.map_width == b.maps.map_width && a.maps.map_first_z == b.maps.map_first_z && a.maps.moisture.empty() &&
           b.maps.moisture.empty() && a.layer_indices.pixels == b.layer_indices.pixels &&
           a.layer_weights.pixels == b.layer_weights.pixels && a.weights[0].pixels.empty() && b.weights[0].pixels.empty();
}

} // namespace

int main(int argc, char** argv) {
    using test::Expect;

    poligon::GraphicsBackend backend = poligon::GraphicsBackend::Default;
    if (argc == 3 && std::strcmp(argv[1], "d3d12") == 0) {
        backend = poligon::GraphicsBackend::Direct3D12;
    } else if (argc == 3 && std::strcmp(argv[1], "vulkan") == 0) {
        backend = poligon::GraphicsBackend::Vulkan;
    } else {
        std::printf("usage: height_map_land_test d3d12|vulkan OUTPUT_DIRECTORY\n");
        return 2;
    }
    const std::filesystem::path cooked = std::filesystem::path(argv[2]) / (std::string("height_map_land_") + argv[1]);
    std::error_code ec;
    std::filesystem::remove_all(cooked, ec);
    std::filesystem::create_directories(cooked, ec);

    // 256 x 256 m, a height every 2 m: a gentle slope rising east and a hill in the middle.
    poligon::HeightMap map;
    map.width = 129;
    map.depth = 129;
    map.spacing = 2.0f;
    for (int z = 0; z < map.depth; ++z) {
        for (int x = 0; x < map.width; ++x) {
            const float dx = static_cast<float>(x - 64) * 2.0f;
            const float dz = static_cast<float>(z - 64) * 2.0f;
            map.heights.push_back(0.05f * static_cast<float>(x) * 2.0f + 12.0f * std::exp(-(dx * dx + dz * dz) / (2.0f * 40.0f * 40.0f)));
        }
    }
    const glm::dvec3 origin(-128.0, 20.0, -128.0);
    const PatchLayers layers;
    const poligon::LandFromHeightMap land(map, 128.0, origin, &layers, 2.0f, 16.0f);
    const poligon::LandInfo& made = land.GetInfo();
    Expect(made.grid.patch_count_x == 2 && made.grid.patch_count_z == 2 && made.lakes.empty() && !made.has_lake_viewpoint &&
               made.background.width == 17 && made.background_weights[0].pixels.empty(),
           "256 m in 128 m patches: 2 x 2 patches, no lakes, a background from the heights alone");

    // --- Cooked and read back ---
    std::string error;
    Expect(poligon::CookLand(land, "hm", 7u, cooked.string().c_str(), error), "land from a height map is cooked");
    poligon::AssetLibrary library;
    Expect(library.Open(cooked.string().c_str(), error), "its files open");
    poligon::LandFromFiles from_files(library, "hm", origin);
    Expect(from_files.IsOpen(), "the cooked world opens");
    if (!from_files.IsOpen()) {
        return test::ExitCode();
    }
    bool all_same = from_files.GetInfo().background.heights == made.background.heights;
    for (int z = 0; z < 2; ++z) {
        for (int x = 0; x < 2; ++x) {
            all_same = all_same && SamePatch(*from_files.GetPatch({x, z}), land.CutPatch({x, z}));
        }
    }
    Expect(all_same, "every patch read back equals the cut patch, its layer indices too");

    // --- Plant rules on maps this land does not have plant nothing; on slope and height they do ---
    {
        const poligon::LandPatch& patch = *from_files.GetPatch({0, 0});
        poligon::PlantRule rule;
        rule.density = 0.05f;
        rule.spacing = 4.0f;
        const auto count = [&](const poligon::PlantRule& with) {
            return poligon::PlacePlantsInRectangle(patch.maps, with, glm::vec2(64.0f), 1000.0f, glm::vec2(0.0f), glm::vec2(128.0f), 1).size();
        };
        poligon::PlantRule on_slope = rule;
        on_slope.conditions.slope_degrees = {0.0f, 30.0f, 5.0f};
        poligon::PlantRule wet = rule;
        wet.conditions.moisture = {0.0f, 1.0f, 0.1f};  // would hold everywhere on generated land
        poligon::PlantRule shore = rule;
        shore.conditions.water_distance = {0.0f, 10.0f, 2.0f};  // a missing map read as 0 would mean "at the water"
        poligon::PlantRule dry_land = rule;
        dry_land.conditions.water_depth = {0.0f, 0.0f, 0.1f};
        poligon::PlantRule rocky = rule;
        rocky.conditions.rock = {0.0f, 0.5f, 0.1f};
        std::printf("  plants: on slope %zu, moisture %zu, near water %zu, on dry land %zu, by rock %zu\n", count(on_slope), count(wet),
                    count(shore), count(dry_land), count(rocky));
        Expect(count(on_slope) > 20, "a rule on slope alone plants on land from a height map");
        Expect(count(wet) == 0 && count(shore) == 0 && count(dry_land) == 0 && count(rocky) == 0,
               "rules on maps the land does not have plant nothing");
    }

    // --- Streamed around the camera ---
    poligon::RendererConfig config;
    config.width = kSize;
    config.height = kSize;
    config.headless = true;
    config.enable_validation = true;
    config.backend = backend;
    config.post.tone_mapping = poligon::ToneMapping::None;
    poligon::Renderer renderer(config);
    poligon::Image colors[4] = {poligon::MakeSolidImage(4, 4, 255, 0, 0), poligon::MakeSolidImage(4, 4, 0, 255, 0),
                                poligon::MakeSolidImage(4, 4, 0, 0, 255), poligon::MakeSolidImage(4, 4, 255, 255, 255)};
    poligon::TerrainConfig terrain_config;
    terrain_config.material.colors = renderer.CreateTextureArray(colors, 4);
    terrain_config.material.layer_count = 4;
    terrain_config.detail = 8.0f;
    poligon::Terrain terrain(renderer, from_files.GetInfo(), terrain_config, 9);
    poligon::TerrainPatchSource terrain_source(terrain, from_files);
    poligon::PhysicsWorld physics;
    poligon::TerrainPhysicsSource collisions(physics, from_files);
    poligon::SkyOcclusionPatchSource sky(from_files, 1, 4.0f, 60.0f);
    poligon::StreamingGridSettings grid_settings = from_files.GetInfo().grid;
    grid_settings.origin = {origin.x, origin.z};
    const poligon::StreamingGrid grid(grid_settings);
    poligon::WorldStreamer streamer(grid);
    streamer.AddSource(terrain_source, 1, poligon::StreamingMode::Background);
    streamer.AddSource(sky, 1, poligon::StreamingMode::Background);
    streamer.AddSource(collisions, 1, poligon::StreamingMode::Immediate);
    streamer.SetSynchronous(true);
    streamer.Update({0.0, 0.0, 0.0});
    streamer.FinishLoading();
    streamer.RequirePatches({{0.0, 0.0, 0.0}});
    Expect(terrain.GetPatchCount() == 4 && collisions.GetBodyCount() == 4, "the window around the middle holds all four patches");

    // Heights: the map's, to the bit at its points.
    bool heights_same = true;
    for (int i = 0; i < 200; ++i) {
        const int x = (i * 37) % (map.width - 1);  // the last row and column (the land's far edge) belong to no patch
        const int z = (i * 61) % (map.depth - 1);
        const double wanted = origin.y + static_cast<double>(map.heights[static_cast<size_t>(z) * static_cast<size_t>(map.width) + static_cast<size_t>(x)]);
        const double got = terrain.GetHeight(origin.x + x * 2.0, origin.z + z * 2.0);
        if (got != wanted && heights_same) {
            std::printf("  height at point %d %d: %.9f, the map's %.9f\n", x, z, got, wanted);
        }
        heights_same = heights_same && got == wanted;
    }
    Expect(heights_same, "the streamed land has the height map's heights");
    const poligon::RayHit hit = physics.CastRay({30.0, 200.0, 40.0}, {0.0f, -1.0f, 0.0f}, 400.0f);
    Expect(hit.hit && std::fabs(hit.position.y - terrain.GetHeight(30.0, 40.0)) < 0.05, "physics stands on the same heights");
    const poligon::GroundMap sky_map = sky.GetGroundMap();
    float darkest = 1.0f;
    for (int i = 0; sky_map.values != nullptr && i < sky_map.resolution * sky_map.resolution; ++i) {
        darkest = std::min(darkest, static_cast<float>(sky_map.values[i]) / 255.0f);
    }
    Expect(sky_map.resolution > 0 && darkest < 1.0f, "sky occlusion is made from the heights (the hill hides some sky)");

    // Each patch shows its own layer: seen from above, red, green, blue and (in the south east)
    // white; the unused channels naming layer 3 add nothing elsewhere.
    poligon::Camera above;
    above.position = {0.0, 260.0, 0.0};
    above.target = {0.0, 0.0, 0.0};
    above.up = {0.0f, 0.0f, -1.0f};
    poligon::DirectionalLight light;
    light.color = {0.0f, 0.0f, 0.0f, 1.0f};
    light.ambient = {1.0f, 1.0f, 1.0f, 1.0f};
    poligon::Environment environment;
    environment.shadows = false;
    renderer.BeginFrame(above, light, environment);
    terrain.Draw(above);
    renderer.EndFrame();
    poligon::Image frame;
    renderer.CaptureFrame(frame);
    const auto pixel = [&](int x, int y) { return &frame.pixels[static_cast<size_t>(y * kSize + x) * 4]; };
    const std::uint8_t* north_west = pixel(80, 80);
    const std::uint8_t* north_east = pixel(176, 80);
    const std::uint8_t* south_west = pixel(80, 176);
    const std::uint8_t* south_east = pixel(176, 176);
    std::printf("  patches: %d %d %d | %d %d %d | %d %d %d | %d %d %d\n", north_west[0], north_west[1], north_west[2], north_east[0],
                north_east[1], north_east[2], south_west[0], south_west[1], south_west[2], south_east[0], south_east[1], south_east[2]);
    Expect(north_west[0] > 200 && north_west[1] < 30 && north_west[2] < 30, "patch (0, 0) shows layer 0");
    Expect(north_east[1] > 200 && north_east[0] < 30 && north_east[2] < 30, "patch (1, 0) shows layer 1");
    Expect(south_west[2] > 200 && south_west[0] < 30 && south_west[1] < 30, "patch (0, 1) shows layer 2");
    Expect(south_east[0] > 200 && south_east[1] > 200 && south_east[2] > 200, "patch (1, 1) shows layer 3");

    // The far background: with the window at a corner, the other patches come from it.
    poligon::WorldStreamer corner_streamer(grid);
    poligon::Terrain corner_terrain(renderer, from_files.GetInfo(), terrain_config, 4);
    poligon::TerrainPatchSource corner_source(corner_terrain, from_files);
    corner_streamer.AddSource(corner_source, 0, poligon::StreamingMode::Background);
    corner_streamer.SetSynchronous(true);
    corner_streamer.Update({-120.0, 0.0, -120.0});
    corner_streamer.FinishLoading();
    renderer.BeginFrame(above, light, environment);
    corner_terrain.Draw(above);
    renderer.EndFrame();
    Expect(corner_terrain.GetPatchCount() == 1 && corner_terrain.GetStats().background_patches > 0,
           "beyond the window the background draws the land from the heights");

    std::printf("height_map_land_test finished\n");
    return test::ExitCode();
}

// Checks the ground cover: plants exist only within the radius, follow the rules (slope,
// height, patches), stand on the ground, come out the same when a cell is rebuilt, and are drawn.
// Usage: ground_cover_test d3d12|vulkan
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "poligon/ground_cover.h"
#include "poligon/height_map.h"
#include "poligon/renderer.h"
#include "poligon/terrain.h"
#include "test_support.h"

namespace {

constexpr int kSize = 256;

} // namespace

int main(int argc, char** argv) {
    using test::Expect;

    poligon::GraphicsBackend backend = poligon::GraphicsBackend::Default;
    if (argc == 2 && std::strcmp(argv[1], "d3d12") == 0) {
        backend = poligon::GraphicsBackend::Direct3D12;
    } else if (argc == 2 && std::strcmp(argv[1], "vulkan") == 0) {
        backend = poligon::GraphicsBackend::Vulkan;
    } else {
        std::printf("usage: ground_cover_test d3d12|vulkan\n");
        return 2;
    }

    poligon::RendererConfig config;
    config.width = kSize;
    config.height = kSize;
    config.headless = true;
    config.enable_validation = true;
    config.backend = backend;
    config.post.tone_mapping = poligon::ToneMapping::None;
    poligon::Renderer renderer(config);
    const glm::mat4 identity(1.0f);

    // Rolling hills, 400 x 400 m, with slopes up to about 25 degrees.
    const poligon::HeightMap map = poligon::MakeNoiseHeightMap(201, 201, 2.0f, 5u, 40.0f, 12.0f);
    poligon::TerrainConfig terrain_config;
    terrain_config.origin = {-200.0f, 0.0f, -200.0f};
    poligon::Terrain terrain(renderer, map, terrain_config);

    poligon::Material red;
    red.color = {1.0f, 0.0f, 0.0f, 1.0f};
    red.roughness = 1.0f;
    poligon::GroundCoverLayer tufts;
    tufts.species.levels.resize(1);
    tufts.species.levels[0].parts.push_back({renderer.CreateCubeMesh(), red, glm::scale(identity, glm::vec3(0.5f))});
    tufts.species.levels[0].max_distance = 60.0f;
    tufts.density = 0.5f;
    tufts.max_slope_degrees = 10.0f;
    tufts.align_to_ground = true;
    // The same plants everywhere regardless of slope, and the same in patches only.
    poligon::GroundCoverLayer everywhere = tufts;
    everywhere.seed = 3;
    everywhere.max_slope_degrees = 90.0f;
    everywhere.align_to_ground = false;
    poligon::GroundCoverLayer patchy = everywhere;
    patchy.patch_threshold = 0.7f;

    poligon::GroundCover cover(terrain, 80.0f, 16.0f);
    const int tuft_layer = cover.AddLayer(tufts);
    const int everywhere_layer = cover.AddLayer(everywhere);
    const int patchy_layer = cover.AddLayer(patchy);

    cover.Update({0.0f, 10.0f, 0.0f});
    std::vector<glm::mat4> first;
    cover.CollectInstances(tuft_layer, first);
    const poligon::GroundCoverStats stats = cover.GetStats();
    Expect(stats.cells_built > 0 && stats.cells_alive == stats.cells_built, "the first update builds every cell in reach");
    Expect(!first.empty(), "plants were placed");

    bool all_in_reach = true;
    bool all_on_ground = true;
    bool all_gentle = true;
    bool all_tilted_right = true;
    for (const glm::mat4& transform : first) {
        const glm::vec3 foot(transform[3]);
        all_in_reach = all_in_reach && glm::length(glm::vec2(foot.x, foot.z)) <= 80.0f + 16.0f * 1.5f;
        all_on_ground = all_on_ground && std::fabs(foot.y - terrain.GetHeight(foot.x, foot.z)) < 0.01f;
        all_gentle = all_gentle && terrain.GetSlopeDegrees(foot.x, foot.z) <= 10.0f;
        // Aligned to the ground: the plant's up axis is the land's normal.
        const glm::vec3 up = glm::normalize(glm::vec3(transform[1]));
        all_tilted_right = all_tilted_right && glm::dot(up, terrain.GetNormal(foot.x, foot.z)) > 0.999f;
    }
    Expect(all_in_reach, "no plant lies beyond the radius plus a cell");
    Expect(all_on_ground, "every plant stands on the ground");
    Expect(all_gentle, "no plant stands on ground steeper than its layer allows");
    Expect(all_tilted_right, "aligned plants tilt with the slope");

    // The patchy layer leaves large parts empty: well under half the plants of the even one.
    std::vector<glm::mat4> everywhere_plants;
    std::vector<glm::mat4> patches;
    cover.CollectInstances(everywhere_layer, everywhere_plants);
    cover.CollectInstances(patchy_layer, patches);
    Expect(everywhere_plants.size() > first.size(), "the looser slope rule allows more plants");
    Expect(!patches.empty() && patches.size() < everywhere_plants.size() / 2, "a patch threshold leaves most of the ground bare");

    // Walking away drops the cells; coming back rebuilds them identically.
    cover.Update({500.0f, 10.0f, 500.0f});
    Expect(cover.GetStats().cells_built > 0, "new cells are built where the camera arrives");
    std::vector<glm::mat4> far_away;
    cover.CollectInstances(tuft_layer, far_away);
    bool none_left_behind = true;
    for (const glm::mat4& transform : far_away) {
        none_left_behind = none_left_behind && glm::length(glm::vec2(transform[3].x, transform[3].z)) > 300.0f;
    }
    Expect(none_left_behind, "cells left behind are dropped");
    cover.Update({0.0f, 10.0f, 0.0f});
    std::vector<glm::mat4> again;
    cover.CollectInstances(tuft_layer, again);
    Expect(again == first, "a rebuilt cell holds exactly the same plants");
    cover.Update({1.0f, 10.0f, 1.0f});
    Expect(cover.GetStats().cells_built == 0, "moving within a cell builds nothing");

    // Drawn: the camera above the origin sees red cubes on the ground.
    poligon::Camera camera;
    camera.position = {0.0f, 25.0f, 0.0f};
    camera.target = {0.0f, 0.0f, 0.0f};
    camera.up = {0.0f, 0.0f, -1.0f};
    poligon::DirectionalLight light;
    light.color = {0.0f, 0.0f, 0.0f, 1.0f};
    light.ambient = {1.0f, 1.0f, 1.0f, 1.0f};
    renderer.BeginFrame(camera, light);
    terrain.Draw(camera);
    cover.Draw(renderer, camera);
    renderer.EndFrame();
    poligon::Image frame;
    renderer.CaptureFrame(frame);
    int red_pixels = 0;
    for (size_t i = 0; i < frame.pixels.size(); i += 4) {
        red_pixels += frame.pixels[i] > 200 && frame.pixels[i + 1] < 80 ? 1 : 0;
    }
    Expect(renderer.GetFrameStats().instances_drawn > 0 && red_pixels > 20, "the ground cover is drawn");

    std::printf("ground_cover_test finished\n");
    return test::ExitCode();
}

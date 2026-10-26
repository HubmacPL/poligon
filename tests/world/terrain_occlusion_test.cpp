// Checks occlusion culling by terrain (poligon/world/terrain_occlusion.h): on flat land with a
// broad hill 50 m high between x = 300 and 600 m, seen from x = 100 m at eye height, a box behind
// the hill is hidden, the same box in front of it is not, a tower behind it that rises above
// the hill's top is not, and nothing is hidden without a floor. The floor never rises above the
// land. Then a vegetation cell behind the hill is not drawn in the picture (FrameStats), only as
// a shadow caster, while the cell in front is drawn.
// Usage: terrain_occlusion_test d3d12|vulkan
#include <cmath>
#include <cstdio>
#include <cstring>

#include "poligon/render/renderer.h"
#include "poligon/world/land.h"
#include "poligon/world/terrain_occlusion.h"
#include "poligon/world/vegetation.h"
#include "test_support.h"

int main(int argc, char** argv) {
    using test::Expect;

    poligon::GraphicsBackend backend = poligon::GraphicsBackend::Default;
    if (argc == 2 && std::strcmp(argv[1], "d3d12") == 0) {
        backend = poligon::GraphicsBackend::Direct3D12;
    } else if (argc == 2 && std::strcmp(argv[1], "vulkan") == 0) {
        backend = poligon::GraphicsBackend::Vulkan;
    } else {
        std::printf("usage: terrain_occlusion_test d3d12|vulkan\n");
        return 2;
    }

    // 1 km of land at 2 m: flat, with a hill rising over 50 m to a flat top 50 m high.
    poligon::HeightMap land;
    land.width = 501;
    land.depth = 501;
    land.spacing = 2.0f;
    land.heights.resize(static_cast<size_t>(land.width) * static_cast<size_t>(land.depth));
    for (int z = 0; z < land.depth; ++z) {
        for (int x = 0; x < land.width; ++x) {
            const float meters = static_cast<float>(x) * land.spacing;
            const float rise = std::fmin(std::fmax(std::fmin(meters - 300.0f, 600.0f - meters) / 50.0f, 0.0f), 1.0f);
            land.heights[static_cast<size_t>(z) * static_cast<size_t>(land.width) + static_cast<size_t>(x)] = 50.0f * rise;
        }
    }
    const poligon::HeightMap floor = poligon::MakeOcclusionFloor(land, 32);
    bool below = true;
    for (float x = 0.0f; x <= 1000.0f; x += 1.0f) {
        below = below && floor.GetHeight(x, 500.0f) <= land.GetHeight(x, 500.0f) + 1e-3f;
    }
    Expect(below, "the occlusion floor never rises above the land");

    poligon::TerrainOcclusion occlusion;
    const glm::dvec3 eye(100.0, 1.7, 500.0);
    occlusion.Update(eye);
    Expect(!occlusion.IsHidden({700.0, 0.0, 480.0}, {732.0, 15.0, 512.0}), "without a floor nothing is hidden");
    occlusion.SetFloor(&floor, glm::dvec3(0.0));
    occlusion.Update(eye);
    const bool behind = occlusion.IsHidden({700.0, 0.0, 480.0}, {732.0, 15.0, 512.0});
    const bool in_front = occlusion.IsHidden({200.0, 0.0, 480.0}, {232.0, 15.0, 512.0});
    const bool tower = occlusion.IsHidden({700.0, 0.0, 480.0}, {732.0, 150.0, 512.0});
    std::printf("  hidden: behind the hill %d, in front %d, a tall tower behind %d\n", behind, in_front, tower);
    Expect(behind, "a box behind the hill is hidden");
    Expect(!in_front, "the same box in front of the hill is not");
    Expect(!tower, "a tower rising above the hill's top is not");

    // Plants: one cell in front of the hill, one behind it.
    poligon::RendererConfig config;
    config.width = 128;
    config.height = 128;
    config.headless = true;
    config.backend = backend;
    poligon::Renderer renderer(config);
    poligon::VegetationSpecies species;
    species.levels.resize(1);
    poligon::VegetationPart part;
    part.mesh = renderer.CreateCubeMesh();
    species.levels[0].parts = {part};
    species.levels[0].max_distance = 2000.0f;
    species.shadow_distance = 2000.0f;
    poligon::Vegetation vegetation;
    const int kind = vegetation.AddSpecies(species);
    const glm::mat4 identity(1.0f);
    for (int i = 0; i < 4; ++i) {
        vegetation.AddInstance(kind, glm::dvec3(210.0 + 4.0 * i, 0.0, 500.0), glm::scale(identity, glm::vec3(2.0f)));
        vegetation.AddInstance(kind, glm::dvec3(710.0 + 4.0 * i, 0.0, 500.0), glm::scale(identity, glm::vec3(2.0f)));
    }
    vegetation.SetOcclusion(&occlusion);
    poligon::Camera camera;
    camera.position = eye;
    camera.target = {800.0, 1.7, 500.0};
    camera.far_plane = 3000.0f;
    poligon::DirectionalLight sun;
    poligon::Environment environment;
    environment.shadow_distance = 300.0f;
    renderer.BeginFrame(camera, sun, environment);
    vegetation.Draw(renderer, camera);
    renderer.EndFrame();
    const poligon::FrameStats& stats = renderer.GetFrameStats();
    std::printf("  cells hidden %d, instances submitted %d, drawn in view %d\n", vegetation.GetStats().cells_hidden_by_terrain,
                stats.instances_submitted, stats.instances_drawn);
    Expect(vegetation.GetStats().cells_hidden_by_terrain == 1, "the cell behind the hill is hidden");
    Expect(stats.instances_drawn == 4, "only the four plants in front are drawn in the picture");
    return test::ExitCode();
}

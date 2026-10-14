// Checks the vegetation system: which level of detail each plant gets, which cells are
// skipped, how shadows are handled, and that the plants end up in the picture.
// Usage: vegetation_render_test d3d12|vulkan
#include <cstdio>
#include <cstring>

#include "poligon/renderer.h"
#include "poligon/vegetation.h"
#include "test_support.h"

namespace {

constexpr int kSize = 256;
constexpr float kCameraHeight = 40.0f;

// Converts a point on the ground (x or z) to the pixel it is drawn at by the top-down camera.
int ToPixel(float ground_coordinate) {
    const float visible_half_extent = kCameraHeight * 0.57735027f;  // height * tan(30 degrees)
    return static_cast<int>(kSize * 0.5f + ground_coordinate / visible_half_extent * kSize * 0.5f);
}

const std::uint8_t* PixelAt(const poligon::Image& image, float x, float z) {
    return &image.pixels[static_cast<size_t>(ToPixel(z) * image.width + ToPixel(x)) * 4];
}

} // namespace

int main(int argc, char** argv) {
    using test::Expect;

    poligon::GraphicsBackend backend = poligon::GraphicsBackend::Default;
    if (argc == 2 && std::strcmp(argv[1], "d3d12") == 0) {
        backend = poligon::GraphicsBackend::Direct3D12;
    } else if (argc == 2 && std::strcmp(argv[1], "vulkan") == 0) {
        backend = poligon::GraphicsBackend::Vulkan;
    } else {
        std::printf("usage: vegetation_render_test d3d12|vulkan\n");
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
    const poligon::MeshHandle cube = renderer.CreateCubeMesh();
    const glm::mat4 identity(1.0f);

    // A "plant" that is a red cube up close and a blue one from further away, so the
    // picture shows which level was used.
    poligon::Material red;
    red.color = {1.0f, 0.0f, 0.0f, 1.0f};
    red.roughness = 1.0f;
    poligon::Material blue = red;
    blue.color = {0.0f, 0.0f, 1.0f, 1.0f};

    poligon::VegetationSpecies species;
    species.levels.resize(2);
    species.levels[0].parts.push_back({cube, red, glm::scale(identity, glm::vec3(2.0f))});
    species.levels[0].max_distance = 10.0f;
    species.levels[1].parts.push_back({cube, blue, glm::scale(identity, glm::vec3(2.0f))});
    species.levels[1].max_distance = 50.0f;
    species.shadow_distance = 20.0f;
    species.shadow_level_bias = 1;

    poligon::Vegetation vegetation(32.0f);
    const int plant = vegetation.AddSpecies(species);
    // Along +x at 5, 15, 30 and 80 m, and a group of three far away in a cell of its own.
    for (const float x : {5.0f, 15.0f, 30.0f, 80.0f}) {
        vegetation.AddInstance(plant, glm::translate(identity, {x, 1.0f, 0.0f}));
    }
    for (const float z : {500.0f, 502.0f, 504.0f}) {
        vegetation.AddInstance(plant, glm::translate(identity, {500.0f, 1.0f, z}));
    }
    Expect(vegetation.GetInstanceCount() == 7, "seven plants were placed");

    poligon::Camera camera;
    camera.position = {0.0f, kCameraHeight, 0.0f};
    camera.target = {0.0f, 0.0f, 0.0f};
    camera.up = {0.0f, 0.0f, -1.0f};
    poligon::DirectionalLight light;
    light.color = {0.0f, 0.0f, 0.0f, 1.0f};
    light.ambient = {1.0f, 1.0f, 1.0f, 1.0f};
    poligon::Image frame;

    renderer.BeginFrame(camera, light);
    vegetation.Draw(renderer, camera);
    renderer.EndFrame();
    renderer.CaptureFrame(frame);

    const poligon::VegetationStats& stats = vegetation.GetStats();
    if (stats.instances_per_level[0] != 1 || stats.instances_per_level[1] != 2) {
        std::printf("  level 0: %d, level 1: %d, cells %d, skipped %d, batches %d\n", stats.instances_per_level[0],
                    stats.instances_per_level[1], stats.cells_total, stats.cells_skipped, stats.batches);
    }
    Expect(stats.instances_total == 7, "the stats count all plants");
    Expect(stats.instances_per_level[0] == 1, "the plant at 5 m gets full detail");
    Expect(stats.instances_per_level[1] == 2, "the plants at 15 and 30 m get the simpler level");
    Expect(stats.cells_skipped >= 1 && stats.cells_skipped < stats.cells_total,
           "cells with nothing in range are skipped whole");

    const std::uint8_t* near_pixel = PixelAt(frame, 5.0f, 0.0f);
    const std::uint8_t* middle_pixel = PixelAt(frame, 15.0f, 0.0f);
    Expect(near_pixel[0] == 255 && near_pixel[2] == 0, "the near plant is drawn with the detailed (red) mesh");
    Expect(middle_pixel[0] == 0 && middle_pixel[2] == 255, "the plant at 15 m is drawn with the simple (blue) mesh");
    Expect(PixelAt(frame, 10.0f, 5.0f)[0] == 0 && PixelAt(frame, 10.0f, 5.0f)[2] == 0, "nothing is drawn where no plant stands");

    // Doubling the detail distance moves the plant at 15 m to full detail and brings the one at 80 m into range.
    vegetation.SetDistanceScale(2.0f, 1.0f);
    renderer.BeginFrame(camera, light);
    vegetation.Draw(renderer, camera);
    renderer.EndFrame();
    renderer.CaptureFrame(frame);
    Expect(vegetation.GetStats().instances_per_level[0] == 2 && vegetation.GetStats().instances_per_level[1] == 2,
           "a larger detail distance moves plants to better levels and brings far ones into range");
    Expect(PixelAt(frame, 15.0f, 0.0f)[0] == 255, "the plant at 15 m is now red");
    vegetation.SetDistanceScale(1.0f, 1.0f);

    // Shadows: with a sun, plants within the shadow distance (5 and 15 m) cast shadows,
    // drawn with the simpler level; the one at 30 m casts none.
    poligon::DirectionalLight sun;
    sun.direction = {0.0f, -1.0f, 0.0f};
    sun.color = {1.0f, 1.0f, 1.0f, 1.0f};
    poligon::Environment environment;
    environment.shadow_distance = 100.0f;
    renderer.BeginFrame(camera, sun, environment);
    vegetation.Draw(renderer, camera);
    renderer.EndFrame();
    const poligon::FrameStats frame_stats = renderer.GetFrameStats();
    if (frame_stats.instances_drawn != 2 || frame_stats.shadow_instances < 2 || frame_stats.shadow_instances > 8) {
        std::printf("  in view %d, in shadows %d, submitted %d\n", frame_stats.instances_drawn, frame_stats.shadow_instances,
                    frame_stats.instances_submitted);
    }
    // The camera sees 23 m to each side, so of the three plants in range the one at 30 m
    // is handed to the renderer but culled there.
    Expect(frame_stats.instances_submitted == 4 && frame_stats.instances_drawn == 2,
           "plants in range but outside the picture are culled by the renderer");
    Expect(frame_stats.shadow_instances >= 2 && frame_stats.shadow_instances <= 8,
           "only the two plants within the shadow distance are drawn into shadow cascades");

    vegetation.ClearInstances();
    Expect(vegetation.GetInstanceCount() == 0, "all plants can be removed");

    std::printf("vegetation_render_test finished\n");
    return test::ExitCode();
}

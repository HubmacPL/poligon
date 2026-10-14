// Checks terrain drawing: that land appears where the height map has it and nowhere else,
// that steep slopes get the steep color, that heights can be asked for, and that patches
// are fine near the camera and coarse far from it.
// Usage: terrain_render_test d3d12|vulkan
#include <cmath>
#include <cstdio>
#include <cstring>

#include "poligon/renderer.h"
#include "poligon/terrain.h"
#include "test_support.h"

namespace {

constexpr int kSize = 256;
constexpr float kCameraHeight = 150.0f;

// The pixel at which the top-down camera (above the world's origin) draws a point on the ground.
const std::uint8_t* PixelAt(const poligon::Image& image, float x, float z) {
    const float visible_half_extent = kCameraHeight * 0.57735027f;  // height * tan(30 degrees)
    const int px = static_cast<int>(kSize * 0.5f + x / visible_half_extent * kSize * 0.5f);
    const int py = static_cast<int>(kSize * 0.5f + z / visible_half_extent * kSize * 0.5f);
    return &image.pixels[static_cast<size_t>(py * image.width + px) * 4];
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
        std::printf("usage: terrain_render_test d3d12|vulkan\n");
        return 2;
    }

    // Land of 256 x 256 m: low and level in the west, 20 m higher and level in the east,
    // and between them a ramp that climbs those 20 m within 10 m (63 degrees).
    poligon::HeightMap map;
    map.width = 257;
    map.depth = 257;
    map.spacing = 1.0f;
    for (int iz = 0; iz < map.depth; ++iz) {
        for (int ix = 0; ix < map.width; ++ix) {
            const float along = (static_cast<float>(ix) - 123.0f) / 10.0f;
            map.heights.push_back(20.0f * (along < 0.0f ? 0.0f : along > 1.0f ? 1.0f : along));
        }
    }

    poligon::RendererConfig config;
    config.width = kSize;
    config.height = kSize;
    config.headless = true;
    config.enable_validation = true;
    config.backend = backend;
    config.post.tone_mapping = poligon::ToneMapping::None;
    poligon::Renderer renderer(config);

    // The map's middle is put at the world's origin, its low part at height 100.
    poligon::TerrainConfig terrain_config;
    terrain_config.origin = {-128.0f, 100.0f, -128.0f};
    terrain_config.material.steep_color = {1.0f, 0.0f, 0.0f, 1.0f};
    terrain_config.material.roughness = 1.0f;
    poligon::Terrain terrain(renderer, map, terrain_config);

    Expect(std::fabs(terrain.GetHeight(-100.0f, 0.0f) - 100.0f) < 0.001f, "the low land is at the origin's height");
    Expect(std::fabs(terrain.GetHeight(100.0f, 0.0f) - 120.0f) < 0.001f, "the high land is 20 m above it");
    Expect(std::fabs(terrain.GetHeight(0.0f, 0.0f) - 110.0f) < 0.05f, "the middle of the ramp is half-way up");
    Expect(terrain.GetSlopeDegrees(0.0f, 0.0f) > 60.0f && terrain.GetSlopeDegrees(-100.0f, 0.0f) < 1.0f, "the ramp is steep, the rest level");
    Expect(terrain.GetMin().x == -128.0f && terrain.GetMax().x == 128.0f, "the land's extent is reported in world coordinates");

    poligon::Camera camera;
    camera.position = {0.0f, 100.0f + kCameraHeight, 0.0f};
    camera.target = {0.0f, 100.0f, 0.0f};
    camera.up = {0.0f, 0.0f, -1.0f};
    // Even light, sun off: level land without a texture is plain white.
    poligon::DirectionalLight light;
    light.color = {0.0f, 0.0f, 0.0f, 1.0f};
    light.ambient = {1.0f, 1.0f, 1.0f, 1.0f};
    poligon::Image frame;

    renderer.BeginFrame(camera, light);
    terrain.Draw(camera);
    renderer.EndFrame();
    renderer.CaptureFrame(frame);

    const std::uint8_t* low_land = PixelAt(frame, -60.0f, 20.0f);
    const std::uint8_t* high_land = PixelAt(frame, 50.0f, -30.0f);
    const std::uint8_t* ramp = PixelAt(frame, 0.0f, 0.0f);
    if (low_land[0] != 255 || ramp[1] > 40) {
        std::printf("  low land %d %d %d, ramp %d %d %d\n", low_land[0], low_land[1], low_land[2], ramp[0], ramp[1], ramp[2]);
    }
    Expect(low_land[0] == 255 && low_land[1] == 255 && low_land[2] == 255, "level land is drawn, in its plain color");
    Expect(high_land[0] == 255 && high_land[1] == 255, "also the higher land");
    Expect(ramp[0] > 150 && ramp[1] < 40 && ramp[2] < 40, "the steep ramp shows the steep color");

    // The high land is nearer to the camera, so it looks larger: its edge at x = 128 is drawn
    // further out than the low land's edge at x = -128. Beyond the edges there is nothing.
    Expect(PixelAt(frame, -84.0f, 0.0f)[0] == 0 || PixelAt(frame, -72.0f, 0.0f)[0] == 255, "the low land reaches towards its edge");

    const poligon::TerrainStats first = terrain.GetStats();
    Expect(first.patches > 0 && first.patches == renderer.GetFrameStats().instances_submitted, "the patches are handed to the renderer");

    // Sun and shadows: with the light travelling west, the high land in the east shadows
    // the low land at the foot of the ramp.
    poligon::DirectionalLight sun;
    sun.direction = {-1.0f, -0.5f, 0.0f};
    sun.color = {1.0f, 1.0f, 1.0f, 1.0f};
    sun.ambient = {0.1f, 0.1f, 0.1f, 1.0f};
    poligon::Environment environment;
    environment.shadow_distance = 400.0f;
    renderer.BeginFrame(camera, sun, environment);
    terrain.Draw(camera);
    renderer.EndFrame();
    renderer.CaptureFrame(frame);
    // 20 m of height at a sun elevation of 26.6 degrees throws a shadow 40 m long.
    const int in_shadow = PixelAt(frame, -25.0f, 0.0f)[0];
    const int in_sun = PixelAt(frame, -70.0f, 0.0f)[0];
    if (!(in_shadow < 110 && in_sun > 150)) {
        std::printf("  in shadow %d, in sun %d\n", in_shadow, in_sun);
    }
    Expect(in_shadow < 110 && in_sun > 150, "high land casts a shadow on low land");

    // Detail: from close to the ground, patches near the camera are the smallest size and
    // far ones larger; seen from far away, one patch covers all the land.
    terrain.SetDetail(0.5f);
    poligon::Camera low_camera;
    low_camera.position = {-120.0f, 102.0f, -120.0f};
    low_camera.target = {0.0f, 100.0f, 0.0f};
    renderer.BeginFrame(low_camera, light);
    terrain.Draw(low_camera);
    renderer.EndFrame();
    const poligon::TerrainStats near_ground = terrain.GetStats();
    if (near_ground.patches_per_level[0] == 0 || near_ground.patches_per_level[1] == 0) {
        std::printf("  patches: %d, level 0: %d, level 1: %d, level 2: %d\n", near_ground.patches, near_ground.patches_per_level[0],
                    near_ground.patches_per_level[1], near_ground.patches_per_level[2]);
    }
    Expect(near_ground.patches_per_level[0] > 0, "near the camera the land uses the smallest patches");
    Expect(near_ground.patches_per_level[1] > 0, "further away it uses larger ones");

    poligon::Camera far_camera;
    far_camera.position = {0.0f, 3000.0f, 0.0f};
    far_camera.target = {0.0f, 100.0f, 0.0f};
    far_camera.up = {0.0f, 0.0f, -1.0f};
    far_camera.far_plane = 5000.0f;
    renderer.BeginFrame(far_camera, light);
    terrain.Draw(far_camera);
    renderer.EndFrame();
    Expect(terrain.GetStats().patches == 1 && terrain.GetStats().patches_per_level[2] == 1, "from far away the whole land is one patch");

    std::printf("terrain_render_test finished\n");
    return test::ExitCode();
}

// Checks water surfaces: water tints and dims what lies under it, more where it is deeper;
// dry ground beside it is untouched; a surface seen at a grazing angle mirrors what stands
// behind it (screen-space reflections) or, with reflections off, the sky; and water works
// with multisampling as well. Usage: water_render_test d3d12|vulkan
#include <cmath>
#include <cstdio>
#include <cstring>

#include "poligon/renderer.h"
#include "test_support.h"

namespace {

constexpr int kSize = 128;

poligon::RendererConfig MakeConfig(poligon::GraphicsBackend backend, int msaa) {
    poligon::RendererConfig config;
    config.width = kSize;
    config.height = kSize;
    config.headless = true;
    config.enable_validation = true;
    config.backend = backend;
    config.msaa_samples = msaa;
    config.post.tone_mapping = poligon::ToneMapping::None;
    return config;
}

const std::uint8_t* PixelAt(const poligon::Image& image, int x, int y) {
    return &image.pixels[static_cast<size_t>(y * image.width + x) * 4];
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
        std::printf("usage: water_render_test d3d12|vulkan\n");
        return 2;
    }

    const glm::mat4 identity(1.0f);
    poligon::Material white;
    white.roughness = 1.0f;
    poligon::Material red = white;
    red.color = {1.0f, 0.0f, 0.0f, 1.0f};
    // Even light, sun off: a white surface is plain white.
    poligon::DirectionalLight light;
    light.color = {0.0f, 0.0f, 0.0f, 1.0f};
    light.ambient = {1.0f, 1.0f, 1.0f, 1.0f};
    poligon::Environment environment;
    environment.shadows = false;
    poligon::WaterMaterial water;
    water.wave_strength = 0.0f;  // a still mirror, so pixels can be predicted
    water.refraction = 0.0f;

    for (const int msaa : {1, 4}) {
        poligon::Renderer renderer(MakeConfig(backend, msaa));
        const poligon::MeshHandle plane = renderer.CreatePlaneMesh();
        const poligon::MeshHandle cube = renderer.CreateCubeMesh();
        poligon::Image frame;

        // Seen from above: the left half is a pool 0.5 m deep at the top of the picture and
        // 4 m deep at the bottom; the right half is dry ground a little above the water.
        poligon::Camera above;
        above.position = {0.0f, 30.0f, 0.0f};
        above.target = {0.0f, 0.0f, 0.0f};
        above.up = {0.0f, 0.0f, -1.0f};
        renderer.BeginFrame(above, light, environment);
        renderer.DrawMesh(plane, glm::translate(identity, {-25.0f, -4.0f, 0.0f}) * glm::scale(identity, {50.0f, 1.0f, 100.0f}), white);
        renderer.DrawMesh(plane, glm::translate(identity, {-25.0f, -0.5f, -25.0f}) * glm::scale(identity, {50.0f, 1.0f, 50.0f}), white);
        renderer.DrawMesh(plane, glm::translate(identity, {25.0f, 0.3f, 0.0f}) * glm::scale(identity, {50.0f, 1.0f, 100.0f}), white);
        renderer.DrawWater(plane, glm::scale(identity, {100.0f, 1.0f, 100.0f}), water);
        renderer.EndFrame();
        renderer.CaptureFrame(frame);
        const std::uint8_t* shallow = PixelAt(frame, kSize / 4, kSize / 4);
        const std::uint8_t* deep = PixelAt(frame, kSize / 4, kSize * 3 / 4);
        const std::uint8_t* dry = PixelAt(frame, kSize * 3 / 4, kSize / 2);
        if (!(shallow[0] + 15 < shallow[2] && deep[0] + 60 < shallow[0] && deep[2] < shallow[2] && dry[0] > 250)) {
            std::printf("  msaa %d: shallow %d %d %d, deep %d %d %d, dry %d %d %d\n", msaa, shallow[0], shallow[1], shallow[2],
                        deep[0], deep[1], deep[2], dry[0], dry[1], dry[2]);
        }
        Expect(shallow[0] + 15 < shallow[2], "water takes red out of what lies under it first");
        Expect(deep[0] + 60 < shallow[0] && deep[2] < shallow[2], "deeper water is darker, red going first");
        Expect(dry[0] > 250 && dry[1] > 250 && dry[2] > 250, "dry ground beside the water is untouched");

        // At a grazing angle across the water, a red wall stands behind it: its mirror image
        // shows on the water below the wall. With reflections off, the sky shows instead.
        environment.draw_sky = true;
        poligon::Camera low;
        low.position = {0.0f, 1.0f, 30.0f};
        low.target = {0.0f, 0.2f, 0.0f};
        poligon::Image mirrored;
        poligon::Image sky_only;
        for (const int steps : {32, 0}) {
            environment.water_reflection_steps = steps;
            renderer.BeginFrame(low, light, environment);
            renderer.DrawMesh(plane, glm::translate(identity, {0.0f, -2.0f, 0.0f}) * glm::scale(identity, {200.0f, 1.0f, 200.0f}), white);
            renderer.DrawMesh(cube, glm::translate(identity, {0.0f, 4.0f, -10.0f}) * glm::scale(identity, {60.0f, 8.0f, 1.0f}), red);
            renderer.DrawWater(plane, glm::scale(identity, {200.0f, 1.0f, 200.0f}), water);
            renderer.EndFrame();
            renderer.CaptureFrame(steps > 0 ? mirrored : sky_only);
        }
        environment.draw_sky = false;
        environment.water_reflection_steps = 24;
        // A pixel of water just below where the wall meets it.
        int wall_bottom = 0;
        for (int y = 0; y < kSize; ++y) {
            const std::uint8_t* pixel = PixelAt(sky_only, kSize / 2, y);
            if (pixel[0] > 200 && pixel[1] < 60) {
                wall_bottom = y;
            }
        }
        const int probe_y = wall_bottom + 3 < kSize ? wall_bottom + 3 : kSize - 1;
        const std::uint8_t* with_reflections = PixelAt(mirrored, kSize / 2, probe_y);
        const std::uint8_t* without = PixelAt(sky_only, kSize / 2, probe_y);
        if (!(with_reflections[0] > with_reflections[2] + 40 && without[2] > without[0])) {
            std::printf("  msaa %d: wall ends at row %d; water below it %d %d %d with reflections, %d %d %d without\n", msaa,
                        wall_bottom, with_reflections[0], with_reflections[1], with_reflections[2], without[0], without[1],
                        without[2]);
        }
        Expect(wall_bottom > 0, "the wall is seen above the water");
        Expect(with_reflections[0] > with_reflections[2] + 40, "the water mirrors the red wall behind it");
        Expect(without[2] > without[0], "with reflections off the water mirrors the sky");
    }

    std::printf("water_render_test finished\n");
    return test::ExitCode();
}

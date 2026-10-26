// Checks blended materials (BlendMode::Blend): half-transparent red glass in front of a white
// wall tints it by half; two overlapping panes (blue nearer than red, drawn in the opposite
// order) are mixed farthest first; a pane behind the wall is hidden; with volumetric fog the
// glass is still drawn and tinted.
// Usage: blend_render_test d3d12|vulkan
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "poligon/render/renderer.h"
#include "test_support.h"

namespace {

const std::uint8_t* PixelAt(const poligon::Image& image, int x, int y) {
    return &image.pixels[static_cast<size_t>(y * image.width + x) * 4];
}

// A plane standing up and facing the camera (+z), `size` meters wide.
poligon::Transform Standing(double x, double y, double z, float size) {
    return poligon::Transform({x, y, z}, glm::angleAxis(glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f)), glm::vec3(size));
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
        std::printf("usage: blend_render_test d3d12|vulkan\n");
        return 2;
    }
    poligon::RendererConfig config;
    config.width = 256;
    config.height = 256;
    config.headless = true;
    config.enable_validation = true;
    config.backend = backend;
    config.post.tone_mapping = poligon::ToneMapping::None;
    poligon::Renderer renderer(config);
    const poligon::MeshHandle plane = renderer.CreatePlaneMesh();

    // Even light, sun off: colors come out as they are.
    poligon::DirectionalLight light;
    light.color = {0.0f, 0.0f, 0.0f, 1.0f};
    light.ambient = {1.0f, 1.0f, 1.0f, 1.0f};
    poligon::Environment environment;
    environment.shadows = false;
    poligon::Material wall;
    wall.roughness = 1.0f;
    poligon::Material red_glass = wall;
    red_glass.color = {1.0f, 0.0f, 0.0f, 0.5f};
    red_glass.blend = poligon::BlendMode::Blend;
    poligon::Material blue_glass = red_glass;
    blue_glass.color = {0.0f, 0.0f, 1.0f, 0.5f};
    poligon::Material green_glass = red_glass;
    green_glass.color = {0.0f, 1.0f, 0.0f, 0.9f};

    poligon::Camera camera;
    camera.position = {0.0, 0.0, 5.0};
    camera.target = {0.0, 0.0, 0.0};

    const auto draw = [&](const poligon::Environment& look) {
        renderer.BeginFrame(camera, light, look);
        renderer.DrawMesh(plane, Standing(0.0, 0.0, -2.0, 20.0f), wall);
        // Behind the wall: never seen.
        renderer.DrawMesh(plane, Standing(0.0, 0.0, -3.0, 20.0f), green_glass);
        // On the left, one pane.
        renderer.DrawMesh(plane, Standing(-1.2, 0.0, 0.0, 1.6f), red_glass);
        // On the right, blue nearer than red, drawn first: sorting must put it last.
        renderer.DrawMesh(plane, Standing(1.2, 0.0, 1.0, 1.0f), blue_glass);
        renderer.DrawMesh(plane, Standing(1.2, 0.0, 0.5, 1.6f), red_glass);
        renderer.EndFrame();
    };
    draw(environment);
    poligon::Image picture;
    renderer.CaptureFrame(picture);
    const std::uint8_t* clear = PixelAt(picture, 128, 30);
    const std::uint8_t* tinted = PixelAt(picture, 75, 128);
    const std::uint8_t* red_only = PixelAt(picture, 157, 128);
    const std::uint8_t* overlap = PixelAt(picture, 190, 128);
    std::printf("  wall %d %d %d, red glass %d %d %d, red only %d %d %d, overlap %d %d %d\n", clear[0], clear[1], clear[2],
                tinted[0], tinted[1], tinted[2], red_only[0], red_only[1], red_only[2], overlap[0], overlap[1], overlap[2]);
    Expect(clear[0] > 245 && clear[1] > 245 && clear[2] > 245, "the wall is white: the green pane behind it is hidden");
    // Half red over white: (1, 0.5, 0.5) in linear light, (255, 188, 188) on screen.
    Expect(tinted[0] > 245 && std::abs(tinted[1] - 188) <= 12 && std::abs(tinted[2] - 188) <= 12,
           "red glass at alpha 0.5 tints the wall by half");
    Expect(std::abs(red_only[1] - tinted[1]) <= 6, "the right red pane alone tints as the left one");
    // Blue over (red over white): (0.5, 0.25, 0.75), on screen (188, 137, 225); the wrong
    // order would give (0.75, 0.25, 0.5), (225, 137, 188).
    Expect(overlap[2] > overlap[0] + 20 && std::abs(overlap[0] - 188) <= 14 && std::abs(overlap[2] - 225) <= 14,
           "overlapping panes are mixed farthest first");
    Expect(renderer.GetFrameStats().meshes_drawn == 5, "all five planes are counted as drawn");

    // Dense volumetric fog: the glass is still drawn and tinted.
    poligon::Environment foggy = environment;
    foggy.volumetric_fog = true;
    foggy.fog_density = 0.05f;
    foggy.fog_color = {0.5f, 0.5f, 0.5f, 1.0f};
    for (int frame = 0; frame < 3; ++frame) {
        draw(foggy);
    }
    renderer.CaptureFrame(picture);
    tinted = PixelAt(picture, 75, 128);
    clear = PixelAt(picture, 128, 30);
    std::printf("  in fog: wall %d %d %d, red glass %d %d %d\n", clear[0], clear[1], clear[2], tinted[0], tinted[1], tinted[2]);
    Expect(tinted[0] > tinted[1] + 20, "in fog the glass still tints red");
    return test::ExitCode();
}

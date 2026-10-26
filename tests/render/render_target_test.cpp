// Checks render targets (Renderer::CreateRenderTarget): a second camera draws a red cube on a
// green floor into a 256 x 256 target; the window's camera then looks at a plane that wears the
// target as its texture, and the plane's middle shows the cube's red, its edge the floor's
// green. Both views run with temporal anti-aliasing for several frames each, in turn, so each
// keeps its own history; a resized target still works.
// Usage: render_target_test d3d12|vulkan
#include <cstdio>
#include <cstring>

#include "poligon/render/renderer.h"
#include "test_support.h"

namespace {

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
        std::printf("usage: render_target_test d3d12|vulkan\n");
        return 2;
    }
    poligon::RendererConfig config;
    config.width = 256;
    config.height = 256;
    config.headless = true;
    config.enable_validation = true;
    config.backend = backend;
    config.post.tone_mapping = poligon::ToneMapping::None;
    config.post.temporal_aa = true;
    poligon::Renderer renderer(config);
    const poligon::MeshHandle cube = renderer.CreateCubeMesh();
    const poligon::MeshHandle plane = renderer.CreatePlaneMesh();

    // Even light, sun off: colors come out as they are.
    poligon::DirectionalLight light;
    light.color = {0.0f, 0.0f, 0.0f, 1.0f};
    light.ambient = {1.0f, 1.0f, 1.0f, 1.0f};
    poligon::Environment environment;
    environment.shadows = false;
    poligon::Material red;
    red.color = {1.0f, 0.0f, 0.0f, 1.0f};
    red.roughness = 1.0f;
    poligon::Material green = red;
    green.color = {0.0f, 1.0f, 0.0f, 1.0f};

    // The second camera, high above a cube that fills the middle of its view.
    poligon::Camera scope;
    scope.position = {100.0, 10.0, 0.0};
    scope.target = {100.0, 0.0, 0.0};
    scope.up = {0.0f, 0.0f, -1.0f};
    // The window's camera, looking straight at a 4 m plane that stands in front of it.
    poligon::Camera eye;
    eye.position = {0.0, 0.0, 5.0};
    eye.target = {0.0, 0.0, 0.0};

    poligon::RenderTargetHandle target = renderer.CreateRenderTarget(256, 256);
    poligon::Material screen;
    screen.texture = renderer.GetRenderTargetTexture(target);
    screen.roughness = 1.0f;

    const auto draw_both = [&] {
        renderer.BeginFrame(scope, light, environment, target);
        renderer.DrawMesh(plane, poligon::Transform({100.0, 0.0, 0.0}, glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(40.0f)), green);
        renderer.DrawMesh(cube, poligon::Transform({100.0, 1.0, 0.0}, glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(4.0f)), red);
        renderer.EndFrame();
        renderer.BeginFrame(eye, light, environment);
        // The plane stands up, facing the camera (+z), its top towards +y.
        renderer.DrawMesh(plane,
                          poligon::Transform(glm::dvec3(0.0), glm::angleAxis(glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
                                             glm::vec3(4.0f)),
                          screen);
        renderer.EndFrame();
    };
    for (int frame = 0; frame < 8; ++frame) {
        draw_both();
    }
    poligon::Image picture;
    renderer.CaptureFrame(picture);
    const std::uint8_t* middle = PixelAt(picture, 128, 128);
    // The plane's edge: 2 m from its middle, near 1.8 m up (the plane is 4 m, seen from 5 m).
    const std::uint8_t* edge = PixelAt(picture, 128, 128 - 80);
    std::printf("  middle %d %d %d, edge %d %d %d\n", middle[0], middle[1], middle[2], edge[0], edge[1], edge[2]);
    Expect(middle[0] > 180 && middle[1] < 60, "the plane's middle shows the second camera's red cube");
    Expect(edge[1] > 180 && edge[0] < 60, "its edge shows the second camera's green floor");

    // A smaller target (a scope drawn at lower resolution) still works.
    renderer.ResizeRenderTarget(target, 64, 64);
    screen.texture = renderer.GetRenderTargetTexture(target);
    for (int frame = 0; frame < 4; ++frame) {
        draw_both();
    }
    renderer.CaptureFrame(picture);
    middle = PixelAt(picture, 128, 128);
    Expect(middle[0] > 180 && middle[1] < 60, "after resizing to 64 x 64 the target still shows the cube");
    Expect(renderer.GetFrameStats().meshes_drawn == 1, "the window's frame drew only its plane");
    renderer.DestroyRenderTarget(target);
    return test::ExitCode();
}

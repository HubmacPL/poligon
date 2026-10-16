// Example: render one frame without a window and save it as PNG.
// Usage: example_renderer OUTPUT.png
#include <cstdio>

#include "poligon/render/renderer.h"

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: example_renderer OUTPUT.png\n");
        return 2;
    }

    poligon::RendererConfig config;
    config.width = 640;
    config.height = 360;
    config.headless = true;  // no window; the frame exists only as an off-screen image
    config.clear_color = {0.1f, 0.1f, 0.15f, 1.0f};
    poligon::Renderer renderer(config);

    const poligon::MeshHandle cube = renderer.CreateCubeMesh();
    const poligon::MeshHandle plane = renderer.CreatePlaneMesh();

    poligon::Camera camera;
    camera.position = {3.0f, 2.5f, 4.0f};
    camera.target = {0.0f, 0.5f, 0.0f};
    const poligon::DirectionalLight light;

    renderer.BeginFrame(camera, light);
    const glm::mat4 identity(1.0f);
    // Plain color: pass a Color. (Textures: see examples/texture.)
    renderer.DrawMesh(plane, glm::scale(identity, {10.0f, 1.0f, 10.0f}), poligon::Color{0.4f, 0.4f, 0.4f, 1.0f});
    // A cube standing on the plane, turned by 30 degrees around the vertical axis.
    // Read right to left: rotate first, then move up.
    const glm::mat4 cube_transform = glm::translate(identity, {0.0f, 0.5f, 0.0f}) *
                                     glm::rotate(identity, glm::radians(30.0f), {0.0f, 1.0f, 0.0f});
    renderer.DrawMesh(cube, cube_transform, poligon::Color{0.9f, 0.5f, 0.1f, 1.0f});
    renderer.EndFrame();

    if (!renderer.SaveFrameToPng(argv[1])) {
        std::fprintf(stderr, "cannot write %s\n", argv[1]);
        return 1;
    }
    std::printf("saved %dx%d frame to %s using %s\n", renderer.GetWidth(), renderer.GetHeight(), argv[1],
                renderer.GetBackendName());
    return 0;
}

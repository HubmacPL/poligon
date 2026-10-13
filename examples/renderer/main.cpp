// Example: render one frame without a window and save it as PNG.
// Usage: example_renderer OUTPUT.png
#include <cstdio>

#include "poligon/renderer.h"

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
    renderer.DrawMesh(plane, poligon::MakeScale({10.0f, 1.0f, 10.0f}), {0.4f, 0.4f, 0.4f, 1.0f});
    // A cube standing on the plane, turned by 30 degrees.
    const poligon::Mat4 cube_transform = poligon::Multiply(
        poligon::MakeTranslation({0.0f, 0.5f, 0.0f}), poligon::MakeRotationY(poligon::DegreesToRadians(30.0f)));
    renderer.DrawMesh(cube, cube_transform, {0.9f, 0.5f, 0.1f, 1.0f});
    renderer.EndFrame();

    if (!renderer.SaveFrameToPng(argv[1])) {
        std::fprintf(stderr, "cannot write %s\n", argv[1]);
        return 1;
    }
    std::printf("saved %dx%d frame to %s using %s\n", renderer.GetWidth(), renderer.GetHeight(), argv[1],
                renderer.GetBackendName());
    return 0;
}

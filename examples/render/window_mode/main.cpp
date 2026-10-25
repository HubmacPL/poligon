// Example: switch between a 1280 x 720 window and fullscreen at 1920 x 1080, as a game's F11
// key does, and then resize the picture as when the window is dragged larger. With a window the display really changes its resolution (exclusive fullscreen);
// this example runs without one, so only the picture's size changes, which is what the
// renderer has to get right: every image that depends on the size is made again.
// Usage: example_window_mode OUTPUT.png
#include <cstdio>

#include "poligon/render/renderer.h"

namespace {

void DrawScene(poligon::Renderer& renderer, poligon::MeshHandle plane, poligon::MeshHandle cube) {
    poligon::Camera camera;
    camera.position = {3.0f, 2.5f, 4.0f};
    camera.target = {0.0f, 0.5f, 0.0f};
    poligon::Environment environment;
    environment.ambient_occlusion = true;
    renderer.BeginFrame(camera, poligon::DirectionalLight{}, environment);
    const glm::mat4 identity(1.0f);
    renderer.DrawMesh(plane, glm::scale(identity, {10.0f, 1.0f, 10.0f}), poligon::Color{0.4f, 0.4f, 0.4f, 1.0f});
    renderer.DrawMesh(cube, glm::translate(identity, {0.0f, 0.5f, 0.0f}), poligon::Color{0.9f, 0.5f, 0.1f, 1.0f});
    renderer.EndFrame();
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: example_window_mode OUTPUT.png\n");
        return 2;
    }
    poligon::RendererConfig config;
    config.width = 1280;
    config.height = 720;
    config.headless = true;
    poligon::Renderer renderer(config);
    const poligon::MeshHandle cube = renderer.CreateCubeMesh();
    const poligon::MeshHandle plane = renderer.CreatePlaneMesh();

    DrawScene(renderer, plane, cube);
    std::printf("windowed: %d x %d\n", renderer.GetWidth(), renderer.GetHeight());

    // Between frames (outside BeginFrame/EndFrame). False would mean the display has no
    // 1920 x 1080 mode; the game then stays as it was.
    if (!renderer.SetWindowMode(poligon::WindowMode::Fullscreen, 1920, 1080)) {
        std::fprintf(stderr, "cannot switch to 1920 x 1080\n");
        return 1;
    }
    DrawScene(renderer, plane, cube);
    std::printf("fullscreen: %d x %d\n", renderer.GetWidth(), renderer.GetHeight());

    if (!renderer.SaveFrameToPng(argv[1])) {
        std::fprintf(stderr, "cannot write %s\n", argv[1]);
        return 1;
    }
    renderer.SetWindowMode(poligon::WindowMode::Windowed, 1280, 720);
    std::printf("saved %s; back to a window of %d x %d\n", argv[1], renderer.GetWidth(), renderer.GetHeight());

    // The player drags the window's corner or maximizes it: with a window, BeginFrame sees the
    // new size and calls Resize by itself. Called directly here, as there is no window.
    renderer.Resize(1600, 900);
    DrawScene(renderer, plane, cube);
    std::printf("resized: %d x %d\n", renderer.GetWidth(), renderer.GetHeight());
    return 0;
}

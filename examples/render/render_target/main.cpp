// Example: binoculars. A second camera, from the player's eyes with a six times narrower view,
// draws a far deer stand into a render target; the window's frame shows the scene as the eyes
// see it and, in a circle of sprites, the target: the stand magnified. The target is drawn at a
// third of the window's size, as a scope that covers part of the screen would be.
// Usage: example_render_target OUTPUT.png
#include <cmath>
#include <cstdio>

#include "poligon/render/overlay.h"
#include "poligon/render/renderer.h"

namespace {

// Something to look at: a meadow, a row of trees as boxes, and a deer stand 120 m away.
void DrawScene(poligon::Renderer& renderer, poligon::MeshHandle cube, poligon::MeshHandle plane) {
    const glm::quat level(1.0f, 0.0f, 0.0f, 0.0f);
    renderer.DrawMesh(plane, poligon::Transform(glm::dvec3(0.0), level, glm::vec3(600.0f)), poligon::Color{0.35f, 0.45f, 0.2f, 1.0f});
    for (int i = -6; i <= 6; ++i) {
        renderer.DrawMesh(cube, poligon::Transform({i * 12.0, 4.0, -160.0}, level, {3.0f, 8.0f, 3.0f}), poligon::Color{0.15f, 0.3f, 0.12f, 1.0f});
    }
    // The stand: four legs and a box on top.
    for (const double x : {-1.0, 1.0}) {
        for (const double z : {-1.0, 1.0}) {
            renderer.DrawMesh(cube, poligon::Transform({x, 2.0, -120.0 + z}, level, {0.2f, 4.0f, 0.2f}), poligon::Color{0.4f, 0.3f, 0.2f, 1.0f});
        }
    }
    renderer.DrawMesh(cube, poligon::Transform({0.0, 4.8, -120.0}, level, {2.4f, 1.6f, 2.4f}), poligon::Color{0.5f, 0.35f, 0.2f, 1.0f});
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: example_render_target OUTPUT.png\n");
        return 2;
    }
    poligon::RendererConfig config;
    config.width = 960;
    config.height = 540;
    config.headless = true;
    config.post.temporal_aa = true;
    poligon::Renderer renderer(config);
    const poligon::MeshHandle cube = renderer.CreateCubeMesh();
    const poligon::MeshHandle plane = renderer.CreatePlaneMesh();

    poligon::DirectionalLight sun;
    sun.direction = {-0.4f, -0.7f, -0.3f};
    poligon::Environment environment;
    environment.draw_sky = true;
    environment.shadow_distance = 200.0f;

    poligon::Camera eyes;
    eyes.position = {0.0, 1.7, 0.0};
    eyes.target = {0.0, 3.0, -120.0};
    eyes.far_plane = 2000.0f;
    // The binoculars: the same place and direction, a six times narrower view (7.5 degrees).
    poligon::Camera binoculars = eyes;
    binoculars.vertical_fov_degrees = eyes.vertical_fov_degrees / 6.0f;

    // Round, so square; a third of the window's height.
    const poligon::RenderTargetHandle scope = renderer.CreateRenderTarget(180, 180);
    for (int frame = 0; frame < 16; ++frame) {
        // The scope's frame first, then the window's that shows it.
        renderer.BeginFrame(binoculars, sun, environment, scope);
        DrawScene(renderer, cube, plane);
        renderer.EndFrame();

        renderer.BeginFrame(eyes, sun, environment);
        DrawScene(renderer, cube, plane);
        // The scope's picture in the lower right, as a disc: rows of the square sprite cut to the circle.
        const poligon::TextureHandle picture = renderer.GetRenderTargetTexture(scope);
        const float size = 180.0f;
        const float left = 960.0f - size - 30.0f;
        const float top = 540.0f - size - 30.0f;
        // A dark ring around it: the same disc 6 pixels larger, under the picture.
        for (int row = -6; row < 186; ++row) {
            const float y = (static_cast<float>(row) + 0.5f) / 192.0f * 2.0f - 1.0f + 6.0f / 96.0f;
            const float half = std::sqrt(std::fmax(1.0f - y * y, 0.0f)) * 96.0f;
            renderer.DrawRect(poligon::Rect{left + 90.0f - half, top + static_cast<float>(row), half * 2.0f, 1.0f},
                              poligon::Color{0.05f, 0.05f, 0.05f, 1.0f});
        }
        for (int row = 0; row < 180; ++row) {
            const float y = (static_cast<float>(row) + 0.5f) / 180.0f * 2.0f - 1.0f;
            const float half = std::sqrt(std::fmax(1.0f - y * y, 0.0f));
            const float u0 = 0.5f - half * 0.5f;
            renderer.DrawSprite(picture, poligon::Rect{left + u0 * size, top + static_cast<float>(row), half * size, 1.0f},
                                poligon::Rect{u0, static_cast<float>(row) / 180.0f, half, 1.0f / 180.0f});
        }
        renderer.EndFrame();
    }
    if (!renderer.SaveFrameToPng(argv[1])) {
        std::fprintf(stderr, "cannot write %s\n", argv[1]);
        return 1;
    }
    std::printf("saved %s\n", argv[1]);
    return 0;
}

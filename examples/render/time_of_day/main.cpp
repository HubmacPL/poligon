// Example: the time of day. The sky from the atmosphere model (SkyModel::Atmosphere) with the
// sun placed by the hour: four frames of one small scene, at 6:00, 9:00, 12:00 and 20:00 solar
// time on 21 June near Lublin, put side by side in one picture with a render target each.
// Usage: example_time_of_day OUTPUT.png
#include <cmath>
#include <cstdio>

#include "poligon/render/overlay.h"
#include "poligon/render/renderer.h"
#include "poligon/render/sky.h"

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: example_time_of_day OUTPUT.png\n");
        return 2;
    }
    poligon::RendererConfig config;
    config.width = 960;
    config.height = 270;
    config.headless = true;
    config.post.auto_exposure = true;
    poligon::Renderer renderer(config);
    const poligon::MeshHandle cube = renderer.CreateCubeMesh();
    const poligon::MeshHandle plane = renderer.CreatePlaneMesh();

    poligon::Environment environment;
    environment.draw_sky = true;
    environment.sky_light = true;
    environment.sky_model = poligon::SkyModel::Atmosphere;
    environment.day_of_year = 172.0f;  // 21 June
    environment.latitude = 50.86f;
    // The sun's color above the air; the model reddens and dims it.
    poligon::DirectionalLight sun;
    sun.color = {3.6f, 3.6f, 3.6f, 1.0f};

    // Looking south-west over a meadow with a few blocks.
    poligon::Camera camera;
    camera.position = {0.0, 1.7, 0.0};
    camera.target = {-10.0, 3.0, 10.0};
    const glm::quat level(1.0f, 0.0f, 0.0f, 0.0f);

    const float hours[] = {6.0f, 9.0f, 12.0f, 20.0f};
    poligon::RenderTargetHandle targets[4];
    for (poligon::RenderTargetHandle& target : targets) {
        target = renderer.CreateRenderTarget(240, 270);
    }
    for (int frame = 0; frame < 8; ++frame) {
        for (int i = 0; i < 4; ++i) {
            environment.time_of_day = hours[i];
            renderer.BeginFrame(camera, sun, environment, targets[i]);
            renderer.DrawMesh(plane, poligon::Transform(glm::dvec3(0.0), level, glm::vec3(400.0f)), poligon::Color{0.35f, 0.45f, 0.2f, 1.0f});
            for (int b = 0; b < 5; ++b) {
                renderer.DrawMesh(cube, poligon::Transform({-6.0 - 4.0 * b, 1.0, 8.0 + 3.0 * b}, level, {1.5f, 2.0f, 1.5f}),
                                  poligon::Color{0.7f, 0.6f, 0.5f, 1.0f});
            }
            renderer.EndFrame();
        }
        renderer.BeginFrame(camera, sun, environment);
        for (int i = 0; i < 4; ++i) {
            renderer.DrawSprite(renderer.GetRenderTargetTexture(targets[i]), poligon::Rect{240.0f * static_cast<float>(i), 0.0f, 240.0f, 270.0f},
                                poligon::Rect{0.0f, 0.0f, 1.0f, 1.0f});
        }
        renderer.EndFrame();
    }
    const glm::vec3 noon = -poligon::ComputeSunDirection(12.0f, 172.0f, 50.86f);
    std::printf("the sun at noon: %.1f degrees up\n", glm::degrees(std::asin(noon.y)));
    if (!renderer.SaveFrameToPng(argv[1])) {
        std::fprintf(stderr, "cannot write %s\n", argv[1]);
        return 1;
    }
    std::printf("saved %s\n", argv[1]);
    return 0;
}

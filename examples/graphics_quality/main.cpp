// Example: graphics quality presets. A preset (low, medium, high, ultra) decides how much
// the picture may cost; the scene decides how it looks. Values that need a new renderer
// (shadow image size, drawing resolution) go into the RendererConfig; switches and step
// counts limit every frame's Environment and PostSettings.
// Usage: example_graphics_quality OUTPUT.png [low|medium|high|ultra]
#include <cstdio>

#include "poligon/graphics_quality.h"
#include "poligon/renderer.h"

int main(int argc, char** argv) {
    if (argc != 2 && argc != 3) {
        std::fprintf(stderr, "usage: example_graphics_quality OUTPUT.png [low|medium|high|ultra]\n");
        return 2;
    }
    poligon::GraphicsPreset preset = poligon::GraphicsPreset::Medium;
    if (argc == 3 && !poligon::ParseGraphicsPreset(argv[2], preset)) {
        std::fprintf(stderr, "unknown preset \"%s\"\n", argv[2]);
        return 2;
    }
    // Start from a preset; any single value can then be changed.
    poligon::GraphicsQuality quality = poligon::GetGraphicsPreset(preset);

    poligon::RendererConfig config;
    config.width = 640;
    config.height = 360;
    config.headless = true;
    config.post.temporal_aa = true;
    config.post.exposure = 0.6f;
    poligon::ApplyGraphicsQuality(quality, config);  // shadow image size, multisampling, render scale
    poligon::Renderer renderer(config);
    std::printf("preset %s: scene drawn at %d x %d, shown at %d x %d\n", poligon::GetGraphicsPresetName(preset),
                renderer.GetRenderWidth(), renderer.GetRenderHeight(), renderer.GetWidth(), renderer.GetHeight());

    const glm::mat4 identity(1.0f);
    const poligon::MeshHandle cube = renderer.CreateCubeMesh();
    const poligon::MeshHandle plane = renderer.CreatePlaneMesh();
    poligon::Camera camera;
    camera.position = {0.0f, 2.0f, 7.0f};
    camera.target = {0.0f, 0.8f, 0.0f};
    poligon::DirectionalLight sun;
    sun.direction = {-0.5f, -0.8f, -0.4f};
    sun.color = {2.6f, 2.45f, 2.2f, 1.0f};

    for (int frame = 0; frame < 8; ++frame) {
        // What the scene would like ...
        poligon::Environment environment;
        environment.draw_sky = true;
        environment.sky_light = true;
        environment.ambient_occlusion = true;
        environment.volumetric_fog = true;
        environment.fog_density = 0.01f;
        poligon::PostSettings post = renderer.GetPostSettings();
        post.bloom = true;
        // ... limited to what the quality allows, every frame.
        poligon::ApplyGraphicsQuality(quality, environment, post);
        renderer.SetPostSettings(post);

        renderer.BeginFrame(camera, sun, environment);
        renderer.DrawMesh(plane, glm::scale(identity, {60.0f, 1.0f, 60.0f}), poligon::Color{0.4f, 0.45f, 0.3f, 1.0f});
        for (int i = 0; i < 5; ++i) {
            renderer.DrawMesh(cube, glm::translate(identity, {static_cast<float>(i - 2) * 1.6f, 0.5f, static_cast<float>(-i)}),
                              poligon::Color{0.7f, 0.5f, 0.3f, 1.0f});
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

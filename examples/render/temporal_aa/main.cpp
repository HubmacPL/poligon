// Example: temporal anti-aliasing and color grading. TAA needs several frames: each one is
// rendered with the camera shifted by a different fraction of a pixel and blended into the
// previous result. Here a fence of thin posts (hard to smooth by any other means) is drawn
// for 16 frames, then graded warm with a vignette.
// Usage: example_temporal_aa OUTPUT.png
#include <cstdio>

#include "poligon/render/renderer.h"

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: example_temporal_aa OUTPUT.png\n");
        return 2;
    }
    const glm::mat4 identity(1.0f);

    poligon::RendererConfig config;
    config.width = 640;
    config.height = 360;
    config.headless = true;
    config.msaa_samples = 1;              // TAA replaces multisampling
    config.post.temporal_aa = true;       // blend each frame into the previous ones
    config.post.sharpen = 0.25f;          // give back a little of the softness
    config.post.temperature = 0.3f;       // warmer
    config.post.saturation = 1.1f;
    config.post.contrast = 1.05f;
    config.post.vignette = 0.25f;
    config.post.exposure = 0.6f;
    poligon::Renderer renderer(config);
    const poligon::MeshHandle cube = renderer.CreateCubeMesh();
    const poligon::MeshHandle plane = renderer.CreatePlaneMesh();

    poligon::Material wood;
    wood.color = {0.45f, 0.32f, 0.20f, 1.0f};
    poligon::Camera camera;
    camera.position = {0.0f, 1.6f, 6.0f};
    camera.target = {0.0f, 1.0f, -10.0f};
    poligon::DirectionalLight sun;
    sun.direction = {-0.5f, -1.0f, -0.3f};
    sun.color = {2.6f, 2.45f, 2.2f, 1.0f};
    poligon::Environment environment;
    environment.draw_sky = true;
    environment.sky_light = true;

    // A game simply renders every frame; TAA collects them on its own.
    for (int frame = 0; frame < 16; ++frame) {
        renderer.BeginFrame(camera, sun, environment);
        renderer.DrawMesh(plane, glm::dvec3(0.0), glm::scale(identity, {100.0f, 1.0f, 100.0f}), poligon::Color{0.35f, 0.42f, 0.25f, 1.0f});
        for (int i = 0; i < 40; ++i) {
            // Thin posts receding into the distance: a few pixels wide, then less than one.
            const float z = -static_cast<float>(i) * 1.5f;
            for (const float x : {-2.0f, 2.0f}) {
                renderer.DrawMesh(cube, glm::dvec3(0.0), glm::translate(identity, {x, 0.6f, z}) * glm::scale(identity, {0.05f, 1.2f, 0.05f}), wood);
            }
            renderer.DrawMesh(cube, glm::dvec3(0.0), glm::translate(identity, {-2.0f, 1.1f, z}) * glm::scale(identity, {0.03f, 0.03f, 1.5f}), wood);
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

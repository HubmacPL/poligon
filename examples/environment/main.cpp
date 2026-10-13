// Example: a sunny scene with sky, shadows, fog and a shiny and a matte material.
// Usage: example_environment OUTPUT.png
#include <cstdio>

#include "poligon/renderer.h"

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: example_environment OUTPUT.png\n");
        return 2;
    }

    poligon::RendererConfig config;
    config.width = 640;
    config.height = 360;
    config.headless = true;
    // Light values in a sunny scene go well above 1; exposure scales them before tone mapping.
    config.exposure = 0.6f;
    poligon::Renderer renderer(config);

    const poligon::MeshHandle cube = renderer.CreateCubeMesh();
    const poligon::MeshHandle plane = renderer.CreatePlaneMesh();

    poligon::Camera camera;
    camera.position = {4.0f, 2.5f, 5.0f};
    camera.target = {0.0f, 0.7f, 0.0f};

    // The sun: direction it shines in, and a warm color brighter than 1.
    poligon::DirectionalLight sun;
    sun.direction = {-0.5f, -0.75f, -0.4f};
    sun.color = {2.6f, 2.45f, 2.2f, 1.0f};

    poligon::Environment environment;
    environment.draw_sky = true;      // sky gradient and sun disc instead of the clear color
    environment.sky_light = true;     // shadows are lit by the blue sky, not by flat gray
    environment.shadow_distance = 40.0f;
    environment.fog_density = 0.01f;  // light haze

    const glm::mat4 identity(1.0f);
    renderer.BeginFrame(camera, sun, environment);

    poligon::Material ground;
    ground.color = {0.35f, 0.45f, 0.25f, 1.0f};
    ground.roughness = 1.0f;
    renderer.DrawMesh(plane, glm::scale(identity, {60.0f, 1.0f, 60.0f}), ground);

    poligon::Material matte;
    matte.color = {0.8f, 0.3f, 0.2f, 1.0f};
    matte.roughness = 0.9f;
    renderer.DrawMesh(cube, glm::translate(identity, {-1.0f, 0.5f, 0.0f}), matte);

    poligon::Material polished_metal;
    polished_metal.color = {0.9f, 0.8f, 0.5f, 1.0f};
    polished_metal.metallic = 1.0f;
    polished_metal.roughness = 0.25f;
    renderer.DrawMesh(cube, glm::translate(identity, {1.0f, 0.75f, 0.0f}) * glm::scale(identity, {1.0f, 1.5f, 1.0f}),
                      polished_metal);

    renderer.EndFrame();

    if (!renderer.SaveFrameToPng(argv[1])) {
        std::fprintf(stderr, "cannot write %s\n", argv[1]);
        return 1;
    }
    std::printf("saved %s (multisampling: %d samples)\n", argv[1], renderer.GetMsaaSamples());
    return 0;
}

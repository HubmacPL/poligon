// Example: a water surface. A pond 3 m deep with a sloping sandy bottom, a red post and a
// white wall behind it, seen from the shore at a low angle: the water is clear at the edge,
// darker and greener where it is deep, mirrors the post and the wall, and ripples in the
// wind. Lakes of generated land get their surfaces from poligon/world/water.h.
// Usage: example_water OUTPUT.png
#include <cstdio>

#include "poligon/render/renderer.h"

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: example_water OUTPUT.png\n");
        return 2;
    }
    const glm::mat4 identity(1.0f);
    poligon::RendererConfig config;
    config.width = 640;
    config.height = 360;
    config.headless = true;
    config.post.exposure = 0.6f;
    poligon::Renderer renderer(config);
    const poligon::MeshHandle plane = renderer.CreatePlaneMesh();
    const poligon::MeshHandle cube = renderer.CreateCubeMesh();

    poligon::Camera camera;
    camera.position = {0.0f, 1.8f, 14.0f};
    camera.target = {0.0f, 0.0f, 0.0f};
    poligon::DirectionalLight sun;
    sun.direction = {-0.4f, -0.5f, -0.75f};
    sun.color = {2.6f, 2.45f, 2.2f, 1.0f};
    poligon::Environment environment;
    environment.draw_sky = true;
    environment.sky_light = true;
    environment.wind_strength = 0.6f;
    environment.wind_time = 3.0f;  // the game's own time; ripples move with it

    poligon::Material sand;
    sand.color = {0.75f, 0.68f, 0.5f, 1.0f};
    poligon::WaterMaterial water;  // the defaults: clear lake water

    renderer.BeginFrame(camera, sun, environment);
    // The bottom: a slope from the shore (in front of the camera) down to 3 m deep.
    renderer.DrawMesh(plane, glm::translate(identity, {0.0f, -1.5f, 0.0f}) * glm::rotate(identity, 0.15f, {1.0f, 0.0f, 0.0f}) *
                                 glm::scale(identity, {60.0f, 1.0f, 30.0f}),
                      sand);
    renderer.DrawMesh(cube, glm::translate(identity, {-3.0f, 1.0f, -6.0f}) * glm::scale(identity, {0.3f, 5.0f, 0.3f}),
                      poligon::Color{0.8f, 0.1f, 0.1f, 1.0f});
    renderer.DrawMesh(cube, glm::translate(identity, {0.0f, 2.0f, -16.0f}) * glm::scale(identity, {40.0f, 6.0f, 1.0f}),
                      poligon::Color{0.9f, 0.9f, 0.85f, 1.0f});
    // The water surface: a flat mesh at height 0.
    renderer.DrawWater(plane, glm::scale(identity, {60.0f, 1.0f, 30.0f}), water);
    renderer.EndFrame();

    if (!renderer.SaveFrameToPng(argv[1])) {
        std::fprintf(stderr, "cannot write %s\n", argv[1]);
        return 1;
    }
    std::printf("saved %s\n", argv[1]);
    return 0;
}

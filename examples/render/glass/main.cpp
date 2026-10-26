// Example: glass. Blended materials (Material::blend = BlendMode::Blend) in sunlight: three
// colored panes that overlap, a glass block, and a window in a wall, in front of solid
// boxes, with light fog. Blended objects are drawn after everything solid, farthest first,
// and cast no shadow.
// Usage: example_glass OUTPUT.png
#include <cstdio>

#include "poligon/render/renderer.h"

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: example_glass OUTPUT.png\n");
        return 2;
    }
    poligon::RendererConfig config;
    config.width = 960;
    config.height = 540;
    config.headless = true;
    poligon::Renderer renderer(config);
    const poligon::MeshHandle cube = renderer.CreateCubeMesh();
    const poligon::MeshHandle plane = renderer.CreatePlaneMesh();

    poligon::DirectionalLight sun;
    sun.direction = {-0.5f, -0.6f, -0.4f};
    poligon::Environment environment;
    environment.draw_sky = true;
    environment.fog_density = 0.004f;

    poligon::Camera camera;
    camera.position = {0.0, 2.0, 9.0};
    camera.target = {0.0, 1.2, 0.0};

    const glm::quat level(1.0f, 0.0f, 0.0f, 0.0f);
    const glm::quat standing = glm::angleAxis(glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    poligon::Material glass;
    glass.blend = poligon::BlendMode::Blend;
    glass.roughness = 0.05f;
    glass.double_sided = true;

    renderer.BeginFrame(camera, sun, environment);
    renderer.DrawMesh(plane, poligon::Transform(glm::dvec3(0.0), level, glm::vec3(60.0f)), poligon::Color{0.45f, 0.45f, 0.4f, 1.0f});
    // Solid things to look through the glass at.
    renderer.DrawMesh(cube, poligon::Transform({-3.0, 1.0, -4.0}, level, glm::vec3(2.0f)), poligon::Color{0.8f, 0.7f, 0.2f, 1.0f});
    renderer.DrawMesh(cube, poligon::Transform({0.5, 1.5, -6.0}, level, {1.0f, 3.0f, 1.0f}), poligon::Color{0.2f, 0.5f, 0.8f, 1.0f});
    renderer.DrawMesh(cube, poligon::Transform({3.5, 0.75, -3.0}, level, glm::vec3(1.5f)), poligon::Color{0.8f, 0.3f, 0.3f, 1.0f});
    // A wall with a window: two pillars and a lintel, a pale pane between them.
    renderer.DrawMesh(cube, poligon::Transform({1.6, 1.5, -1.0}, level, {0.4f, 3.0f, 0.3f}), poligon::Color{0.6f, 0.55f, 0.5f, 1.0f});
    renderer.DrawMesh(cube, poligon::Transform({4.4, 1.5, -1.0}, level, {0.4f, 3.0f, 0.3f}), poligon::Color{0.6f, 0.55f, 0.5f, 1.0f});
    renderer.DrawMesh(cube, poligon::Transform({3.0, 3.2, -1.0}, level, {3.2f, 0.4f, 0.3f}), poligon::Color{0.6f, 0.55f, 0.5f, 1.0f});
    glass.color = {0.85f, 0.95f, 1.0f, 0.25f};
    renderer.DrawMesh(plane, poligon::Transform({3.0, 1.5, -1.0}, standing, {2.4f, 1.0f, 3.0f}), glass);
    // Three colored panes, overlapping, nearest drawn first: the renderer sorts them.
    glass.color = {0.1f, 0.3f, 1.0f, 0.5f};
    renderer.DrawMesh(plane, poligon::Transform({-0.6, 1.3, 2.0}, standing, glm::vec3(1.6f)), glass);
    glass.color = {0.1f, 1.0f, 0.2f, 0.5f};
    renderer.DrawMesh(plane, poligon::Transform({-1.3, 1.0, 1.3}, standing, glm::vec3(1.6f)), glass);
    glass.color = {1.0f, 0.15f, 0.1f, 0.5f};
    renderer.DrawMesh(plane, poligon::Transform({-2.0, 1.6, 0.6}, standing, glm::vec3(1.6f)), glass);
    // A block of amber glass.
    glass.color = {1.0f, 0.6f, 0.1f, 0.4f};
    glass.double_sided = false;
    renderer.DrawMesh(cube, poligon::Transform({1.2, 0.5, 2.5}, glm::angleAxis(0.5f, glm::vec3(0.0f, 1.0f, 0.0f)), glm::vec3(1.0f)), glass);
    renderer.EndFrame();

    if (!renderer.SaveFrameToPng(argv[1])) {
        std::fprintf(stderr, "cannot write %s\n", argv[1]);
        return 1;
    }
    std::printf("saved %s\n", argv[1]);
    return 0;
}

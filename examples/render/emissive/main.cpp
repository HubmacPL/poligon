// Example: surfaces that give off light (a lantern's glass, embers) at dusk. Emission is
// added after lighting, so it shows in the dark, and bloom spreads a glow around it.
// Usage: example_emissive OUTPUT.png
#include <cstdio>

#include "poligon/render/renderer.h"

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: example_emissive OUTPUT.png\n");
        return 2;
    }

    poligon::RendererConfig config;
    config.width = 640;
    config.height = 360;
    config.headless = true;
    config.post.bloom = true;
    config.post.bloom_strength = 0.08f;
    poligon::Renderer renderer(config);
    const poligon::MeshHandle cube = renderer.CreateCubeMesh();
    const poligon::MeshHandle plane = renderer.CreatePlaneMesh();

    poligon::Camera camera;
    camera.position = {3.0f, 2.0f, 4.0f};
    camera.target = {0.0f, 0.5f, 0.0f};
    // Dusk: a weak, low sun and little ambient light.
    poligon::DirectionalLight dusk;
    dusk.direction = {-1.0f, -0.15f, -0.3f};
    dusk.color = {0.08f, 0.05f, 0.04f, 1.0f};
    dusk.ambient = {0.02f, 0.025f, 0.04f, 1.0f};

    // The lantern: a warm glowing box. Its light is emissive * emissive_strength; a texture,
    // if given (emissive_texture, a color texture), multiplies it.
    poligon::Material glass;
    glass.color = {0.9f, 0.8f, 0.6f, 1.0f};
    glass.emissive = {1.0f, 0.6f, 0.25f, 1.0f};
    glass.emissive_strength = 4.0f;
    // Embers: a faint red glow on a dark material.
    poligon::Material embers;
    embers.color = {0.05f, 0.05f, 0.05f, 1.0f};
    embers.emissive = {1.0f, 0.15f, 0.02f, 1.0f};
    embers.emissive_strength = 1.5f;

    renderer.BeginFrame(camera, dusk);
    const glm::mat4 identity(1.0f);
    renderer.DrawMesh(plane, glm::dvec3(0.0), glm::scale(identity, {10.0f, 1.0f, 10.0f}), poligon::Color{0.4f, 0.4f, 0.4f, 1.0f});
    renderer.DrawMesh(cube, glm::dvec3(0.0), glm::translate(identity, {0.0f, 0.6f, 0.0f}) * glm::scale(identity, {0.4f, 0.6f, 0.4f}), glass);
    renderer.DrawMesh(cube, glm::dvec3(0.0), glm::translate(identity, {1.2f, 0.05f, 0.8f}) * glm::scale(identity, {0.6f, 0.1f, 0.6f}), embers);
    renderer.EndFrame();

    if (!renderer.SaveFrameToPng(argv[1])) {
        std::fprintf(stderr, "cannot write %s\n", argv[1]);
        return 1;
    }
    std::printf("saved %s\n", argv[1]);
    return 0;
}

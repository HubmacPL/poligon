// Example: wind. The frame's wind (direction, strength, time) is set in Environment; each
// material says how much its meshes bend with it and how much their vertices flutter.
// Here a row of "trees" (a trunk with a leafy block) and a field of grass blades lean in a
// gust; shadows move with them because the shadow pass uses the same vertex shaders.
// Usage: example_wind OUTPUT.png
#include <cstdio>
#include <vector>

#include "poligon/renderer.h"

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: example_wind OUTPUT.png\n");
        return 2;
    }
    const glm::mat4 identity(1.0f);

    poligon::RendererConfig config;
    config.width = 640;
    config.height = 360;
    config.headless = true;
    config.post.exposure = 0.5f;
    poligon::Renderer renderer(config);
    const poligon::MeshHandle cube = renderer.CreateCubeMesh();
    const poligon::MeshHandle plane = renderer.CreatePlaneMesh();

    // A grass blade: a thin upright quad, 0.6 m tall, with its foot at the origin.
    const poligon::MeshVertex blade_vertices[4] = {{{-0.03f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}},
                                                   {{0.03f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},
                                                   {{0.01f, 0.6f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
                                                   {{-0.01f, 0.6f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}}};
    const std::uint32_t blade_indices[6] = {0, 1, 2, 0, 2, 3};
    const poligon::MeshHandle blade = renderer.CreateMesh(blade_vertices, 4, blade_indices, 6);

    // Materials: how much each kind of thing gives to the wind.
    poligon::Material bark;
    bark.color = {0.35f, 0.24f, 0.15f, 1.0f};
    bark.wind_bend = 1.0f;  // a tree leans a little ...
    poligon::Material leaves = bark;
    leaves.color = {0.20f, 0.45f, 0.18f, 1.0f};
    leaves.wind_flutter = 1.0f;  // ... and its leaves shiver
    poligon::Material grass;
    grass.color = {0.30f, 0.55f, 0.20f, 1.0f};
    grass.double_sided = true;
    grass.wind_bend = 6.0f;  // short plants bend much more per meter of height
    grass.wind_flutter = 1.5f;

    // The field of grass is drawn as instances; their transforms place each blade's foot.
    std::vector<glm::mat4> blades;
    for (int row = 0; row < 40; ++row) {
        for (int column = 0; column < 80; ++column) {
            const float x = (static_cast<float>(column) - 40.0f) * 0.5f + static_cast<float>((row * 7 + column * 3) % 5) * 0.1f;
            const float z = -static_cast<float>(row) * 0.5f - static_cast<float>((row * 3 + column * 11) % 4) * 0.1f;
            const float turn = static_cast<float>((row * 13 + column * 5) % 12) * 0.5f;
            blades.push_back(glm::translate(identity, {x, 0.0f, z}) * glm::rotate(identity, turn, {0.0f, 1.0f, 0.0f}));
        }
    }

    poligon::Camera camera;
    camera.position = {0.0f, 2.5f, 8.0f};
    camera.target = {0.0f, 2.0f, -10.0f};
    poligon::DirectionalLight sun;
    sun.direction = {-0.5f, -1.0f, -0.4f};
    sun.color = {2.6f, 2.45f, 2.2f, 1.0f};
    poligon::Environment environment;
    environment.draw_sky = true;
    environment.sky_light = true;
    environment.shadow_distance = 40.0f;
    environment.wind_direction = {1.0f, 0.0f};  // blowing towards +x
    environment.wind_strength = 1.0f;
    environment.wind_time = 2.0f;  // a game gives its own running time here, every frame

    renderer.BeginFrame(camera, sun, environment);
    renderer.DrawMesh(plane, glm::scale(identity, {200.0f, 1.0f, 200.0f}), poligon::Color{0.35f, 0.42f, 0.25f, 1.0f});
    for (int i = 0; i < 5; ++i) {
        const glm::vec3 foot{(static_cast<float>(i) - 2.0f) * 6.0f, 0.0f, -14.0f};
        // The model's origin is the foot the tree bends about, so the trunk and the crown
        // are placed relative to it.
        const glm::mat4 at_foot = glm::translate(identity, foot);
        renderer.DrawMesh(cube, at_foot * glm::translate(identity, {0.0f, 3.0f, 0.0f}) * glm::scale(identity, {0.5f, 6.0f, 0.5f}), bark);
        renderer.DrawMesh(cube, at_foot * glm::translate(identity, {0.0f, 7.0f, 0.0f}) * glm::scale(identity, glm::vec3(3.5f)), leaves);
    }
    renderer.DrawMeshInstances(blade, grass, blades.data(), static_cast<int>(blades.size()));
    renderer.EndFrame();

    if (!renderer.SaveFrameToPng(argv[1])) {
        std::fprintf(stderr, "cannot write %s\n", argv[1]);
        return 1;
    }
    std::printf("saved %s\n", argv[1]);
    return 0;
}

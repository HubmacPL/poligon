// Example: many copies of a mesh in one draw call, and the vegetation system that picks a
// level of detail for every plant by its distance.
//
// The "trees" are made of cubes: a trunk and a crown. Close ones have a three-part crown,
// distant ones a single block, so the picture shows where the level changes.
// Usage: example_vegetation OUTPUT.png
#include <cstdio>
#include <vector>

#include "poligon/render/renderer.h"
#include "poligon/world/vegetation.h"

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: example_vegetation OUTPUT.png\n");
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

    poligon::Material bark;
    bark.color = {0.35f, 0.24f, 0.15f, 1.0f};
    poligon::Material leaves;
    leaves.color = {0.20f, 0.45f, 0.18f, 1.0f};
    poligon::Material far_leaves;
    far_leaves.color = {0.25f, 0.50f, 0.25f, 1.0f};

    // A part is a mesh, a material and where it sits relative to the plant's foot.
    const poligon::VegetationPart trunk{cube, bark, glm::translate(identity, {0.0f, 1.5f, 0.0f}) * glm::scale(identity, {0.4f, 3.0f, 0.4f})};
    const auto crown = [&](float y, float size, const poligon::Material& material) {
        return poligon::VegetationPart{cube, material, glm::translate(identity, {0.0f, y, 0.0f}) * glm::scale(identity, glm::vec3(size))};
    };

    // A species: its looks from near to far, each with the distance up to which it is used.
    poligon::VegetationSpecies tree;
    tree.levels.resize(2);
    tree.levels[0].parts = {trunk, crown(3.2f, 2.2f, leaves), crown(4.6f, 1.6f, leaves), crown(5.6f, 1.0f, leaves)};
    tree.levels[0].max_distance = 25.0f;
    tree.levels[1].parts = {trunk, crown(4.2f, 2.4f, far_leaves)};
    tree.levels[1].max_distance = 200.0f;  // beyond this a tree is not drawn at all
    tree.shadow_distance = 60.0f;          // beyond this it casts no shadow
    tree.shadow_level_bias = 1;            // shadows always use the simple level

    // Plants are placed once. The world is cut into cells of 32 m behind the scenes.
    poligon::Vegetation vegetation;
    const int tree_species = vegetation.AddSpecies(tree);
    for (int row = 0; row < 30; ++row) {
        for (int column = 0; column < 30; ++column) {
            // A grid, pushed about a little so that it does not look planted.
            const float x = (static_cast<float>(column) - 15.0f) * 6.0f + static_cast<float>((row * 7 + column * 13) % 5);
            const float z = -static_cast<float>(row) * 6.0f - static_cast<float>((row * 11 + column * 3) % 4);
            const float size = 0.8f + 0.1f * static_cast<float>((row + column * 5) % 5);
            vegetation.AddInstance(tree_species, glm::translate(identity, {x, 0.0f, z}) * glm::scale(identity, glm::vec3(size)));
        }
    }

    poligon::Camera camera;
    camera.position = {0.0f, 7.0f, 12.0f};
    camera.target = {0.0f, 3.0f, -20.0f};
    poligon::DirectionalLight sun;
    sun.color = {2.6f, 2.45f, 2.2f, 1.0f};
    poligon::Environment environment;
    environment.draw_sky = true;
    environment.sky_light = true;
    environment.shadow_distance = 60.0f;
    environment.fog_density = 0.004f;

    renderer.BeginFrame(camera, sun, environment);
    renderer.DrawMesh(plane, glm::scale(identity, {600.0f, 1.0f, 600.0f}), poligon::Color{0.35f, 0.42f, 0.25f, 1.0f});
    // Every frame: sort the plants into levels by distance and hand them to the renderer.
    vegetation.Draw(renderer, camera);
    renderer.EndFrame();

    const poligon::VegetationStats& stats = vegetation.GetStats();
    const poligon::FrameStats frame = renderer.GetFrameStats();
    std::printf("%d trees: %d near, %d far; %d of %d cells skipped\n", stats.instances_total, stats.instances_per_level[0],
                stats.instances_per_level[1], stats.cells_skipped, stats.cells_total);
    std::printf("%d instances in view, drawn with %d draw calls in all passes\n", frame.instances_drawn, frame.instance_draw_calls);

    if (!renderer.SaveFrameToPng(argv[1])) {
        std::fprintf(stderr, "cannot write %s\n", argv[1]);
        return 1;
    }
    std::printf("saved %s\n", argv[1]);
    return 0;
}

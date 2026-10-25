// Example: one object drawn with simpler meshes the farther it is (levels of detail), as an
// animal far away in the field. Here the levels are a cube and a coarse pyramid-like box, so
// the switch can be seen; the real use is the simpler levels a cooked model carries
// (MakeMeshLodGroup in poligon/assets/assets.h). Three copies stand at 6, 25 and 70 m.
// Usage: example_mesh_lod OUTPUT.png
#include <cstdio>

#include "poligon/render/renderer.h"

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: example_mesh_lod OUTPUT.png\n");
        return 2;
    }
    poligon::RendererConfig config;
    config.width = 640;
    config.height = 360;
    config.headless = true;
    config.clear_color = {0.55f, 0.65f, 0.8f, 1.0f};
    poligon::Renderer renderer(config);
    const poligon::MeshHandle plane = renderer.CreatePlaneMesh();

    // Level 1: a box with a pointed top, 5 vertices instead of the cube's 24.
    const poligon::MeshVertex coarse_vertices[5] = {{{-0.5f, -0.5f, -0.5f}, {-0.6f, 0.5f, -0.6f}, {0.0f, 0.0f}},
                                                    {{0.5f, -0.5f, -0.5f}, {0.6f, 0.5f, -0.6f}, {1.0f, 0.0f}},
                                                    {{0.5f, -0.5f, 0.5f}, {0.6f, 0.5f, 0.6f}, {1.0f, 1.0f}},
                                                    {{-0.5f, -0.5f, 0.5f}, {-0.6f, 0.5f, 0.6f}, {0.0f, 1.0f}},
                                                    {{0.0f, 0.5f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.5f, 0.5f}}};
    const std::uint32_t coarse_indices[12] = {0, 4, 1, 1, 4, 2, 2, 4, 3, 3, 4, 0};

    // Levels nearest first: the cube to 15 m, the coarse box to 50 m, then nothing. Each
    // dissolves into the next over the last tenth of its range.
    poligon::MeshLodGroup group;
    group.levels = {{renderer.CreateCubeMesh(), 15.0f}, {renderer.CreateMesh(coarse_vertices, 5, coarse_indices, 12), 50.0f}};
    group.fade_width = 0.1f;

    poligon::Camera camera;
    camera.position = {0.0f, 1.5f, 0.0f};
    camera.target = {0.0f, 1.0f, -10.0f};
    camera.far_plane = 200.0f;
    renderer.BeginFrame(camera, poligon::DirectionalLight{});
    const glm::mat4 identity(1.0f);
    renderer.DrawMesh(plane, glm::dvec3(0.0), glm::scale(identity, {200.0f, 1.0f, 200.0f}), poligon::Color{0.35f, 0.45f, 0.25f, 1.0f});
    poligon::Material fur;
    fur.color = {0.55f, 0.35f, 0.2f, 1.0f};
    for (const float distance : {6.0f, 25.0f, 70.0f}) {
        // DrawMesh with a group picks the level by the distance to the transform's origin and
        // returns it (-1: too far, nothing drawn).
        const int level = renderer.DrawMesh(group, glm::dvec3(0.0), glm::translate(identity, {distance * 0.15f, 0.5f, -distance}), fur);
        std::printf("at %.0f m: level %d\n", distance, level);
    }
    renderer.EndFrame();

    if (!renderer.SaveFrameToPng(argv[1])) {
        std::fprintf(stderr, "cannot write %s\n", argv[1]);
        return 1;
    }
    const poligon::FrameStats stats = renderer.GetFrameStats();
    std::printf("saved %s; draws per level: %d %d\n", argv[1], stats.mesh_lod_draws[0], stats.mesh_lod_draws[1]);
    return 0;
}

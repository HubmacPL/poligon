// Example: an impostor, a far-away stand-in for a model. The model is photographed from many
// directions once (BakeImpostor); far away it is drawn as one card showing those pictures.
// Here the vegetation system switches each "tree" from its meshes to the impostor at 40 m,
// fading from one to the other, so a long avenue costs little.
//
// The "tree" is made of cubes: a trunk and a three-part crown.
// Usage: example_impostor OUTPUT.png
#include <cstdio>
#include <vector>

#include "poligon/render/renderer.h"
#include "poligon/world/vegetation.h"

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: example_impostor OUTPUT.png\n");
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
    const auto box = [&](const poligon::Material& material, glm::vec3 middle, glm::vec3 size) {
        return poligon::VegetationPart{cube, material, glm::translate(identity, middle) * glm::scale(identity, size)};
    };
    poligon::VegetationLevel tree_meshes;
    tree_meshes.parts = {box(bark, {0.0f, 1.5f, 0.0f}, {0.4f, 3.0f, 0.4f}), box(leaves, {0.0f, 3.2f, 0.0f}, glm::vec3(2.2f)),
                         box(leaves, {0.4f, 4.6f, 0.2f}, glm::vec3(1.6f)), box(leaves, {0.0f, 5.6f, -0.2f}, glm::vec3(1.0f))};
    tree_meshes.max_distance = 40.0f;

    // Photograph the tree: 8 x 8 directions over the half sphere, 64 pixels each. A real
    // game does this once with tools/impostor_baker and keeps the result in files
    // (poligon/render/impostor.h); here it is quick enough to do on every run.
    std::vector<poligon::ImpostorPart> parts;
    for (const poligon::VegetationPart& part : tree_meshes.parts) {
        parts.push_back({part.mesh, part.material, part.local});
    }
    poligon::Impostor pictures;
    renderer.BakeImpostor(parts.data(), static_cast<int>(parts.size()), 8, 64, pictures);

    poligon::VegetationLevel tree_card;
    tree_card.impostor = renderer.CreateImpostor(pictures);
    tree_card.max_distance = 400.0f;

    poligon::VegetationSpecies tree;
    tree.levels = {tree_meshes, tree_card};
    tree.shadow_distance = 40.0f;
    tree.fade_width = 0.15f;  // the last 15% of the mesh range (34 to 40 m) fades into the card

    poligon::Vegetation vegetation;
    const int tree_species = vegetation.AddSpecies(tree);
    // Two rows of trees along an avenue, each turned and sized a little differently.
    for (int i = 0; i < 60; ++i) {
        for (const float side : {-5.0f, 5.0f}) {
            const float z = -static_cast<float>(i) * 6.0f;
            const float turn = static_cast<float>((i * 7 + (side > 0.0f ? 3 : 0)) % 12) * 0.5f;
            const float size = 0.85f + 0.05f * static_cast<float>((i * 3) % 6);
            vegetation.AddInstance(tree_species, glm::translate(identity, {side, 0.0f, z}) *
                                                     glm::rotate(identity, turn, {0.0f, 1.0f, 0.0f}) *
                                                     glm::scale(identity, glm::vec3(size)));
        }
    }

    poligon::Camera camera;
    camera.position = {0.0f, 2.0f, 8.0f};
    camera.target = {0.0f, 2.5f, -40.0f};
    poligon::DirectionalLight sun;
    sun.direction = {-0.5f, -1.0f, -0.4f};
    sun.color = {2.6f, 2.45f, 2.2f, 1.0f};
    poligon::Environment environment;
    environment.draw_sky = true;
    environment.sky_light = true;
    environment.shadow_distance = 40.0f;
    environment.fog_density = 0.004f;

    renderer.BeginFrame(camera, sun, environment);
    renderer.DrawMesh(plane, glm::scale(identity, {800.0f, 1.0f, 800.0f}), poligon::Color{0.35f, 0.42f, 0.25f, 1.0f});
    vegetation.Draw(renderer, camera);
    renderer.EndFrame();

    const poligon::VegetationStats& stats = vegetation.GetStats();
    std::printf("%d trees: %d as meshes, %d as impostors, %d fading\n", stats.instances_total, stats.instances_per_level[0],
                stats.instances_per_level[1], stats.instances_fading);
    if (!renderer.SaveFrameToPng(argv[1])) {
        std::fprintf(stderr, "cannot write %s\n", argv[1]);
        return 1;
    }
    std::printf("saved %s\n", argv[1]);
    return 0;
}

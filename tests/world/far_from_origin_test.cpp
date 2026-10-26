// Checks that world positions in double and the camera-relative renderer make a scene far
// from the origin look the same as near it: one scene with terrain, meshes, plants, water,
// a decal, particles and a skinned mesh is drawn at the origin and again moved by
// (20480, 0, 20480), and the two pictures are saved for image_compare (see CMakeLists.txt).
// The wind blows and the water has waves. Their noise repeats every 4096 m (see
// docs/en/rendering.md), so the shift is five such squares, about 20 km: a shift that is not a
// multiple of 4096 m shows other gusts and other waves. Clouds are off: they are fixed to the
// world, and 29 km away the sky holds other clouds.
// Usage: far_from_origin_test d3d12|vulkan OUTPUT_DIRECTORY
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "poligon/core/image.h"
#include "poligon/render/decal.h"
#include "poligon/render/renderer.h"
#include "poligon/world/height_map.h"
#include "poligon/world/particles.h"
#include "poligon/world/terrain.h"
#include "poligon/world/vegetation.h"
#include "test_support.h"

namespace {

// A flat bar from x = 0 to x = 4 that bends at x = 2: the far half follows joint 1.
void MakeBar(std::vector<poligon::MeshVertex>& vertices, std::vector<poligon::SkinVertex>& skin,
             std::vector<std::uint32_t>& indices) {
    const int pieces = 8;
    for (int i = 0; i <= pieces; ++i) {
        const float x = 4.0f * static_cast<float>(i) / pieces;
        for (const float z : {0.25f, -0.25f}) {
            poligon::MeshVertex vertex;
            vertex.position = {x, 0.0f, z};
            vertex.normal = {0.0f, 1.0f, 0.0f};
            vertex.uv = {x / 4.0f, z + 0.5f};
            vertices.push_back(vertex);
            poligon::SkinVertex follows;
            follows.joints[0] = 0;
            follows.joints[1] = 1;
            follows.weights[0] = x < 2.0f ? 1.0f : x > 2.0f ? 0.0f : 0.5f;
            follows.weights[1] = 1.0f - follows.weights[0];
            skin.push_back(follows);
        }
    }
    for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(pieces); ++i) {
        const std::uint32_t corner = i * 2;
        indices.insert(indices.end(), {corner, corner + 2, corner + 1, corner + 1, corner + 2, corner + 3});
    }
}

// Draws the scene around `center` and saves it to `path`.
bool DrawScene(poligon::GraphicsBackend backend, const glm::dvec3& center, const std::string& path) {
    poligon::RendererConfig config;
    config.width = 320;
    config.height = 180;
    config.headless = true;
    config.enable_validation = true;
    config.backend = backend;
    poligon::Renderer renderer(config);
    const poligon::MeshHandle cube = renderer.CreateCubeMesh();
    const poligon::MeshHandle plane = renderer.CreatePlaneMesh();
    std::vector<poligon::MeshVertex> vertices;
    std::vector<poligon::SkinVertex> skin;
    std::vector<std::uint32_t> indices;
    MakeBar(vertices, skin, indices);
    const poligon::MeshHandle bar = renderer.CreateSkinnedMesh(vertices.data(), skin.data(), static_cast<int>(vertices.size()),
                                                               indices.data(), static_cast<int>(indices.size()));

    // Gentle hills 128 m across, centered on `center`.
    const poligon::HeightMap map = poligon::MakeNoiseHeightMap(129, 129, 1.0f, 7u, 40.0f, 3.0f);
    poligon::TerrainConfig terrain_config;
    // A checker texture repeating every 4 m. Textures are fixed to the world, so the shift is a
    // whole number of repeats (20480 = 5120 x 4), and the picture must not change. The repeat
    // works out in double; in float at 20 km it would be off by about 2 mm.
    const std::uint8_t grass[4] = {70, 110, 50, 255};
    const std::uint8_t soil[4] = {110, 90, 60, 255};
    const poligon::Image checker = poligon::MakeCheckerImage(64, 64, 16, grass, soil);
    terrain_config.material.colors = renderer.CreateTextureArray(&checker, 1);
    terrain_config.material.layer_count = 1;
    terrain_config.material.tile_sizes[0] = 4.0f;
    terrain_config.origin = center + glm::dvec3(-64.0, 0.0, -64.0);
    poligon::Terrain terrain(renderer, map, terrain_config);
    const auto ground = [&](double x, double z) { return terrain.GetHeight(center.x + x, center.z + z); };
    const auto at = [&](double x, double lift, double z) { return center + glm::dvec3(x, ground(x, z) + lift, z); };

    poligon::Material red;
    red.color = {0.8f, 0.15f, 0.1f, 1.0f};
    red.roughness = 0.8f;
    poligon::Material green = red;
    green.color = {0.2f, 0.6f, 0.2f, 1.0f};
    poligon::Material white = red;
    // The plants move in the wind: lean and flutter.
    green.wind_bend = 1.0f;
    green.wind_flutter = 1.0f;
    white.color = {0.9f, 0.9f, 0.9f, 1.0f};

    // Plants: a green box species, placed in a ring.
    poligon::VegetationSpecies species;
    species.levels.resize(1);
    species.levels[0].parts.push_back({cube, green, glm::translate(glm::mat4(1.0f), {0.0f, 1.0f, 0.0f}) *
                                                        glm::scale(glm::mat4(1.0f), {0.8f, 2.0f, 0.8f})});
    species.levels[0].max_distance = 200.0f;
    poligon::Vegetation vegetation(32.0f);
    const int plant = vegetation.AddSpecies(species);
    for (int i = 0; i < 12; ++i) {
        const double angle = static_cast<double>(i) * 0.5236;
        const double x = 9.0 * std::cos(angle);
        const double z = 9.0 * std::sin(angle) - 4.0;
        vegetation.AddInstance(plant, poligon::Transform(at(x, 0.0, z), glm::angleAxis(static_cast<float>(angle), glm::vec3(0.0f, 1.0f, 0.0f))));
    }

    // Particles rising from a point, simulated for one second.
    poligon::ParticleEmitterSettings smoke;
    smoke.rate = 80.0f;
    smoke.lifetime = 1.5f;
    smoke.box_size = {0.4f, 0.0f, 0.4f};
    smoke.velocity = {0.0f, 1.5f, 0.0f};
    smoke.velocity_variation = {0.3f, 0.3f, 0.3f};
    smoke.start_size = 0.15f;
    smoke.end_size = 0.4f;
    smoke.start_color = {0.2f, 0.4f, 1.0f, 1.0f};
    poligon::ParticleEmitter emitter(smoke, 5u);
    emitter.SetPosition(at(3.0, 0.5, 3.0));
    for (int tick = 0; tick < 60; ++tick) {
        emitter.Update(1.0f / 60.0f);
    }

    poligon::Camera camera;
    camera.position = at(0.0, 7.0, 16.0);
    camera.target = at(0.0, 0.0, -2.0);
    poligon::DirectionalLight sun;
    sun.direction = {-0.4f, -0.7f, -0.3f};
    sun.color = {1.2f, 1.15f, 1.05f, 1.0f};
    sun.ambient = {0.2f, 0.22f, 0.25f, 1.0f};
    poligon::Environment environment;
    environment.draw_sky = true;
    environment.shadow_distance = 60.0f;
    environment.fog_density = 0.004f;
    // A fresh wind, a few seconds into it: the plants lean and their leaves shiver.
    environment.wind_strength = 1.0f;
    environment.wind_time = 3.7f;
    poligon::WaterMaterial water;
    poligon::DecalMaterial blood;
    blood.color = {0.5f, 0.0f, 0.0f, 1.0f};
    blood.roughness = 1.0f;
    blood.edge_fade = 0.1f;

    renderer.BeginFrame(camera, sun, environment);
    terrain.Draw(camera);
    vegetation.Draw(renderer, camera);
    renderer.DrawMesh(cube, poligon::Transform(at(-2.0, 0.6, 2.0), glm::angleAxis(0.4f, glm::vec3(0.0f, 1.0f, 0.0f)), glm::vec3(1.2f)), red);
    renderer.DrawMesh(cube, poligon::Transform(at(4.0, 0.4, -3.0), glm::angleAxis(1.1f, glm::vec3(0.3f, 0.9f, 0.1f)), glm::vec3(0.8f)), white);
    // Water: a still pool over the low part of the left side, half a meter above the lowest ground there.
    double lowest = 1e9;
    for (int z = -10; z <= 10; ++z) {
        for (int x = -16; x <= -4; ++x) {
            lowest = std::fmin(lowest, ground(x, z));
        }
    }
    renderer.DrawWater(plane, center + glm::dvec3(-10.0, lowest + 0.5, 0.0), glm::scale(glm::mat4(1.0f), {12.0f, 1.0f, 20.0f}), water);
    renderer.DrawDecal(poligon::Transform(at(0.5, 0.0, 4.0), glm::angleAxis(0.7f, glm::vec3(0.0f, 1.0f, 0.0f)), {2.0f, 2.0f, 1.4f}), blood);
    // The skinned bar bent a quarter turn at its middle.
    const glm::mat4 joints[2] = {glm::mat4(1.0f), glm::translate(glm::mat4(1.0f), {2.0f, 0.0f, 0.0f}) *
                                                      glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), {0.0f, 1.0f, 0.0f}) *
                                                      glm::translate(glm::mat4(1.0f), {-2.0f, 0.0f, 0.0f})};
    renderer.DrawSkinnedMesh(bar, poligon::Transform(at(-5.0, 0.3, 6.0)), white, joints, 2);
    renderer.DrawParticles(poligon::TextureHandle{}, emitter.GetOrigin(), emitter.GetParticles().data(), emitter.GetParticleCount());
    renderer.EndFrame();
    return renderer.SaveFrameToPng(path.c_str());
}

} // namespace

int main(int argc, char** argv) {
    using test::Expect;

    poligon::GraphicsBackend backend = poligon::GraphicsBackend::Default;
    if (argc == 3 && std::strcmp(argv[1], "d3d12") == 0) {
        backend = poligon::GraphicsBackend::Direct3D12;
    } else if (argc == 3 && std::strcmp(argv[1], "vulkan") == 0) {
        backend = poligon::GraphicsBackend::Vulkan;
    } else {
        std::printf("usage: far_from_origin_test d3d12|vulkan OUTPUT_DIRECTORY\n");
        return 2;
    }
    const std::string prefix = std::string(argv[2]) + "/far_from_origin_" + argv[1];
    Expect(DrawScene(backend, glm::dvec3(0.0), prefix + "_near.png"), "the scene at the origin is drawn and saved");
    Expect(DrawScene(backend, glm::dvec3(20480.0, 0.0, 20480.0), prefix + "_far.png"), "the scene 29 km away, in the wind, is drawn and saved");
    return test::ExitCode();
}

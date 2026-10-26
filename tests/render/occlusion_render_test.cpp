// Checks occlusion culling by depth (Environment::occlusion_culling): behind a wall, a mesh and
// a row of instanced boxes are left out of the picture once the depth of two submissions
// earlier is known (FrameStats::occlusion_culled), while a box beside the wall and one in front
// of it are drawn; the picture is the same as without culling. Nothing is culled in the first
// two frames (no earlier depth yet).
// Usage: occlusion_render_test d3d12|vulkan
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "poligon/render/renderer.h"
#include "test_support.h"

int main(int argc, char** argv) {
    using test::Expect;

    poligon::GraphicsBackend backend = poligon::GraphicsBackend::Default;
    if (argc == 2 && std::strcmp(argv[1], "d3d12") == 0) {
        backend = poligon::GraphicsBackend::Direct3D12;
    } else if (argc == 2 && std::strcmp(argv[1], "vulkan") == 0) {
        backend = poligon::GraphicsBackend::Vulkan;
    } else {
        std::printf("usage: occlusion_render_test d3d12|vulkan\n");
        return 2;
    }

    const auto run = [backend](bool culling, poligon::Image& picture, std::vector<int>& culled) {
        poligon::RendererConfig config;
        config.width = 256;
        config.height = 144;
        config.headless = true;
        config.enable_validation = true;
        config.backend = backend;
        poligon::Renderer renderer(config);
        const poligon::MeshHandle cube = renderer.CreateCubeMesh();
        poligon::DirectionalLight sun;
        poligon::Environment environment;
        environment.occlusion_culling = culling;
        poligon::Camera camera;
        camera.position = {0.0, 1.5, 10.0};
        camera.target = {0.0, 1.5, 0.0};
        const glm::quat level(1.0f, 0.0f, 0.0f, 0.0f);
        // Behind the wall: five instanced boxes.
        std::vector<glm::mat4> hidden;
        for (int i = 0; i < 5; ++i) {
            hidden.push_back(glm::translate(glm::mat4(1.0f), glm::vec3(-2.0f + static_cast<float>(i), 1.0f, -4.0f)));
        }
        for (int frame = 0; frame < 6; ++frame) {
            renderer.BeginFrame(camera, sun, environment);
            renderer.DrawMesh(cube, poligon::Transform({0.0, 0.0, 0.0}, level, {40.0f, 0.1f, 40.0f}), poligon::Color{0.4f, 0.5f, 0.3f, 1.0f});
            // A wall 8 m wide and 4 m high, 3 m in front of the origin... at z = 0.
            renderer.DrawMesh(cube, poligon::Transform({0.0, 2.0, 0.0}, level, {8.0f, 4.0f, 0.3f}), poligon::Color{0.7f, 0.6f, 0.5f, 1.0f});
            // Behind it: a box.
            renderer.DrawMesh(cube, poligon::Transform({0.0, 1.0, -3.0}, level, glm::vec3(1.5f)), poligon::Color{1.0f, 0.0f, 0.0f, 1.0f});
            // Beside it and in front of it: seen.
            renderer.DrawMesh(cube, poligon::Transform({6.0, 1.0, -3.0}, level, glm::vec3(1.5f)), poligon::Color{0.0f, 0.0f, 1.0f, 1.0f});
            renderer.DrawMesh(cube, poligon::Transform({0.0, 0.5, 3.0}, level, glm::vec3(1.0f)), poligon::Color{0.0f, 1.0f, 0.0f, 1.0f});
            poligon::Material material;
            material.color = {1.0f, 1.0f, 0.0f, 1.0f};
            renderer.DrawMeshInstances(cube, material, glm::dvec3(0.0), hidden.data(), static_cast<int>(hidden.size()));
            renderer.EndFrame();
            culled.push_back(renderer.GetFrameStats().occlusion_culled);
        }
        renderer.CaptureFrame(picture);
    };

    poligon::Image with_culling;
    poligon::Image without_culling;
    std::vector<int> culled;
    std::vector<int> culled_off;
    run(true, with_culling, culled);
    run(false, without_culling, culled_off);
    std::printf("  culled per frame:");
    for (const int count : culled) {
        std::printf(" %d", count);
    }
    std::printf("\n");
    Expect(culled[0] == 0 && culled[1] == 0, "nothing is culled before an earlier frame's depth is known");
    Expect(culled.back() == 6, "the mesh and the five instances behind the wall are culled, nothing else");
    Expect(culled_off.back() == 0, "nothing is culled with culling off");
    int differing = 0;
    for (size_t i = 0; i < with_culling.pixels.size(); ++i) {
        differing += std::abs(static_cast<int>(with_culling.pixels[i]) - static_cast<int>(without_culling.pixels[i])) > 2 ? 1 : 0;
    }
    std::printf("  channels differing from the picture without culling: %d\n", differing);
    Expect(differing == 0, "the picture is the same as without culling");
    return test::ExitCode();
}

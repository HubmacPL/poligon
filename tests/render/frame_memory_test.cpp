// Checks that drawing frame after frame does not make the program's memory grow: a headless
// renderer draws 3 000 instances, particles, decals and text for 1200 frames in the high
// preset at 1920 x 1080 (clouds, volumetric fog, ambient occlusion, temporal anti-aliasing,
// sky light), which keeps the graphics card busier than the processor, and the process holds at most 50 MB more after frame 1200 than
// after frame 300. Without a window nothing waits for the graphics card (no swapchain), so
// the renderer itself must keep the frames in flight few.
// Usage: frame_memory_test d3d12|vulkan
#include <cstdio>
#include <cstring>
#include <vector>

#include "poligon/core/platform.h"
#include "poligon/render/decal.h"
#include "poligon/render/graphics_quality.h"
#include "poligon/render/overlay.h"
#include "poligon/render/particles.h"
#include "poligon/render/renderer.h"
#include "poligon/world/trample.h"
#include "test_support.h"

int main(int argc, char** argv) {
    using test::Expect;

    poligon::GraphicsBackend backend = poligon::GraphicsBackend::Default;
    if (argc == 2 && std::strcmp(argv[1], "d3d12") == 0) {
        backend = poligon::GraphicsBackend::Direct3D12;
    } else if (argc == 2 && std::strcmp(argv[1], "vulkan") == 0) {
        backend = poligon::GraphicsBackend::Vulkan;
    } else {
        std::printf("usage: frame_memory_test d3d12|vulkan\n");
        return 2;
    }
    poligon::RendererConfig config;
    config.width = 3840;
    config.height = 2160;
    config.headless = true;
    config.backend = backend;
    // The high preset, as forest_scene runs it: the card has far more to do than the processor.
    const poligon::GraphicsQuality high = poligon::GetGraphicsPreset(poligon::GraphicsPreset::Ultra);
    poligon::ApplyGraphicsQuality(high, config);
    poligon::Renderer renderer(config);
    renderer.PrepareClouds();
    const poligon::MeshHandle cube = renderer.CreateCubeMesh();
    const poligon::MeshHandle plane = renderer.CreatePlaneMesh();

    // 3 000 small cubes; the processor has little to do, the graphics card much.
    std::vector<glm::mat4> cubes;
    for (int z = 0; z < 15; ++z) {
        for (int x = 0; x < 200; ++x) {
            cubes.push_back(glm::translate(glm::mat4(1.0f), {static_cast<float>(x) * 1.5f - 150.0f, 0.5f, static_cast<float>(z) * 1.5f - 200.0f}));
        }
    }
    std::vector<poligon::Particle> particles(2000);
    for (size_t i = 0; i < particles.size(); ++i) {
        particles[i].position = {static_cast<float>(i % 50) * 0.3f - 7.0f, 2.0f + static_cast<float>(i / 50) * 0.1f, -5.0f};
        particles[i].size = 0.1f;
        particles[i].color = {1.0f, 1.0f, 1.0f, 0.5f};
    }
    poligon::Material grey;
    grey.color = {0.6f, 0.6f, 0.6f, 1.0f};
    poligon::DecalMaterial mark;
    mark.color = {0.4f, 0.1f, 0.1f, 1.0f};
    poligon::Camera camera;
    camera.position = {0.0, 8.0, 20.0};
    camera.target = {0.0, 0.0, -30.0};
    poligon::DirectionalLight sun;
    poligon::Environment environment;
    environment.draw_sky = true;
    environment.sky_light = true;
    environment.clouds = true;
    environment.cloud_shadows = true;
    environment.volumetric_fog = true;
    environment.fog_density = 0.01f;
    environment.ambient_occlusion = true;
    poligon::PostSettings post;
    post.temporal_aa = true;
    post.bloom = true;
    post.auto_exposure = true;
    poligon::ApplyGraphicsQuality(high, environment, post);
    renderer.SetPostSettings(post);

    // A trample map that changes every frame, as a walking player's does: copied to the card
    // every frame.
    poligon::TrampleMap trample(64.0f, 256);
    renderer.SetTrampleMap(&trample);

    std::uint64_t at_300 = 0;
    std::uint64_t at_1200 = 0;
    for (int frame = 1; frame <= 1200; ++frame) {
        trample.Trample({static_cast<double>(frame % 50), 0.0}, 1.0f, 0.1f);
        renderer.BeginFrame(camera, sun, environment);
        renderer.DrawMesh(plane, poligon::Transform(glm::dvec3(0.0), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(400.0f)), grey);
        renderer.DrawMeshInstances(cube, grey, glm::dvec3(0.0), cubes.data(), static_cast<int>(cubes.size()));
        renderer.DrawParticles(poligon::TextureHandle{}, glm::dvec3(0.0), particles.data(), static_cast<int>(particles.size()));
        renderer.DrawDecal(poligon::Transform(glm::dvec3(0.0, 0.0, -5.0), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(4.0f)), mark);
        renderer.DrawText(renderer.GetBuiltinFont(), "frame memory", {10.0f, 10.0f}, 12.0f, poligon::Color{1.0f, 1.0f, 1.0f, 1.0f});
        renderer.EndFrame();
        if (frame == 300) {
            at_300 = poligon::GetProcessMemoryBytes();
        }
        if (frame == 1200) {
            at_1200 = poligon::GetProcessMemoryBytes();
        }
    }
    const double growth = (static_cast<double>(at_1200) - static_cast<double>(at_300)) / 1048576.0;
    std::printf("  process memory: %.1f MB after frame 300, %.1f MB after frame 1200 (%.3f MB per frame)\n",
                static_cast<double>(at_300) / 1048576.0, static_cast<double>(at_1200) / 1048576.0, growth / 900.0);
    Expect(at_300 > 0 && growth <= 50.0, "900 more frames make the process hold at most 50 MB more");
    return test::ExitCode();
}

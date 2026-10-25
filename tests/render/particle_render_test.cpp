// Renders particles without a window and checks pixels: a card colors its pixels, a card
// behind a wall is hidden, a card just in front of the wall is faded by its soft edge, lit
// cards take the sun's light, and additive cards add light.
// Usage: particle_render_test d3d12|vulkan
#include <cstdio>
#include <cstring>

#include "poligon/render/renderer.h"
#include "test_support.h"

namespace {

constexpr int kSize = 128;

int RedAt(const poligon::Image& image, int x, int y) {
    return image.pixels[static_cast<size_t>(y * image.width + x) * 4];
}

} // namespace

int main(int argc, char** argv) {
    using test::Expect;

    poligon::GraphicsBackend backend = poligon::GraphicsBackend::Default;
    if (argc == 2 && std::strcmp(argv[1], "d3d12") == 0) {
        backend = poligon::GraphicsBackend::Direct3D12;
    } else if (argc == 2 && std::strcmp(argv[1], "vulkan") == 0) {
        backend = poligon::GraphicsBackend::Vulkan;
    } else {
        std::printf("usage: particle_render_test d3d12|vulkan\n");
        return 2;
    }

    poligon::RendererConfig config;
    config.width = kSize;
    config.height = kSize;
    config.headless = true;
    config.enable_validation = true;
    config.backend = backend;
    config.post.tone_mapping = poligon::ToneMapping::None;
    poligon::Renderer renderer(config);
    const poligon::MeshHandle plane = renderer.CreatePlaneMesh();
    const glm::mat4 identity(1.0f);

    // A black wall facing the camera at z = 0; the camera 5 m in front of it.
    poligon::Camera camera;
    camera.position = {0.0f, 0.0f, 5.0f};
    camera.target = {0.0f, 0.0f, 0.0f};
    const glm::mat4 wall = glm::rotate(identity, glm::radians(90.0f), {1.0f, 0.0f, 0.0f}) * glm::scale(identity, {20.0f, 1.0f, 20.0f});
    poligon::Material black;
    black.color = {0.0f, 0.0f, 0.0f, 1.0f};
    black.roughness = 1.0f;
    poligon::DirectionalLight dark;
    dark.color = {0.0f, 0.0f, 0.0f, 1.0f};
    dark.ambient = {0.0f, 0.0f, 0.0f, 1.0f};
    poligon::Environment environment;
    environment.shadows = false;

    const auto draw = [&](const poligon::DirectionalLight& light, float z, const poligon::ParticleOptions& options) {
        poligon::Particle particle;
        particle.position = {0.0f, 0.0f, z};
        particle.size = 1.0f;
        particle.color = {1.0f, 1.0f, 1.0f, 1.0f};
        renderer.BeginFrame(camera, light, environment);
        renderer.DrawMesh(plane, glm::dvec3(0.0), wall, black);
        renderer.DrawParticles(poligon::TextureHandle{}, glm::dvec3(0.0), &particle, 1, options);
        renderer.EndFrame();
        poligon::Image frame;
        renderer.CaptureFrame(frame);
        return RedAt(frame, kSize / 2, kSize / 2);
    };

    poligon::ParticleOptions unlit;
    unlit.lighting = poligon::ParticleLighting::None;
    unlit.soft_distance = 0.0f;
    const int in_front = draw(dark, 1.0f, unlit);
    const int behind = draw(dark, -0.5f, unlit);
    // 0.1 m in front of the wall with a soft distance of 0.3 m: a third of the card shows,
    // 0.333 of white over black, stored as 155.
    poligon::ParticleOptions soft = unlit;
    soft.soft_distance = 0.3f;
    const int faded = draw(dark, 0.1f, soft);
    const int hard = draw(dark, 0.1f, unlit);
    // Lit simply by a sun of 0.5 and no ambient light: half of white, stored as 188.
    poligon::DirectionalLight sun;
    sun.direction = {0.0f, -1.0f, -0.2f};
    sun.color = {0.5f, 0.5f, 0.5f, 1.0f};
    sun.ambient = {0.0f, 0.0f, 0.0f, 1.0f};
    poligon::ParticleOptions lit = unlit;
    lit.lighting = poligon::ParticleLighting::Simple;
    const int lit_value = draw(sun, 1.0f, lit);
    std::printf("  card in front %d, behind the wall %d, 0.1 m from the wall soft %d / hard %d, lit by a sun of 0.5: %d\n", in_front,
                behind, faded, hard, lit_value);
    Expect(in_front == 255, "a particle colors its pixels");
    Expect(behind == 0, "a particle behind a wall is hidden");
    Expect(std::abs(faded - 155) <= 4 && hard == 255, "near the wall a particle fades by its soft distance");
    Expect(std::abs(lit_value - 188) <= 3, "a simply lit particle takes the sun's light");

    // Additive: two half-transparent cards add up (0.25 + 0.25 of white = 0.5, stored as 188).
    {
        poligon::Particle cards[2];
        for (poligon::Particle& card : cards) {
            card.position = {0.0f, 0.0f, 1.0f};
            card.size = 1.0f;
            card.color = {1.0f, 1.0f, 1.0f, 0.25f};
        }
        cards[1].position.z = 1.5f;
        poligon::ParticleOptions additive = unlit;
        additive.blend = poligon::ParticleBlend::Additive;
        renderer.BeginFrame(camera, dark, environment);
        renderer.DrawMesh(plane, glm::dvec3(0.0), wall, black);
        renderer.DrawParticles(poligon::TextureHandle{}, glm::dvec3(0.0), cards, 2, additive);
        renderer.EndFrame();
        poligon::Image frame;
        renderer.CaptureFrame(frame);
        const int sum = RedAt(frame, kSize / 2, kSize / 2);
        if (std::abs(sum - 188) > 3) {
            std::printf("  two additive cards of 0.25: %d\n", sum);
        }
        Expect(std::abs(sum - 188) <= 3, "additive particles add their light");
    }

    std::printf("particle_render_test finished\n");
    return test::ExitCode();
}

// Example: light rain around the camera and dust drifting in the sun, from two emitters. The
// game moves particles (ParticleEmitter::Update every tick, repeatably for a seed); the
// renderer only draws them (Renderer::DrawParticles). Two seconds are simulated, then one
// frame is drawn.
// Usage: example_particles OUTPUT.png
#include <cmath>
#include <cstdio>

#include "poligon/render/renderer.h"
#include "poligon/world/particles.h"

namespace {

// A round, soft-edged white dot: the picture for dust (the color comes from the particles).
poligon::Image MakeSoftDot() {
    constexpr int kSize = 32;
    poligon::Image image = poligon::MakeSolidImage(kSize, kSize, 255, 255, 255);
    for (int y = 0; y < kSize; ++y) {
        for (int x = 0; x < kSize; ++x) {
            const float u = (static_cast<float>(x) + 0.5f) / kSize * 2.0f - 1.0f;
            const float v = (static_cast<float>(y) + 0.5f) / kSize * 2.0f - 1.0f;
            const float alpha = std::fmax(0.0f, 1.0f - std::sqrt(u * u + v * v));
            image.pixels[static_cast<size_t>(y * kSize + x) * 4 + 3] = static_cast<std::uint8_t>(alpha * alpha * 255.0f);
        }
    }
    return image;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: example_particles OUTPUT.png\n");
        return 2;
    }
    poligon::RendererConfig config;
    config.width = 640;
    config.height = 360;
    config.headless = true;
    config.clear_color = {0.45f, 0.5f, 0.58f, 1.0f};
    poligon::Renderer renderer(config);
    const poligon::MeshHandle plane = renderer.CreatePlaneMesh();
    const poligon::MeshHandle cube = renderer.CreateCubeMesh();
    const poligon::TextureHandle dot = renderer.CreateTexture(MakeSoftDot());

    poligon::Camera camera;
    camera.position = {0.0f, 1.7f, 6.0f};
    camera.target = {0.0f, 1.2f, 0.0f};

    // Rain: a box 16 m wide over the camera; drops fall at about 7 m/s, slanted by the wind,
    // drawn as streaks stretched along their velocity.
    poligon::ParticleEmitterSettings rain;
    rain.rate = 1500.0f;
    rain.max_particles = 3000;
    rain.lifetime = 1.2f;
    rain.box_size = {16.0f, 1.0f, 16.0f};
    rain.velocity = {0.8f, -7.0f, 0.0f};
    rain.velocity_variation = {0.2f, 0.8f, 0.2f};
    rain.start_size = 0.01f;
    rain.end_size = 0.01f;
    rain.start_color = {0.7f, 0.75f, 0.8f, 0.5f};
    rain.end_color = {0.7f, 0.75f, 0.8f, 0.5f};
    poligon::ParticleEmitter rain_emitter(rain, 1);
    rain_emitter.SetPosition(camera.position + glm::dvec3(0.0, 6.0, -4.0));

    // Dust: slow motes near a ray of sun, lit by it and shaded where the block's shadow falls.
    poligon::ParticleEmitterSettings dust;
    dust.rate = 120.0f;
    dust.lifetime = 4.0f;
    dust.box_size = {3.0f, 2.0f, 2.0f};
    dust.velocity_variation = {0.08f, 0.05f, 0.08f};
    dust.start_size = 0.04f;
    dust.end_size = 0.04f;
    dust.start_color = {0.9f, 0.85f, 0.7f, 0.0f};
    dust.end_color = {0.9f, 0.85f, 0.7f, 0.9f};
    poligon::ParticleEmitter dust_emitter(dust, 2);
    dust_emitter.SetPosition({-1.0f, 1.2f, 2.5f});

    for (int tick = 0; tick < 120; ++tick) {
        rain_emitter.Update(1.0f / 60.0f);
        dust_emitter.Update(1.0f / 60.0f);
    }

    poligon::DirectionalLight sun;
    sun.direction = {0.6f, -0.6f, -0.5f};
    sun.color = {1.2f, 1.1f, 0.95f, 1.0f};
    renderer.BeginFrame(camera, sun);
    const glm::mat4 identity(1.0f);
    renderer.DrawMesh(plane, glm::dvec3(0.0), glm::scale(identity, {40.0f, 1.0f, 40.0f}), poligon::Color{0.35f, 0.4f, 0.28f, 1.0f});
    renderer.DrawMesh(cube, glm::dvec3(0.0), glm::translate(identity, {0.8f, 1.0f, 0.0f}) * glm::scale(identity, {1.0f, 2.0f, 1.0f}),
                      poligon::Color{0.5f, 0.45f, 0.4f, 1.0f});

    poligon::ParticleOptions streaks;
    streaks.stretch = 0.03f;      // a drop's path during about a 30th of a second
    streaks.soft_distance = 0.1f;
    // Particles are relative to their emitter's origin (double).
    renderer.DrawParticles(poligon::TextureHandle{}, rain_emitter.GetOrigin(), rain_emitter.GetParticles().data(),
                           rain_emitter.GetParticleCount(), streaks);
    renderer.DrawParticles(dot, dust_emitter.GetOrigin(), dust_emitter.GetParticles().data(), dust_emitter.GetParticleCount());
    renderer.EndFrame();

    if (!renderer.SaveFrameToPng(argv[1])) {
        std::fprintf(stderr, "cannot write %s\n", argv[1]);
        return 1;
    }
    std::printf("saved %s: %d raindrops, %d motes of dust\n", argv[1], rain_emitter.GetParticleCount(), dust_emitter.GetParticleCount());
    return 0;
}

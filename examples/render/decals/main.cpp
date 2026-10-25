// Example: decals on the ground: a trail of hoof prints and a blood splash, one passing a
// boulder (the print does not climb its side) and half in its shadow (darker there).
// Pictures are made in code; a game would load them as textures.
// Usage: example_decals OUTPUT.png
#include <cmath>
#include <cstdio>

#include "poligon/render/decal.h"
#include "poligon/render/renderer.h"

namespace {

// A soft dark blob of the given color: a hoof print (two lobes) or a splash (round, ragged).
poligon::Image MakeBlob(std::uint8_t r, std::uint8_t g, std::uint8_t b, bool hoof) {
    constexpr int kSize = 64;
    poligon::Image image = poligon::MakeSolidImage(kSize, kSize, r, g, b);
    for (int y = 0; y < kSize; ++y) {
        for (int x = 0; x < kSize; ++x) {
            const float u = (static_cast<float>(x) + 0.5f) / kSize * 2.0f - 1.0f;
            const float v = (static_cast<float>(y) + 0.5f) / kSize * 2.0f - 1.0f;
            float inside = 0.0f;
            if (hoof) {
                // Two toes side by side, a gap between them.
                const float left = std::hypot((u + 0.3f) / 0.28f, v / 0.8f);
                const float right = std::hypot((u - 0.3f) / 0.28f, v / 0.8f);
                inside = 1.0f - std::min(left, right);
            } else {
                const float angle = std::atan2(v, u);
                const float edge = 0.7f + 0.15f * std::sin(angle * 7.0f) + 0.08f * std::sin(angle * 13.0f);
                inside = edge - std::hypot(u, v);
            }
            image.pixels[static_cast<size_t>(y * kSize + x) * 4 + 3] =
                static_cast<std::uint8_t>(std::fmin(std::fmax(inside * 8.0f, 0.0f), 1.0f) * 220.0f);
        }
    }
    return image;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: example_decals OUTPUT.png\n");
        return 2;
    }
    poligon::RendererConfig config;
    config.width = 640;
    config.height = 360;
    config.headless = true;
    config.clear_color = {0.55f, 0.65f, 0.8f, 1.0f};
    poligon::Renderer renderer(config);
    const poligon::MeshHandle plane = renderer.CreatePlaneMesh();
    const poligon::MeshHandle cube = renderer.CreateCubeMesh();

    poligon::DecalMaterial hoof;
    hoof.texture = renderer.CreateTexture(MakeBlob(40, 30, 20, true));
    hoof.roughness = 0.9f;
    poligon::DecalMaterial blood;
    blood.texture = renderer.CreateTexture(MakeBlob(90, 5, 5, false));
    blood.roughness = 0.3f;  // still wet

    poligon::Camera camera;
    camera.position = {1.2f, 1.3f, 2.4f};
    camera.target = {0.0f, 0.0f, 0.0f};
    poligon::DirectionalLight sun;
    sun.direction = {-0.5f, -1.0f, -0.4f};
    renderer.BeginFrame(camera, sun);
    const glm::mat4 identity(1.0f);
    renderer.DrawMesh(plane, glm::scale(identity, {20.0f, 1.0f, 20.0f}), poligon::Color{0.55f, 0.48f, 0.36f, 1.0f});
    renderer.DrawMesh(cube, glm::translate(identity, {0.3f, 0.35f, -0.4f}) * glm::scale(identity, {0.6f, 0.7f, 0.6f}),
                      poligon::Color{0.5f, 0.5f, 0.48f, 1.0f});

    // A trail of prints walking towards -z, turning slightly; each box 12 x 30 x 16 cm, its
    // height (y) reaching a little above and below the ground.
    for (int step = 0; step < 8; ++step) {
        const float z = 1.6f - static_cast<float>(step) * 0.45f;
        const float x = -0.5f + (step % 2 == 0 ? -0.12f : 0.12f) + 0.05f * static_cast<float>(step);
        const glm::mat4 print = glm::translate(identity, {x, 0.0f, z}) * glm::rotate(identity, 0.1f * static_cast<float>(step), {0.0f, 1.0f, 0.0f}) *
                                glm::scale(identity, {0.12f, 0.3f, 0.16f});
        renderer.DrawDecal(print, hoof);
    }
    // The blood splash, partly against the boulder: the boulder's sides stay clean.
    renderer.DrawDecal(glm::translate(identity, {0.1f, 0.0f, 0.1f}) * glm::scale(identity, {0.9f, 0.5f, 0.9f}), blood);
    renderer.EndFrame();

    if (!renderer.SaveFrameToPng(argv[1])) {
        std::fprintf(stderr, "cannot write %s\n", argv[1]);
        return 1;
    }
    std::printf("saved %s\n", argv[1]);
    return 0;
}

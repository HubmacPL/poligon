// Checks AgX tone mapping (ToneMapping::AgX, AgXPunchy): a very bright saturated orange keeps
// its hue under AgX (it bleaches towards white but stays orange), while the ACES curve turns
// it towards yellow; a mid gray stays gray under every curve; Punchy has more contrast than
// plain AgX. Each picture is one emissive surface filling the view, the sun off.
// Usage: tone_mapping_test d3d12|vulkan
#include <cmath>
#include <cstdio>
#include <cstring>

#include "poligon/render/renderer.h"
#include "test_support.h"

namespace {

// Hue in degrees of an sRGB-encoded pixel (0 red, 60 yellow).
float Hue(const std::uint8_t* pixel) {
    const float r = pixel[0];
    const float g = pixel[1];
    const float b = pixel[2];
    const float high = std::fmax(r, std::fmax(g, b));
    const float low = std::fmin(r, std::fmin(g, b));
    if (high - low < 1.0f) {
        return 0.0f;
    }
    float hue = 0.0f;
    if (high == r) {
        hue = 60.0f * std::fmod((g - b) / (high - low), 6.0f);
    } else if (high == g) {
        hue = 60.0f * ((b - r) / (high - low) + 2.0f);
    } else {
        hue = 60.0f * ((r - g) / (high - low) + 4.0f);
    }
    return hue < 0.0f ? hue + 360.0f : hue;
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
        std::printf("usage: tone_mapping_test d3d12|vulkan\n");
        return 2;
    }

    // One pixel's color for an emitted light under a curve.
    const auto shade = [backend](poligon::ToneMapping curve, poligon::Color light, std::uint8_t out[3]) {
        poligon::RendererConfig config;
        config.width = 64;
        config.height = 64;
        config.headless = true;
        config.enable_validation = true;
        config.backend = backend;
        config.post.tone_mapping = curve;
        config.post.bloom = false;
        poligon::Renderer renderer(config);
        const poligon::MeshHandle plane = renderer.CreatePlaneMesh();
        poligon::DirectionalLight dark;
        dark.color = {0.0f, 0.0f, 0.0f, 1.0f};
        dark.ambient = {0.0f, 0.0f, 0.0f, 1.0f};
        poligon::Environment environment;
        environment.shadows = false;
        poligon::Material glow;
        glow.color = {0.0f, 0.0f, 0.0f, 1.0f};
        glow.emissive = light;
        poligon::Camera camera;
        camera.position = {0.0, 2.0, 0.001};
        camera.target = {0.0, 0.0, 0.0};
        renderer.BeginFrame(camera, dark, environment);
        renderer.DrawMesh(plane, poligon::Transform(glm::dvec3(0.0), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(20.0f)), glow);
        renderer.EndFrame();
        poligon::Image picture;
        renderer.CaptureFrame(picture);
        const std::uint8_t* pixel = &picture.pixels[static_cast<size_t>(32 * picture.width + 32) * 4];
        out[0] = pixel[0];
        out[1] = pixel[1];
        out[2] = pixel[2];
    };

    // A bright saturated orange (4 times white, hue 14 degrees).
    const poligon::Color orange{4.0f, 1.0f, 0.1f, 1.0f};
    std::uint8_t agx[3];
    std::uint8_t aces[3];
    std::uint8_t punchy[3];
    shade(poligon::ToneMapping::AgX, orange, agx);
    shade(poligon::ToneMapping::Aces, orange, aces);
    shade(poligon::ToneMapping::AgXPunchy, orange, punchy);
    std::printf("  bright orange: AgX %d %d %d (hue %.1f), ACES %d %d %d (hue %.1f), Punchy %d %d %d (hue %.1f)\n", agx[0], agx[1],
                agx[2], Hue(agx), aces[0], aces[1], aces[2], Hue(aces), punchy[0], punchy[1], punchy[2], Hue(punchy));
    Expect(Hue(agx) < 40.0f && agx[0] > agx[1] + 30, "under AgX the bright orange stays orange");
    Expect(Hue(aces) > Hue(agx) + 10.0f, "under ACES it turns towards yellow");

    // Mid gray stays gray.
    std::uint8_t gray[3];
    shade(poligon::ToneMapping::AgX, poligon::Color{0.18f, 0.18f, 0.18f, 1.0f}, gray);
    std::printf("  mid gray under AgX: %d %d %d\n", gray[0], gray[1], gray[2]);
    Expect(std::abs(gray[0] - gray[1]) <= 2 && std::abs(gray[1] - gray[2]) <= 2 && gray[0] > 60 && gray[0] < 200, "gray stays gray");

    // Punchy: darker shadows than plain AgX.
    std::uint8_t dim_agx[3];
    std::uint8_t dim_punchy[3];
    shade(poligon::ToneMapping::AgX, poligon::Color{0.05f, 0.05f, 0.05f, 1.0f}, dim_agx);
    shade(poligon::ToneMapping::AgXPunchy, poligon::Color{0.05f, 0.05f, 0.05f, 1.0f}, dim_punchy);
    std::printf("  a dark gray: AgX %d, Punchy %d\n", dim_agx[0], dim_punchy[0]);
    Expect(dim_punchy[0] < dim_agx[0], "Punchy has more contrast");
    return test::ExitCode();
}

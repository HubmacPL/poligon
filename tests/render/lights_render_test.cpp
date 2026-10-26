// Checks point and spot lights (Renderer::DrawLight): in the dark, a sphere lit by a point light
// from its right is bright on that side and dark on the other; a spot light pointing down
// lights the floor under it and nothing outside its outer angle; a box between a spot light
// with shadows and the floor leaves a shadow on the floor, and none without shadows.
// Usage: lights_render_test d3d12|vulkan
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "poligon/render/renderer.h"
#include "test_support.h"

namespace {

const std::uint8_t* PixelAt(const poligon::Image& image, int x, int y) {
    return &image.pixels[static_cast<size_t>(y * image.width + x) * 4];
}

// A sphere of radius 1 from rings and segments.
poligon::MeshHandle CreateSphere(poligon::Renderer& renderer) {
    constexpr int kRings = 24;
    constexpr int kSegments = 48;
    std::vector<poligon::MeshVertex> vertices;
    std::vector<std::uint32_t> indices;
    for (int ring = 0; ring <= kRings; ++ring) {
        const float theta = 3.14159265f * static_cast<float>(ring) / kRings;
        for (int segment = 0; segment <= kSegments; ++segment) {
            const float phi = 2.0f * 3.14159265f * static_cast<float>(segment) / kSegments;
            const glm::vec3 point(std::sin(theta) * std::cos(phi), std::cos(theta), std::sin(theta) * std::sin(phi));
            poligon::MeshVertex vertex;
            vertex.position = point;
            vertex.normal = point;
            vertex.uv = glm::vec2(static_cast<float>(segment) / kSegments, static_cast<float>(ring) / kRings);
            vertices.push_back(vertex);
        }
    }
    for (int ring = 0; ring < kRings; ++ring) {
        for (int segment = 0; segment < kSegments; ++segment) {
            const std::uint32_t a = static_cast<std::uint32_t>(ring * (kSegments + 1) + segment);
            const std::uint32_t b = a + kSegments + 1;
            // Counter-clockwise seen from outside.
            indices.insert(indices.end(), {a, a + 1, b, b, a + 1, b + 1});
        }
    }
    return renderer.CreateMesh(vertices.data(), static_cast<int>(vertices.size()), indices.data(), static_cast<int>(indices.size()));
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
        std::printf("usage: lights_render_test d3d12|vulkan\n");
        return 2;
    }
    poligon::RendererConfig config;
    config.width = 256;
    config.height = 256;
    config.headless = true;
    config.enable_validation = true;
    config.backend = backend;
    config.post.tone_mapping = poligon::ToneMapping::None;
    poligon::Renderer renderer(config);
    const poligon::MeshHandle sphere = CreateSphere(renderer);
    const poligon::MeshHandle plane = renderer.CreatePlaneMesh();
    const poligon::MeshHandle cube = renderer.CreateCubeMesh();

    // Darkness: no sun, no ambient light.
    poligon::DirectionalLight dark;
    dark.color = {0.0f, 0.0f, 0.0f, 1.0f};
    dark.ambient = {0.0f, 0.0f, 0.0f, 1.0f};
    poligon::Environment environment;
    poligon::Material white;
    white.roughness = 1.0f;
    const glm::quat level(1.0f, 0.0f, 0.0f, 0.0f);
    poligon::Image picture;

    // 1. A sphere lit from its right.
    poligon::Camera front;
    front.position = {0.0, 0.0, 5.0};
    front.target = {0.0, 0.0, 0.0};
    renderer.BeginFrame(front, dark, environment);
    renderer.DrawMesh(sphere, poligon::Transform(glm::dvec3(0.0), level, glm::vec3(1.0f)), white);
    poligon::PointLight lamp;
    lamp.position = {2.5, 0.0, 0.0};
    lamp.intensity = 4.0f;
    lamp.range = 8.0f;
    renderer.DrawLight(lamp);
    renderer.EndFrame();
    renderer.CaptureFrame(picture);
    // The sphere spans about 128 +- 45 pixels across (radius 1 from 5 m with a 60 degree view).
    const std::uint8_t* right = PixelAt(picture, 128 + 35, 128);
    const std::uint8_t* left = PixelAt(picture, 128 - 35, 128);
    std::printf("  sphere: right %d, left %d, lights drawn %d\n", right[0], left[0], renderer.GetFrameStats().lights_drawn);
    Expect(right[0] > 150, "the side towards the point light is bright");
    Expect(left[0] < 5, "the far side stays dark");
    Expect(renderer.GetFrameStats().lights_drawn == 1, "one light drawn");

    // 2. A spot light pointing down at a floor, seen from above.
    poligon::Camera above;
    above.position = {0.0, 6.0, 0.001};
    above.target = {0.0, 0.0, 0.0};
    poligon::SpotLight torch;
    torch.position = {0.0, 3.0, 0.0};
    torch.direction = {0.0f, -1.0f, 0.0f};
    torch.intensity = 9.0f;
    torch.range = 10.0f;
    torch.inner_angle_degrees = 15.0f;
    torch.outer_angle_degrees = 20.0f;
    const auto draw_floor = [&](bool with_box, bool shadows) {
        renderer.BeginFrame(above, dark, environment);
        renderer.DrawMesh(plane, poligon::Transform(glm::dvec3(0.0), level, glm::vec3(20.0f)), white);
        if (with_box) {
            renderer.DrawMesh(cube, poligon::Transform({0.0, 1.5, 0.0}, level, glm::vec3(0.4f)), white);
        }
        poligon::SpotLight light = torch;
        light.shadows = shadows;
        renderer.DrawLight(light);
        renderer.EndFrame();
        renderer.CaptureFrame(picture);
    };
    draw_floor(false, false);
    // From 6 m up a 60 degree view shows about 6.9 m across: 37 pixels per meter.
    const std::uint8_t* centre = PixelAt(picture, 128 + 20, 128);  // 0.54 m out: 10 degrees from the axis
    const std::uint8_t* outside = PixelAt(picture, 128 + 52, 128);  // 1.4 m out: 25 degrees
    std::printf("  spot: inside %d, outside %d\n", centre[0], outside[0]);
    Expect(centre[0] > 150, "the floor inside the spot's cone is lit");
    Expect(outside[0] < 5, "the floor outside its outer angle is dark");

    // 3. A 0.4 m box 1.5 m up between the spot light and the floor. From above the box hides
    // the floor out to about 0.28 m; its shadow reaches out to 0.46 m (0.2 m * 3 / 1.3).
    draw_floor(true, true);
    const int under = 128 + 14;  // 0.37 m out: in the shadow, beside the box as seen
    const int beside = 128 + 22;  // 0.6 m out: lit, inside the cone
    const int shadowed = PixelAt(picture, under, 128)[0];
    const int lit = PixelAt(picture, beside, 128)[0];
    draw_floor(true, false);
    const int unshadowed = PixelAt(picture, under, 128)[0];
    std::printf("  spot shadow: in the shadow %d, beside it %d, the same place without shadows %d\n", shadowed, lit, unshadowed);
    Expect(lit > 150 && shadowed < lit / 4, "the box's shadow falls on the floor");
    Expect(unshadowed > 150, "without shadows the same place is lit");
    return test::ExitCode();
}

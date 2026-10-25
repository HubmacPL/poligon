// Renders decals without a window and checks pixels: a red decal colors the floor inside its
// box and nothing outside it, leaves a wall standing in its box alone, and is darker where a
// slab shades it from the sun.
// Usage: decal_render_test d3d12|vulkan
#include <cstdio>
#include <cstring>

#include "poligon/render/decal.h"
#include "poligon/render/renderer.h"
#include "test_support.h"

namespace {

constexpr int kSize = 256;

// The pixel a world position is drawn at, computed the way the renderer does.
glm::ivec2 ToPixel(const poligon::Camera& camera, glm::vec3 position) {
    const glm::mat4 view = glm::lookAt(camera.position, camera.target, camera.up);
    const glm::mat4 projection = glm::perspective(glm::radians(camera.vertical_fov_degrees), 1.0f, camera.near_plane, camera.far_plane);
    const glm::vec4 clip = projection * view * glm::vec4(position, 1.0f);
    return {static_cast<int>((clip.x / clip.w * 0.5f + 0.5f) * kSize), static_cast<int>((0.5f - clip.y / clip.w * 0.5f) * kSize)};
}

glm::ivec3 ColorAt(const poligon::Image& image, glm::ivec2 pixel) {
    const std::uint8_t* value = &image.pixels[static_cast<size_t>(pixel.y * image.width + pixel.x) * 4];
    return {value[0], value[1], value[2]};
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
        std::printf("usage: decal_render_test d3d12|vulkan\n");
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
    const poligon::MeshHandle cube = renderer.CreateCubeMesh();
    const glm::mat4 identity(1.0f);

    poligon::Material white;
    white.roughness = 1.0f;
    poligon::DecalMaterial blood;
    blood.color = {0.8f, 0.0f, 0.0f, 1.0f};
    blood.roughness = 1.0f;
    blood.edge_fade = 0.05f;

    // The sun straight above; a slab 6 m up (above the camera) shades the half of the floor with z < 0.
    poligon::DirectionalLight sun;
    sun.direction = {0.0f, -1.0f, 0.0f};
    sun.color = {0.8f, 0.8f, 0.8f, 1.0f};
    sun.ambient = {0.15f, 0.15f, 0.15f, 1.0f};
    poligon::Environment environment;
    environment.shadow_distance = 20.0f;

    poligon::Camera camera;
    camera.position = {-3.0f, 4.0f, 0.0f};
    camera.target = {0.0f, 0.0f, 0.0f};

    // A wall facing -x, standing inside the decal's box at x = 0.6.
    const glm::mat4 wall = glm::translate(identity, {0.7f, 1.0f, 0.0f}) * glm::scale(identity, {0.2f, 2.0f, 4.0f});
    const glm::mat4 slab = glm::translate(identity, {-0.5f, 6.0f, -2.0f}) * glm::scale(identity, {4.0f, 0.1f, 4.0f});
    // The decal: a box 2 m across and 1 m high around the origin, pressed along y.
    const glm::mat4 box = glm::scale(identity, {2.0f, 1.0f, 2.0f});

    poligon::Image with_decal;
    poligon::Image without_decal;
    for (int pass = 0; pass < 2; ++pass) {
        renderer.BeginFrame(camera, sun, environment);
        renderer.DrawMesh(plane, glm::scale(identity, {40.0f, 1.0f, 40.0f}), white);
        renderer.DrawMesh(cube, wall, white);
        renderer.DrawMesh(cube, slab, white);
        if (pass == 0) {
            renderer.DrawDecal(box, blood);
        }
        renderer.EndFrame();
        renderer.CaptureFrame(pass == 0 ? with_decal : without_decal);
    }

    const glm::ivec3 lit_decal = ColorAt(with_decal, ToPixel(camera, {-0.5f, 0.0f, 0.5f}));
    const glm::ivec3 shaded_decal = ColorAt(with_decal, ToPixel(camera, {-0.5f, 0.0f, -0.5f}));
    const glm::ivec3 outside = ColorAt(with_decal, ToPixel(camera, {-1.6f, 0.0f, 0.5f}));
    const glm::ivec3 outside_before = ColorAt(without_decal, ToPixel(camera, {-1.6f, 0.0f, 0.5f}));
    const glm::ivec3 wall_face = ColorAt(with_decal, ToPixel(camera, {0.6f, 0.25f, 0.5f}));
    const glm::ivec3 wall_before = ColorAt(without_decal, ToPixel(camera, {0.6f, 0.25f, 0.5f}));
    std::printf("  decal lit %d %d %d, in shadow %d %d %d; floor outside %d %d %d (was %d %d %d); wall %d %d %d (was %d %d %d)\n",
                lit_decal.x, lit_decal.y, lit_decal.z, shaded_decal.x, shaded_decal.y, shaded_decal.z, outside.x, outside.y, outside.z,
                outside_before.x, outside_before.y, outside_before.z, wall_face.x, wall_face.y, wall_face.z, wall_before.x, wall_before.y,
                wall_before.z);
    Expect(lit_decal.x > 150 && lit_decal.y < 30 && lit_decal.z < 30, "a decal colors the floor inside its box");
    Expect(outside == outside_before, "the floor outside the box is left as it was");
    Expect(glm::all(glm::lessThanEqual(glm::abs(wall_face - wall_before), glm::ivec3(1))),
           "a wall standing in the box, turned away from its axis, is left alone");
    Expect(shaded_decal.x < lit_decal.x * 0.6f && shaded_decal.x > 0 && shaded_decal.y < 30,
           "a decal in shadow is darker, lit like the surface it lies on");

    std::printf("decal_render_test finished\n");
    return test::ExitCode();
}

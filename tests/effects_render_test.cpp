// Renders small scenes without a window and checks single pixels: ambient occlusion,
// volumetric fog with light shafts, bloom and automatic exposure.
// Usage: effects_render_test d3d12|vulkan
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "poligon/renderer.h"
#include "test_support.h"

namespace {

constexpr int kSize = 256;

int RedAt(const poligon::Image& image, int x, int y) {
    return image.pixels[static_cast<size_t>(y * image.width + x) * 4];
}

bool RedIs(const poligon::Image& image, int x, int y, int expected, int tolerance) {
    const int red = RedAt(image, x, y);
    const bool match = std::abs(red - expected) <= tolerance;
    if (!match) {
        std::printf("  pixel (%d, %d) has red %d, expected about %d\n", x, y, red, expected);
    }
    return match;
}

poligon::RendererConfig MakeConfig(poligon::GraphicsBackend backend) {
    poligon::RendererConfig config;
    config.width = kSize;
    config.height = kSize;
    config.headless = true;
    config.enable_validation = true;
    config.backend = backend;
    config.clear_color = {0.0f, 0.0f, 0.0f, 1.0f};
    // No tone mapping, so that expected pixel values can be computed by hand.
    config.post.tone_mapping = poligon::ToneMapping::None;
    return config;
}

// A material that reflects no highlights, so a pixel is exactly "color times light".
poligon::Material MakeMatte(poligon::Color color) {
    poligon::Material material;
    material.color = color;
    material.roughness = 1.0f;
    return material;
}

// Light that is the same from everywhere, with the sun off.
poligon::DirectionalLight MakeFlatLight(float brightness) {
    poligon::DirectionalLight light;
    light.color = {0.0f, 0.0f, 0.0f, 1.0f};
    light.ambient = {brightness, brightness, brightness, 1.0f};
    return light;
}

// A camera straight above the origin looking down, with -Z at the top of the picture.
poligon::Camera MakeTopDownCamera(float height) {
    poligon::Camera camera;
    camera.position = {0.0f, height, 0.0f};
    camera.target = {0.0f, 0.0f, 0.0f};
    camera.up = {0.0f, 0.0f, -1.0f};
    return camera;
}

// The pixel a world position is drawn at, computed the way the renderer does.
glm::ivec2 ToPixel(const poligon::Camera& camera, glm::vec3 position) {
    const glm::mat4 view = glm::lookAt(camera.position, camera.target, camera.up);
    const glm::mat4 projection =
        glm::perspective(glm::radians(camera.vertical_fov_degrees), 1.0f, camera.near_plane, camera.far_plane);
    const glm::vec4 clip = projection * view * glm::vec4(position, 1.0f);
    return {static_cast<int>((clip.x / clip.w * 0.5f + 0.5f) * kSize),
            static_cast<int>((0.5f - clip.y / clip.w * 0.5f) * kSize)};
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
        std::printf("usage: effects_render_test d3d12|vulkan\n");
        return 2;
    }

    const glm::mat4 identity(1.0f);
    const poligon::Material white = MakeMatte({1.0f, 1.0f, 1.0f, 1.0f});
    poligon::Image frame;

    {
        poligon::Renderer renderer(MakeConfig(backend));
        const poligon::MeshHandle plane = renderer.CreatePlaneMesh();
        const poligon::MeshHandle cube = renderer.CreateCubeMesh();

        // --- Ambient occlusion ---
        // A cube standing on a floor under even light. Without the effect floor and cube are
        // plain white (255). With it, the floor right in front of the cube is darker, and the
        // open floor far from it is not.
        {
            poligon::Camera camera;
            camera.position = {0.0f, 3.0f, 5.0f};
            camera.target = {0.0f, 0.3f, 0.0f};
            const glm::ivec2 near_cube = ToPixel(camera, {0.0f, 0.0f, 0.62f});
            const glm::ivec2 open_floor = ToPixel(camera, {2.5f, 0.0f, 2.0f});
            for (int pass = 0; pass < 2; ++pass) {
                poligon::Environment environment;
                environment.ambient_occlusion = pass == 1;
                renderer.BeginFrame(camera, MakeFlatLight(1.0f), environment);
                renderer.DrawMesh(plane, glm::scale(identity, {40.0f, 1.0f, 40.0f}), white);
                renderer.DrawMesh(cube, glm::translate(identity, {0.0f, 0.5f, 0.0f}), white);
                renderer.EndFrame();
                renderer.CaptureFrame(frame);
                if (pass == 0) {
                    Expect(RedIs(frame, near_cube.x, near_cube.y, 255, 1), "without ambient occlusion the corner is plain white");
                } else {
                    const int corner = RedAt(frame, near_cube.x, near_cube.y);
                    if (corner > 225 || corner < 60) {
                        std::printf("  corner pixel has red %d\n", corner);
                    }
                    Expect(corner <= 225 && corner >= 60, "ambient occlusion darkens the floor where it meets the cube");
                    Expect(RedIs(frame, open_floor.x, open_floor.y, 255, 3), "ambient occlusion leaves open floor alone");

                    // The debug view shows the occlusion itself: dark in the corner, white
                    // in the open, whatever the light. The same frame is drawn again.
                    poligon::PostSettings post = renderer.GetPostSettings();
                    post.debug_view = poligon::DebugView::AmbientOcclusion;
                    renderer.SetPostSettings(post);
                    renderer.BeginFrame(camera, MakeFlatLight(0.3f), environment);
                    renderer.DrawMesh(plane, glm::scale(identity, {40.0f, 1.0f, 40.0f}), white);
                    renderer.DrawMesh(cube, glm::translate(identity, {0.0f, 0.5f, 0.0f}), white);
                    renderer.EndFrame();
                    renderer.CaptureFrame(frame);
                    Expect(RedAt(frame, near_cube.x, near_cube.y) <= 225, "the ambient occlusion view is dark in the corner");
                    Expect(RedIs(frame, open_floor.x, open_floor.y, 255, 3), "the ambient occlusion view is white in the open");
                    post.debug_view = poligon::DebugView::None;
                    renderer.SetPostSettings(post);
                }
            }

            // Tinting by shadow cascade: the floor 3.6 m from the camera is shaded by the
            // second cascade and comes out green, far away by the last one and comes out yellow.
            poligon::Environment environment;
            environment.show_shadow_cascades = true;
            environment.shadow_distance = 40.0f;
            poligon::DirectionalLight sun;
            sun.color = {0.5f, 0.5f, 0.5f, 1.0f};
            sun.ambient = {0.5f, 0.5f, 0.5f, 1.0f};
            renderer.BeginFrame(camera, sun, environment);
            renderer.DrawMesh(plane, glm::scale(identity, {40.0f, 1.0f, 40.0f}), white);
            renderer.EndFrame();
            renderer.CaptureFrame(frame);
            const glm::ivec2 near_floor = ToPixel(camera, {0.0f, 0.0f, 3.0f});
            const glm::ivec2 far_floor = ToPixel(camera, {0.0f, 0.0f, -14.0f});
            const std::uint8_t* near_pixel = &frame.pixels[static_cast<size_t>(near_floor.y * frame.width + near_floor.x) * 4];
            const std::uint8_t* far_pixel = &frame.pixels[static_cast<size_t>(far_floor.y * frame.width + far_floor.x) * 4];
            Expect(near_pixel[1] > near_pixel[0] + 40 && near_pixel[1] > near_pixel[2] + 40,
                   "the cascade view tints near floor green");
            Expect(far_pixel[0] > far_pixel[2] + 40 && far_pixel[1] > far_pixel[2] + 40 && far_pixel[1] < far_pixel[0] + 20,
                   "the cascade view tints distant floor yellow");
        }

        // --- Volumetric fog and light shafts ---
        // Nothing but fog and a slab high above the origin, with the sun straight overhead.
        // The slab shadows a column of fog 4 m across. The camera looks level through 20 m of
        // fog (density 0.1 per meter): lit all the way it adds 1 - exp(-2) = 0.865 (stored
        // as 239). A line of sight that crosses the dark column between 8 and 12 m loses
        // exp(-0.8) * (1 - exp(-0.4)) = 0.148 of that: 0.717, stored as 221.
        {
            poligon::Camera camera;
            camera.position = {0.0f, 1.0f, 10.0f};
            camera.target = {0.0f, 1.0f, 0.0f};
            camera.far_plane = 20.0f;
            poligon::DirectionalLight sun;
            sun.direction = {0.0f, -1.0f, 0.0f};
            sun.color = {1.0f, 1.0f, 1.0f, 1.0f};
            poligon::Environment environment;
            environment.shadow_distance = 20.0f;
            environment.volumetric_fog = true;
            environment.fog_density = 0.1f;
            environment.fog_color = {1.0f, 1.0f, 1.0f, 1.0f};
            environment.fog_shadow_brightness = 0.0f;
            environment.fog_sun_glow = 0.0f;
            const glm::mat4 slab = glm::translate(identity, {0.0f, 5.0f, 0.0f}) * glm::scale(identity, {4.0f, 0.2f, 4.0f});
            const int row = kSize / 2;

            renderer.BeginFrame(camera, sun, environment);
            renderer.DrawMesh(cube, slab, white);
            renderer.EndFrame();
            renderer.CaptureFrame(frame);
            Expect(RedIs(frame, 17, row, 239, 5), "fog in sunlight has the expected brightness");
            Expect(RedIs(frame, kSize / 2, row, 221, 7), "fog is darker along a line of sight that crosses a shadow");

            environment.shadows = false;
            renderer.BeginFrame(camera, sun, environment);
            renderer.DrawMesh(cube, slab, white);
            renderer.EndFrame();
            renderer.CaptureFrame(frame);
            Expect(RedIs(frame, kSize / 2, row, 239, 5), "without shadows the fog is evenly lit");

            // The fog thins out with height: far above the base it adds almost nothing.
            environment.fog_height_falloff = 1.0f;
            environment.fog_base_height = -20.0f;
            renderer.BeginFrame(camera, sun, environment);
            renderer.EndFrame();
            renderer.CaptureFrame(frame);
            Expect(RedIs(frame, kSize / 2, row, 0, 2), "fog that has thinned out with height is invisible");
        }
    }

    // --- Bloom ---
    // A small, very bright square on black. Without bloom the black next to it stays black;
    // with bloom the square's light spills over, less so far away.
    for (int pass = 0; pass < 2; ++pass) {
        poligon::RendererConfig config = MakeConfig(backend);
        config.post.bloom = pass == 1;
        poligon::Renderer renderer(config);
        const poligon::MeshHandle plane = renderer.CreatePlaneMesh();
        const poligon::Camera camera = MakeTopDownCamera(10.0f);
        renderer.BeginFrame(camera, MakeFlatLight(30.0f));
        renderer.DrawMesh(plane, identity, white);
        renderer.EndFrame();
        renderer.CaptureFrame(frame);
        const int center = kSize / 2;
        const int beside = RedAt(frame, center + 20, center);
        const int far_away = RedAt(frame, center + 110, center);
        Expect(RedAt(frame, center, center) == 255, "the bright square itself is white");
        if (pass == 0) {
            Expect(beside == 0, "without bloom the black next to a bright square stays black");
        } else {
            if (beside < 20 || far_away >= beside) {
                std::printf("  beside the square: %d, far away: %d\n", beside, far_away);
            }
            Expect(beside >= 20, "bloom spills light over the surroundings of a bright square");
            Expect(far_away < beside, "bloom fades with distance");
        }
    }

    // --- Automatic exposure ---
    // A floor that fills the picture. Whatever the light, the first frame comes out at the
    // target brightness (0.25, stored as 137).
    for (const float brightness : {8.0f, 0.02f}) {
        poligon::RendererConfig config = MakeConfig(backend);
        config.post.auto_exposure = true;
        config.post.auto_exposure_target = 0.25f;
        config.post.auto_exposure_min = 0.001f;
        config.post.auto_exposure_max = 1000.0f;
        config.post.auto_exposure_speed = 0.5f;
        poligon::Renderer renderer(config);
        const poligon::MeshHandle plane = renderer.CreatePlaneMesh();
        const poligon::Camera camera = MakeTopDownCamera(5.0f);
        const glm::mat4 floor = glm::scale(identity, {40.0f, 1.0f, 40.0f});

        renderer.BeginFrame(camera, MakeFlatLight(brightness));
        renderer.DrawMesh(plane, floor, white);
        renderer.EndFrame();
        renderer.CaptureFrame(frame);
        Expect(RedIs(frame, kSize / 2, kSize / 2, 137, 3), "automatic exposure brings the picture to the target brightness");

        // The light drops to a quarter. With speed 0.5 the eye is now adapted to the middle
        // between the old and the new brightness (2.5 quarters), so the picture is too dark
        // for the moment: 0.25 / 2.5 = 0.1, stored as 89.
        renderer.BeginFrame(camera, MakeFlatLight(brightness * 0.25f));
        renderer.DrawMesh(plane, floor, white);
        renderer.EndFrame();
        renderer.CaptureFrame(frame);
        Expect(RedIs(frame, kSize / 2, kSize / 2, 89, 3), "automatic exposure follows a change of light gradually");
    }

    std::printf("effects_render_test finished\n");
    return test::ExitCode();
}

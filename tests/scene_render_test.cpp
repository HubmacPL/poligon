// Renders small scenes without a window and checks single pixels: shadows, sky, fog,
// normal maps, multisampling and tone mapping.
// Usage: scene_render_test d3d12|vulkan
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "poligon/renderer.h"
#include "test_support.h"

namespace {

constexpr int kSize = 256;

const std::uint8_t* PixelAt(const poligon::Image& image, int x, int y) {
    return &image.pixels[static_cast<size_t>(y * image.width + x) * 4];
}

bool PixelIs(const poligon::Image& image, int x, int y, int r, int g, int b, int tolerance) {
    const std::uint8_t* pixel = PixelAt(image, x, y);
    const bool match = std::abs(pixel[0] - r) <= tolerance && std::abs(pixel[1] - g) <= tolerance &&
                       std::abs(pixel[2] - b) <= tolerance;
    if (!match) {
        std::printf("  pixel (%d, %d) is (%d, %d, %d), expected about (%d, %d, %d)\n", x, y, pixel[0], pixel[1],
                    pixel[2], r, g, b);
    }
    return match;
}

// A camera straight above the origin looking down, with -Z at the top of the picture.
poligon::Camera MakeTopDownCamera(float height) {
    poligon::Camera camera;
    camera.position = {0.0f, height, 0.0f};
    camera.target = {0.0f, 0.0f, 0.0f};
    camera.up = {0.0f, 0.0f, -1.0f};
    camera.vertical_fov_degrees = 60.0f;
    return camera;
}

// Converts a point on the ground (x or z) to the pixel it is drawn at by MakeTopDownCamera(height).
int ToPixel(float ground_coordinate, float height) {
    const float visible_half_extent = height * 0.57735027f;  // height * tan(30 degrees)
    return static_cast<int>(kSize * 0.5f + ground_coordinate / visible_half_extent * kSize * 0.5f);
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

} // namespace

int main(int argc, char** argv) {
    using test::Expect;

    poligon::GraphicsBackend backend = poligon::GraphicsBackend::Default;
    if (argc == 2 && std::strcmp(argv[1], "d3d12") == 0) {
        backend = poligon::GraphicsBackend::Direct3D12;
    } else if (argc == 2 && std::strcmp(argv[1], "vulkan") == 0) {
        backend = poligon::GraphicsBackend::Vulkan;
    } else {
        std::printf("usage: scene_render_test d3d12|vulkan\n");
        return 2;
    }

    const glm::mat4 identity(1.0f);
    poligon::Image frame;
    int smoothed_edge_pixels_with_msaa = 0;

    {
        poligon::Renderer renderer(MakeConfig(backend));
        const poligon::MeshHandle plane = renderer.CreatePlaneMesh();
        const poligon::MeshHandle cube = renderer.CreateCubeMesh();
        Expect(renderer.GetMsaaSamples() == 4, "multisampling with 4 samples is on by default");

        // --- Shadows ---
        // A white floor, a cube floating 2 m above it, the sun almost overhead but leaning
        // towards +X. The cube's shadow then lies on the floor shifted towards +X.
        // Lit floor: 0.2 ambient + 0.5 sun * 0.958 (angle) = 0.68, stored as 215.
        // Shadowed floor: 0.2 ambient only, stored as 124.
        {
            poligon::DirectionalLight sun;
            sun.direction = {0.3f, -1.0f, 0.0f};
            sun.color = {0.5f, 0.5f, 0.5f, 1.0f};
            sun.ambient = {0.2f, 0.2f, 0.2f, 1.0f};
            const float height = 10.0f;
            const glm::mat4 floor_transform = glm::scale(identity, {10.0f, 1.0f, 10.0f});
            const glm::mat4 cube_transform = glm::translate(identity, {0.0f, 2.0f, 0.0f});
            const poligon::Material white = MakeMatte({1.0f, 1.0f, 1.0f, 1.0f});

            for (int pass = 0; pass < 2; ++pass) {
                poligon::Environment environment;
                environment.shadows = pass == 0;
                environment.shadow_distance = 30.0f;
                renderer.BeginFrame(MakeTopDownCamera(height), sun, environment);
                renderer.DrawMesh(plane, floor_transform, white);
                renderer.DrawMesh(cube, cube_transform, white);
                renderer.EndFrame();
                renderer.CaptureFrame(frame);

                const int z = ToPixel(0.0f, height);
                Expect(PixelIs(frame, ToPixel(-2.5f, height), z, 215, 215, 215, 4), "floor away from the cube is lit");
                if (pass == 0) {
                    Expect(PixelIs(frame, ToPixel(1.1f, height), z, 124, 124, 124, 4),
                           "floor behind the cube, seen from the sun, is in shadow");
                    Expect(PixelIs(frame, ToPixel(-1.0f, height), z, 215, 215, 215, 4),
                           "floor on the sunny side of the cube is not in shadow");
                } else {
                    Expect(PixelIs(frame, ToPixel(1.1f, height), z, 215, 215, 215, 4),
                           "with shadows turned off the same spot is lit");
                }
            }
        }

        // --- Culling ---
        // Three cubes: one in view, one behind the camera, one far off to the side. Only the
        // first is drawn. The one off to the side sits between the sun and the visible
        // floor, so it is still drawn into the shadow image and its shadow shows.
        {
            poligon::DirectionalLight sun;
            sun.direction = {-1.0f, -1.0f, 0.0f};
            sun.color = {0.5f, 0.5f, 0.5f, 1.0f};
            sun.ambient = {0.2f, 0.2f, 0.2f, 1.0f};
            const float height = 10.0f;
            const poligon::Material white = MakeMatte({1.0f, 1.0f, 1.0f, 1.0f});
            poligon::Environment environment;
            environment.shadow_distance = 30.0f;
            renderer.BeginFrame(MakeTopDownCamera(height), sun, environment);
            renderer.DrawMesh(plane, glm::scale(identity, {10.0f, 1.0f, 10.0f}), white);
            renderer.DrawMesh(cube, glm::translate(identity, {-3.0f, 0.5f, 3.0f}), white);
            renderer.DrawMesh(cube, glm::translate(identity, {0.0f, 20.0f, 0.0f}), white);
            renderer.DrawMesh(cube, glm::translate(identity, {12.0f, 8.0f, 0.0f}), white);
            renderer.EndFrame();
            renderer.CaptureFrame(frame);
            const poligon::FrameStats stats = renderer.GetFrameStats();
            if (stats.meshes_submitted != 4 || stats.meshes_drawn != 2) {
                std::printf("  submitted %d, drawn %d\n", stats.meshes_submitted, stats.meshes_drawn);
            }
            Expect(stats.meshes_submitted == 4, "frame statistics count the submitted meshes");
            Expect(stats.meshes_drawn == 2, "meshes behind the camera and off to the side are not drawn");
            Expect(stats.shadow_draws > 0 && stats.shadow_draws < 16, "shadow cascades skip meshes outside them");
            // The cube at (12, 8) throws its shadow down and to the left, onto x = 4.
            Expect(PixelIs(frame, ToPixel(4.0f, height), ToPixel(0.0f, height), 124, 124, 124, 4),
                   "a mesh outside the view still casts its shadow into it");
            Expect(PixelIs(frame, ToPixel(2.5f, height), ToPixel(-2.5f, height), 196, 196, 196, 4),
                   "the floor beside that shadow is lit");
        }

        // --- Destroying meshes and textures ---
        // Destroyed resources are simply gone; everything else keeps drawing as before.
        {
            const poligon::MeshHandle extra_mesh = renderer.CreateCubeMesh();
            const poligon::TextureHandle extra_texture = renderer.CreateTexture(poligon::MakeSolidImage(4, 4, 255, 0, 0));
            renderer.DestroyMesh(extra_mesh);
            renderer.DestroyTexture(extra_texture);

            poligon::DirectionalLight flat;
            flat.color = {0.0f, 0.0f, 0.0f, 1.0f};
            flat.ambient = {1.0f, 1.0f, 1.0f, 1.0f};
            renderer.BeginFrame(MakeTopDownCamera(5.0f), flat);
            renderer.DrawMesh(plane, glm::scale(identity, {4.0f, 1.0f, 4.0f}), MakeMatte({0.0f, 1.0f, 0.0f, 1.0f}));
            renderer.EndFrame();
            renderer.CaptureFrame(frame);
            Expect(PixelIs(frame, kSize / 2, kSize / 2, 0, 255, 0, 1), "drawing works after other resources were destroyed");
        }

        // --- Sky ---
        // No objects, a camera looking level. The middle row of the picture looks exactly at
        // the horizon, the top row 30 degrees up, the bottom row 30 degrees down.
        {
            poligon::Camera camera;
            camera.position = {0.0f, 1.0f, 0.0f};
            camera.target = {0.0f, 1.0f, -1.0f};
            poligon::DirectionalLight no_sun;
            no_sun.color = {0.0f, 0.0f, 0.0f, 1.0f};
            poligon::Environment environment;
            environment.draw_sky = true;
            environment.sky_zenith = {0.0f, 0.0f, 1.0f, 1.0f};
            environment.sky_horizon = {1.0f, 0.0f, 0.0f, 1.0f};
            environment.sky_ground = {0.0f, 1.0f, 0.0f, 1.0f};
            renderer.BeginFrame(camera, no_sun, environment);
            renderer.EndFrame();
            renderer.CaptureFrame(frame);

            const std::uint8_t* top = PixelAt(frame, kSize / 2, 2);
            const std::uint8_t* bottom = PixelAt(frame, kSize / 2, kSize - 3);
            Expect(top[2] > 200 && top[0] > 100 && top[1] == 0, "above the horizon the sky blends towards the zenith color");
            Expect(bottom[1] == 255 && bottom[2] == 0, "well below the horizon the ground color shows");
            const std::uint8_t* middle = PixelAt(frame, kSize / 2, kSize / 2);
            Expect(middle[0] > 235 && middle[2] < 90, "at the horizon the sky has the horizon color");
        }

        // --- Fog ---
        // A black floor 100 m below, fog that hides half of everything at that distance.
        // Half white fog in linear light is stored as 188.
        {
            poligon::DirectionalLight dark;
            dark.color = {0.0f, 0.0f, 0.0f, 1.0f};
            dark.ambient = {0.0f, 0.0f, 0.0f, 1.0f};
            poligon::Environment environment;
            environment.fog_density = 0.0069315f;  // ln(2) / 100
            environment.fog_color = {1.0f, 1.0f, 1.0f, 1.0f};
            renderer.BeginFrame(MakeTopDownCamera(100.0f), dark, environment);
            renderer.DrawMesh(plane, glm::scale(identity, {400.0f, 1.0f, 400.0f}), MakeMatte({0.0f, 0.0f, 0.0f, 1.0f}));
            renderer.EndFrame();
            renderer.CaptureFrame(frame);
            Expect(PixelIs(frame, kSize / 2, kSize / 2, 188, 188, 188, 3), "fog covers half of a surface 100 m away");
        }

        // --- Normal maps ---
        // Light skims along the floor, so the flat floor itself is black. A normal map that
        // tilts every pixel's surface towards the light makes it fully lit.
        {
            poligon::TextureOptions data_texture;
            data_texture.is_color = false;
            data_texture.generate_mipmaps = false;
            // Red encodes the tilt along the texture's u direction (+X on the plane mesh),
            // green the tilt towards the top of the image (-Z on the plane mesh).
            const poligon::TextureHandle tilted_to_u = renderer.CreateTexture(poligon::MakeSolidImage(1, 1, 255, 128, 128), data_texture);
            const poligon::TextureHandle tilted_to_top = renderer.CreateTexture(poligon::MakeSolidImage(1, 1, 128, 255, 128), data_texture);

            poligon::Environment no_shadows;
            no_shadows.shadows = false;
            const glm::mat4 floor_transform = glm::scale(identity, {4.0f, 1.0f, 4.0f});
            struct Case {
                glm::vec3 light_travels;
                poligon::TextureHandle normal_map;
                bool expect_lit;
                const char* description;
            };
            const Case cases[] = {
                {{-1, 0, 0}, {}, false, "without a normal map, light along the floor does not light it"},
                {{-1, 0, 0}, tilted_to_u, true, "a normal map tilted along u catches light coming from +X"},
                {{1, 0, 0}, tilted_to_u, false, "and stays dark for light coming from -X"},
                {{0, 0, 1}, tilted_to_top, true, "a normal map tilted to the image top catches light coming from -Z"},
                {{0, 0, -1}, tilted_to_top, false, "and stays dark for light coming from +Z"},
            };
            for (const Case& test_case : cases) {
                poligon::DirectionalLight light;
                light.direction = test_case.light_travels;
                light.color = {1.0f, 1.0f, 1.0f, 1.0f};
                light.ambient = {0.0f, 0.0f, 0.0f, 1.0f};
                poligon::Material material = MakeMatte({1.0f, 1.0f, 1.0f, 1.0f});
                material.normal_texture = test_case.normal_map;
                renderer.BeginFrame(MakeTopDownCamera(5.0f), light, no_shadows);
                renderer.DrawMesh(plane, floor_transform, material);
                renderer.EndFrame();
                renderer.CaptureFrame(frame);
                const int value = PixelAt(frame, kSize / 2, kSize / 2)[0];
                Expect(test_case.expect_lit ? value > 240 : value < 15, test_case.description);
            }
        }

        // --- Multisampling ---
        // A white square turned by 30 degrees on black: along its slanted edges, pixels are
        // partly covered and must come out gray.
        {
            poligon::DirectionalLight flat;
            flat.color = {0.0f, 0.0f, 0.0f, 1.0f};
            flat.ambient = {1.0f, 1.0f, 1.0f, 1.0f};
            renderer.BeginFrame(MakeTopDownCamera(5.0f), flat);
            renderer.DrawMesh(plane, glm::rotate(identity, glm::radians(30.0f), {0, 1, 0}) * glm::scale(identity, {3.0f, 1.0f, 3.0f}),
                              MakeMatte({1.0f, 1.0f, 1.0f, 1.0f}));
            renderer.EndFrame();
            renderer.CaptureFrame(frame);
            for (int i = 0; i < kSize * kSize; ++i) {
                const int value = frame.pixels[static_cast<size_t>(i) * 4];
                if (value > 30 && value < 225) {
                    ++smoothed_edge_pixels_with_msaa;
                }
            }
            Expect(smoothed_edge_pixels_with_msaa > 100, "with multisampling slanted edges have in-between pixels");
        }
    }

    {
        // The same square without multisampling: every pixel is fully white or fully black.
        poligon::RendererConfig config = MakeConfig(backend);
        config.msaa_samples = 1;
        poligon::Renderer renderer(config);
        const poligon::MeshHandle plane = renderer.CreatePlaneMesh();
        Expect(renderer.GetMsaaSamples() == 1, "multisampling can be turned off");
        poligon::DirectionalLight flat;
        flat.color = {0.0f, 0.0f, 0.0f, 1.0f};
        flat.ambient = {1.0f, 1.0f, 1.0f, 1.0f};
        renderer.BeginFrame(MakeTopDownCamera(5.0f), flat);
        renderer.DrawMesh(plane, glm::rotate(identity, glm::radians(30.0f), {0, 1, 0}) * glm::scale(identity, {3.0f, 1.0f, 3.0f}),
                          MakeMatte({1.0f, 1.0f, 1.0f, 1.0f}));
        renderer.EndFrame();
        renderer.CaptureFrame(frame);
        int in_between = 0;
        for (int i = 0; i < kSize * kSize; ++i) {
            const int value = frame.pixels[static_cast<size_t>(i) * 4];
            if (value > 30 && value < 225) {
                ++in_between;
            }
        }
        Expect(in_between == 0, "without multisampling there are no in-between pixels");
    }

    {
        // Tone mapping: the filmic curve keeps a fully lit white surface below pure white
        // (0.80 linear, stored as 231), leaving room for brighter things; exposure scales the light.
        poligon::RendererConfig config = MakeConfig(backend);
        config.post.tone_mapping = poligon::ToneMapping::Aces;
        poligon::Renderer renderer(config);
        const poligon::MeshHandle plane = renderer.CreatePlaneMesh();
        poligon::DirectionalLight flat;
        flat.color = {0.0f, 0.0f, 0.0f, 1.0f};
        flat.ambient = {1.0f, 1.0f, 1.0f, 1.0f};
        renderer.BeginFrame(MakeTopDownCamera(5.0f), flat);
        renderer.DrawMesh(plane, glm::scale(glm::mat4(1.0f), {4.0f, 1.0f, 4.0f}), MakeMatte({1.0f, 1.0f, 1.0f, 1.0f}));
        renderer.EndFrame();
        renderer.CaptureFrame(frame);
        Expect(PixelIs(frame, kSize / 2, kSize / 2, 231, 231, 231, 3), "filmic tone mapping keeps white below the maximum");
    }

    std::printf("scene_render_test finished\n");
    return test::ExitCode();
}

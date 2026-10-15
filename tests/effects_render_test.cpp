// Renders small scenes without a window and checks single pixels: ambient occlusion,
// volumetric fog with light shafts, bloom and automatic exposure.
// Usage: effects_render_test d3d12|vulkan
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "poligon/renderer.h"
#include "poligon/sky_occlusion.h"
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

            // A cube whose material casts no ambient occlusion (as grass does): the floor in
            // front of it stays white, and the cube itself is still drawn in front of it.
            {
                poligon::Material no_ao = white;
                no_ao.casts_ambient_occlusion = false;
                poligon::Environment environment;
                environment.ambient_occlusion = true;
                renderer.BeginFrame(camera, MakeFlatLight(1.0f), environment);
                renderer.DrawMesh(plane, glm::scale(identity, {40.0f, 1.0f, 40.0f}), white);
                renderer.DrawMesh(cube, glm::translate(identity, {0.0f, 0.5f, 0.0f}),
                                  [&] { poligon::Material red = no_ao; red.color = {1.0f, 0.0f, 0.0f, 1.0f}; return red; }());
                renderer.EndFrame();
                renderer.CaptureFrame(frame);
                Expect(RedIs(frame, near_cube.x, near_cube.y, 255, 3), "what casts no ambient occlusion leaves the floor white");
                const glm::ivec2 cube_face = ToPixel(camera, {0.0f, 0.5f, 0.5f});
                const std::uint8_t* face = &frame.pixels[static_cast<size_t>(cube_face.y * frame.width + cube_face.x) * 4];
                Expect(face[0] > 200 && face[1] < 30, "the cube that casts no ambient occlusion is still drawn");
            }

            // The back of a double-sided floor, seen from below with the sun above: lit as if
            // its normal pointed down (dark), or, with back_faces_keep_normal, by the sun (not
            // the full 255: seen from behind, the highlight term takes a share).
            {
                poligon::Camera below;
                below.position = {0.0f, -3.0f, 0.0f};
                below.target = {0.0f, 0.0f, 0.0f};
                below.up = {0.0f, 0.0f, -1.0f};
                poligon::DirectionalLight sun;
                sun.direction = {0.0f, -1.0f, 0.0f};
                sun.color = {1.0f, 1.0f, 1.0f, 1.0f};
                sun.ambient = {0.0f, 0.0f, 0.0f, 1.0f};
                poligon::Environment environment;
                environment.shadows = false;
                environment.foliage_translucency = 0.0f;
                environment.sky_light = false;
                for (int keep = 0; keep < 2; ++keep) {
                    poligon::Material sheet = white;
                    sheet.double_sided = true;
                    sheet.back_faces_keep_normal = keep == 1;
                    renderer.BeginFrame(below, sun, environment);
                    renderer.DrawMesh(plane, glm::scale(identity, {40.0f, 1.0f, 40.0f}), sheet);
                    renderer.EndFrame();
                    renderer.CaptureFrame(frame);
                    const int red = RedAt(frame, kSize / 2, kSize / 2);
                    if (keep == 0) {
                        Expect(red < 30, "the back of a double-sided surface faces away from the sun");
                    } else {
                        Expect(red > 150, "with back_faces_keep_normal the back is lit by the sun");
                    }
                    if ((keep == 0 && red >= 30) || (keep == 1 && red <= 150)) {
                        std::printf("  back face pixel has red %d\n", red);
                    }
                }
            }

            // Tinting by shadow cascade: with a 40 m range the cascades end at about 4.6, 9.9,
            // 19.5 and 40 m. The floor 3.2 m from the camera is shaded by the first cascade and
            // comes out red, the floor 23 m away by the last one and comes out yellow.
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
            const glm::ivec2 far_floor = ToPixel(camera, {0.0f, 0.0f, -20.0f});
            const std::uint8_t* near_pixel = &frame.pixels[static_cast<size_t>(near_floor.y * frame.width + near_floor.x) * 4];
            const std::uint8_t* far_pixel = &frame.pixels[static_cast<size_t>(far_floor.y * frame.width + far_floor.x) * 4];
            Expect(near_pixel[0] > near_pixel[1] + 40 && near_pixel[0] > near_pixel[2] + 40,
                   "the cascade view tints near floor red");
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

    // --- Far cascades at a smaller share of their image ---
    // A slab 3 m above the floor 20 m in front of the camera, with the sun straight
    // overhead, falls in the last cascade. With far_shadow_scale 0.5 that cascade uses a
    // quarter of its image; its shadow must still land right under the slab.
    {
        poligon::RendererConfig config = MakeConfig(backend);
        config.far_shadow_scale = 0.5f;
        poligon::Renderer far_renderer(config);
        const poligon::MeshHandle far_plane = far_renderer.CreatePlaneMesh();
        const poligon::MeshHandle far_cube = far_renderer.CreateCubeMesh();
        poligon::Camera camera;
        camera.position = {0.0f, 3.0f, 5.0f};
        camera.target = {0.0f, 0.3f, 0.0f};
        poligon::Environment environment;
        environment.shadow_distance = 40.0f;
        poligon::DirectionalLight sun;
        sun.direction = {0.0f, -1.0f, 0.0f};
        sun.color = {0.6f, 0.6f, 0.6f, 1.0f};
        sun.ambient = {0.2f, 0.2f, 0.2f, 1.0f};
        far_renderer.BeginFrame(camera, sun, environment);
        far_renderer.DrawMesh(far_plane, glm::scale(identity, {80.0f, 1.0f, 80.0f}), white);
        far_renderer.DrawMesh(far_cube,
                              glm::translate(identity, {0.0f, 3.0f, -20.0f}) * glm::scale(identity, {4.0f, 0.2f, 4.0f}),
                              white);
        far_renderer.EndFrame();
        far_renderer.CaptureFrame(frame);
        const glm::ivec2 shadowed = ToPixel(camera, {0.0f, 0.0f, -20.0f});
        const glm::ivec2 lit = ToPixel(camera, {6.0f, 0.0f, -20.0f});
        const int dark = RedAt(frame, shadowed.x, shadowed.y);
        const int bright = RedAt(frame, lit.x, lit.y);
        if (dark + 60 > bright) {
            std::printf("  floor under the far slab has red %d, open floor beside it %d\n", dark, bright);
        }
        Expect(dark + 60 <= bright, "a far cascade at half resolution puts the shadow under its caster");
    }

    // --- Clouds, their shadows, the haze of distance and sky occlusion ---
    {
        poligon::Renderer renderer(MakeConfig(backend));
        renderer.PrepareClouds();
        const poligon::MeshHandle plane = renderer.CreatePlaneMesh();
        poligon::DirectionalLight sun;
        sun.direction = {0.0f, -1.0f, 0.0f};
        sun.color = {1.0f, 1.0f, 1.0f, 1.0f};
        poligon::Environment environment;
        environment.draw_sky = true;
        environment.sky_light = true;
        environment.sky_light_intensity = 0.3f;
        environment.shadows = false;
        const auto mean_of = [](const poligon::Image& image) {
            double sum[3] = {0.0, 0.0, 0.0};
            for (size_t i = 0; i < image.pixels.size(); i += 4) {
                for (int c = 0; c < 3; ++c) {
                    sum[c] += image.pixels[i + static_cast<size_t>(c)];
                }
            }
            const double count = static_cast<double>(image.pixels.size() / 4);
            return glm::vec3(static_cast<float>(sum[0] / count), static_cast<float>(sum[1] / count), static_cast<float>(sum[2] / count));
        };

        // Looking straight up: a clear sky is blue; under full cover it turns grey-white.
        poligon::Camera up;
        up.position = {0.0f, 2.0f, 0.0f};
        up.target = {0.0f, 100.0f, 0.0f};
        up.up = {0.0f, 0.0f, -1.0f};
        poligon::Image clear_sky;
        poligon::Image cloudy_sky;
        for (int pass = 0; pass < 2; ++pass) {
            environment.clouds = pass == 1;
            environment.cloud_coverage = 1.0f;
            environment.cloud_density = 0.05f;
            renderer.BeginFrame(up, sun, environment);
            renderer.EndFrame();
            renderer.CaptureFrame(pass == 0 ? clear_sky : cloudy_sky);
        }
        const glm::vec3 clear = mean_of(clear_sky);
        const glm::vec3 cloudy = mean_of(cloudy_sky);
        if (!(clear.z > clear.x + 40.0f && cloudy.z - cloudy.x < (clear.z - clear.x) * 0.6f)) {
            std::printf("  clear sky %.0f %.0f %.0f, cloudy sky %.0f %.0f %.0f\n", clear.x, clear.y, clear.z, cloudy.x, cloudy.y, cloudy.z);
        }
        Expect(clear.z > clear.x + 40.0f, "a clear sky is blue");
        Expect(cloudy.z - cloudy.x < (clear.z - clear.x) * 0.6f, "under full cover the sky turns grey-white");

        // Seen from above, a floor in full sun goes darker in the shadow of full cover.
        poligon::Camera down;
        down.position = {0.0f, 30.0f, 0.0f};
        down.target = {0.0f, 0.0f, 0.0f};
        down.up = {0.0f, 0.0f, -1.0f};
        const poligon::Material floor = MakeMatte({0.8f, 0.8f, 0.8f, 1.0f});
        poligon::Image sunny;
        poligon::Image shaded;
        for (int pass = 0; pass < 2; ++pass) {
            environment.clouds = pass == 1;
            renderer.BeginFrame(down, sun, environment);
            renderer.DrawMesh(plane, glm::scale(identity, {200.0f, 1.0f, 200.0f}), floor);
            renderer.EndFrame();
            renderer.CaptureFrame(pass == 0 ? sunny : shaded);
        }
        const int lit = RedAt(sunny, kSize / 2, kSize / 2);
        const int under_cloud = RedAt(shaded, kSize / 2, kSize / 2);
        if (!(under_cloud < lit * 0.7f)) {
            std::printf("  floor %d in sun, %d under the clouds\n", lit, under_cloud);
        }
        Expect(under_cloud < lit * 0.7f, "clouds shade the land under them");
        environment.clouds = false;

        // The haze of distance: a white floor hundreds of meters away takes the horizon's color.
        poligon::Camera level;
        level.position = {0.0f, 3.0f, 0.0f};
        level.target = {0.0f, 3.0f, -100.0f};  // level: the middle row is the horizon
        level.far_plane = 6000.0f;
        environment.sky_horizon = {0.2f, 0.4f, 0.9f, 1.0f};
        environment.sky_light_intensity = 1.0f;
        poligon::Image hazy;
        environment.aerial_density = 0.01f;
        environment.sky_light = false;  // flat white light: the floor itself is white
        renderer.BeginFrame(level, MakeFlatLight(1.0f), environment);
        renderer.DrawMesh(plane, glm::scale(identity, {12000.0f, 1.0f, 12000.0f}), white);
        renderer.EndFrame();
        renderer.CaptureFrame(hazy);
        // Just below the horizon the floor is hundreds of meters away, at the bottom a few.
        const std::uint8_t* far_floor = &hazy.pixels[static_cast<size_t>((kSize / 2) * kSize + kSize / 2) * 4];
        const std::uint8_t* near_floor = &hazy.pixels[static_cast<size_t>((kSize - 2) * kSize + kSize / 2) * 4];
        if (!(far_floor[2] > far_floor[0] + 60 && near_floor[0] > far_floor[0] + 40)) {
            std::printf("  floor far %d %d %d, near %d %d %d\n", far_floor[0], far_floor[1], far_floor[2], near_floor[0], near_floor[1],
                        near_floor[2]);
        }
        Expect(far_floor[2] > far_floor[0] + 60, "far away the floor takes the haze's blue");
        Expect(near_floor[0] > far_floor[0] + 40, "near the camera the floor keeps its own white");
        environment.aerial_density = 0.0f;

        // Sky occlusion: under even sky light only, a floor whose map says it sees a fifth of
        // the sky is darker than open floor.
        environment.sky_horizon = {1.0f, 1.0f, 1.0f, 1.0f};
        environment.sky_zenith = {1.0f, 1.0f, 1.0f, 1.0f};
        environment.draw_sky = false;
        environment.sky_light = true;
        poligon::SkyOcclusionMap occlusion;
        occlusion.resolution = 64;
        occlusion.spacing = 1.0f;
        occlusion.origin = {-32.0f, -32.0f};
        occlusion.values.assign(64u * 64u, 255);
        for (int z = 0; z < 64; ++z) {
            for (int x = 32; x < 64; ++x) {
                occlusion.values[static_cast<size_t>(z * 64 + x)] = 51;  // the east half sees a fifth
            }
        }
        occlusion.version = 1;
        renderer.SetSkyOcclusion(&occlusion);
        poligon::DirectionalLight dark_sun;
        dark_sun.color = {0.0f, 0.0f, 0.0f, 1.0f};
        renderer.BeginFrame(down, dark_sun, environment);
        renderer.DrawMesh(plane, glm::scale(identity, {200.0f, 1.0f, 200.0f}), floor);
        renderer.EndFrame();
        renderer.CaptureFrame(frame);
        renderer.SetSkyOcclusion(nullptr);
        const int open = RedAt(frame, kSize / 4, kSize / 2);
        const int hidden = RedAt(frame, kSize * 3 / 4, kSize / 2);
        if (!(hidden < open * 0.5f && open > 60)) {
            std::printf("  open floor %d, floor that sees a fifth of the sky %d\n", open, hidden);
        }
        Expect(open > 60 && hidden < open * 0.5f, "floor that sees less sky gets less sky light");
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

    // --- Wind ---
    // A 10 m pole with a bending material, seen from the side. Without wind its top is at
    // x = 0. With wind blowing towards +x the top moves there (by how much depends on the
    // gust at the given moment, so several moments are tried) and never upwind.
    {
        poligon::Renderer renderer(MakeConfig(backend));
        const poligon::MeshVertex pole_vertices[4] = {{{-0.3f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}},
                                                      {{0.3f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},
                                                      {{0.3f, 10.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
                                                      {{-0.3f, 10.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}}};
        const std::uint32_t pole_indices[6] = {0, 1, 2, 0, 2, 3};
        const poligon::MeshHandle pole = renderer.CreateMesh(pole_vertices, 4, pole_indices, 6);
        poligon::Material bending = white;
        bending.double_sided = true;
        bending.wind_bend = 20.0f;
        poligon::Camera camera;
        camera.position = {0.0f, 5.0f, 30.0f};
        camera.target = {0.0f, 5.0f, 0.0f};
        const glm::ivec2 top = ToPixel(camera, {0.0f, 9.5f, 0.0f});
        const glm::ivec2 upwind = ToPixel(camera, {-1.5f, 9.5f, 0.0f});
        const auto is_white = [&frame](glm::ivec2 pixel) {
            return frame.pixels[static_cast<size_t>(pixel.y * frame.width + pixel.x) * 4] > 200;
        };

        poligon::Environment still;
        still.shadows = false;
        renderer.BeginFrame(camera, MakeFlatLight(1.0f), still);
        renderer.DrawMesh(pole, identity, bending);
        renderer.EndFrame();
        renderer.CaptureFrame(frame);
        Expect(is_white(top) && !is_white(upwind), "without wind the pole stands straight");

        poligon::Environment windy = still;
        windy.wind_direction = {1.0f, 0.0f};
        windy.wind_strength = 1.0f;
        bool moved = false;
        bool went_upwind = false;
        for (const float time : {0.0f, 0.9f, 1.8f, 2.7f}) {
            windy.wind_time = time;
            renderer.BeginFrame(camera, MakeFlatLight(1.0f), windy);
            renderer.DrawMesh(pole, identity, bending);
            renderer.EndFrame();
            renderer.CaptureFrame(frame);
            moved = moved || !is_white(top);
            went_upwind = went_upwind || is_white(upwind);
        }
        Expect(moved, "in the wind the top of the pole leaves its place");
        Expect(!went_upwind, "the pole never bends against the wind");

        // Without a wind response the same pole does not move.
        renderer.BeginFrame(camera, MakeFlatLight(1.0f), windy);
        renderer.DrawMesh(pole, identity, white);
        renderer.EndFrame();
        renderer.CaptureFrame(frame);
        Expect(is_white(top), "a material without a wind response ignores the wind");
    }

    std::printf("effects_render_test finished\n");
    return test::ExitCode();
}

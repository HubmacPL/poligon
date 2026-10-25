// Renders small scenes without a window and checks single pixels: ambient occlusion,
// volumetric fog with light shafts, bloom and automatic exposure.
// Usage: effects_render_test d3d12|vulkan
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "poligon/render/renderer.h"
#include "poligon/world/sky_occlusion.h"
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

// The pixel a world position is drawn at, computed the way the renderer does (square picture).
glm::ivec2 ToPixel(const poligon::Camera& camera, glm::vec3 position, int size = kSize) {
    const glm::mat4 view = glm::lookAt(camera.position, camera.target, camera.up);
    const glm::mat4 projection =
        glm::perspective(glm::radians(camera.vertical_fov_degrees), 1.0f, camera.near_plane, camera.far_plane);
    const glm::vec4 clip = projection * view * glm::vec4(position, 1.0f);
    return {static_cast<int>((clip.x / clip.w * 0.5f + 0.5f) * size),
            static_cast<int>((0.5f - clip.y / clip.w * 0.5f) * size)};
}

// An upright cylinder (side and top) standing on y = 0, with flat normals per side.
poligon::MeshHandle CreateCylinderMesh(poligon::Renderer& renderer, float radius, float height) {
    constexpr int kSides = 48;
    std::vector<poligon::MeshVertex> vertices;
    std::vector<std::uint32_t> indices;
    for (int i = 0; i <= kSides; ++i) {
        const float angle = glm::two_pi<float>() * static_cast<float>(i) / kSides;
        const glm::vec3 out(std::sin(angle), 0.0f, std::cos(angle));
        const float u = static_cast<float>(i) / kSides;
        vertices.push_back({out * radius, out, {u, 1.0f}});
        vertices.push_back({out * radius + glm::vec3(0.0f, height, 0.0f), out, {u, 0.0f}});
    }
    for (std::uint32_t i = 0; i < kSides; ++i) {
        const std::uint32_t a = i * 2;
        indices.insert(indices.end(), {a, a + 2, a + 3, a, a + 3, a + 1});
    }
    const auto center = static_cast<std::uint32_t>(vertices.size());
    vertices.push_back({{0.0f, height, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.5f, 0.5f}});
    for (int i = 0; i <= kSides; ++i) {
        const float angle = glm::two_pi<float>() * static_cast<float>(i) / kSides;
        vertices.push_back({{std::sin(angle) * radius, height, std::cos(angle) * radius}, {0.0f, 1.0f, 0.0f}, {0.5f, 0.5f}});
    }
    for (std::uint32_t i = 0; i < kSides; ++i) {
        indices.insert(indices.end(), {center, center + 1 + i, center + 2 + i});
    }
    return renderer.CreateMesh(vertices.data(), static_cast<int>(vertices.size()), indices.data(),
                               static_cast<int>(indices.size()));
}

// A stored (sRGB) channel value back to linear 0..1.
float ToLinear(int stored) {
    const float value = static_cast<float>(stored) / 255.0f;
    return value <= 0.04045f ? value / 12.92f : std::pow((value + 0.055f) / 1.055f, 2.4f);
}

// The mean linear red of five pixels in a row around `pixel`.
float MeanLinearRed(const poligon::Image& image, glm::ivec2 pixel) {
    float sum = 0.0f;
    for (int dx = -2; dx <= 2; ++dx) {
        sum += ToLinear(RedAt(image, pixel.x + dx, pixel.y));
    }
    return sum / 5.0f;
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

                // A wall facing the camera, with the sun straight above: edge-on to the sun
                // it gets no sunlight, unless normal_up turns its normal up (grass cards).
                poligon::Camera front;
                front.position = {0.0f, 0.0f, 3.0f};
                front.target = {0.0f, 0.0f, 0.0f};
                const glm::mat4 wall = glm::scale(glm::rotate(identity, glm::radians(90.0f), {1.0f, 0.0f, 0.0f}), {40.0f, 1.0f, 40.0f});
                for (int up = 0; up < 2; ++up) {
                    poligon::Material card = white;
                    card.normal_up = up == 1 ? 1.0f : 0.0f;
                    renderer.BeginFrame(front, sun, environment);
                    renderer.DrawMesh(plane, wall, card);
                    renderer.EndFrame();
                    renderer.CaptureFrame(frame);
                    const int red = RedAt(frame, kSize / 2, kSize / 2);
                    if ((up == 0 && red >= 30) || (up == 1 && red <= 200)) {
                        std::printf("  wall pixel has red %d\n", red);
                    }
                    Expect(up == 0 ? red < 30 : red > 200, up == 0 ? "a wall edge-on to the sun gets no sunlight"
                                                                   : "with normal_up 1 the wall is lit like the ground");
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

    // --- Ambient occlusion at the foot of a thin trunk ---
    // A cylinder 0.3 m across standing on a floor, seen from 2 m up at a slant, sun off.
    // The floor right at its foot must be shaded, not black: SAO's per-sample term grows
    // like 1 / distance, so without limits a surface a centimeter away drove it to zero.
    {
        constexpr int kAoSize = 512;
        poligon::RendererConfig config = MakeConfig(backend);
        config.width = kAoSize;
        config.height = kAoSize;
        poligon::Renderer renderer(config);
        const poligon::MeshHandle plane = renderer.CreatePlaneMesh();
        const poligon::MeshHandle cylinder = CreateCylinderMesh(renderer, 0.15f, 1.0f);
        poligon::Camera camera;
        camera.position = {0.0f, 2.0f, 2.2f};
        camera.target = {0.0f, 0.0f, 0.6f};
        camera.vertical_fov_degrees = 30.0f;
        poligon::Environment environment;
        environment.ambient_occlusion = true;
        const glm::ivec2 one_cm = ToPixel(camera, {0.0f, 0.0f, 0.16f}, kAoSize);
        const glm::ivec2 two_cm = ToPixel(camera, {0.0f, 0.0f, 0.17f}, kAoSize);
        const glm::ivec2 open = ToPixel(camera, {0.0f, 0.0f, 1.15f}, kAoSize);
        const auto draw = [&](poligon::DebugView view) {
            poligon::PostSettings post = renderer.GetPostSettings();
            post.debug_view = view;
            renderer.SetPostSettings(post);
            renderer.BeginFrame(camera, MakeFlatLight(0.5f), environment);
            renderer.DrawMesh(plane, glm::scale(identity, {40.0f, 1.0f, 40.0f}), white);
            renderer.DrawMesh(cylinder, identity, white);
            renderer.EndFrame();
            renderer.CaptureFrame(frame);
        };
        draw(poligon::DebugView::AmbientOcclusion);
        const float ao_one_cm = MeanLinearRed(frame, one_cm);
        const float ao_two_cm = MeanLinearRed(frame, two_cm);
        const float ao_open = MeanLinearRed(frame, open);
        draw(poligon::DebugView::None);
        const float lit_two_cm = MeanLinearRed(frame, two_cm);
        const float lit_open = MeanLinearRed(frame, open);
        std::printf("  ambient occlusion at a trunk's foot: 1 cm %.3f, 2 cm %.3f, open floor %.3f; lit floor at 2 cm %.3f of open\n",
                    ao_one_cm, ao_two_cm, ao_open, lit_two_cm / glm::max(lit_open, 0.0001f));
        Expect(ao_two_cm >= 0.3f && ao_two_cm <= 0.7f, "the floor 2 cm from a trunk is shaded, not black");
        Expect(ao_one_cm >= 0.25f, "the floor 1 cm from a trunk is not black either");
        Expect(ao_open > 0.95f, "open floor 1 m from the trunk is not shaded");
        Expect(lit_two_cm > 0.2f * lit_open, "lit by ambient light only, the floor at the trunk's foot is not black");

        // Next to a wall far taller and wider than the radius the floor sees exactly half the
        // sky (cosine-weighted); the estimate is scaled to give about that.
        const glm::mat4 wall = glm::translate(identity, {0.0f, 2.0f, 0.0f}) * glm::scale(identity, {8.0f, 4.0f, 0.3f});
        const poligon::MeshHandle cube = renderer.CreateCubeMesh();
        poligon::PostSettings post = renderer.GetPostSettings();
        post.debug_view = poligon::DebugView::AmbientOcclusion;
        renderer.SetPostSettings(post);
        renderer.BeginFrame(camera, MakeFlatLight(0.5f), environment);
        renderer.DrawMesh(plane, glm::scale(identity, {40.0f, 1.0f, 40.0f}), white);
        renderer.DrawMesh(cube, wall, white);
        renderer.EndFrame();
        renderer.CaptureFrame(frame);
        const float ao_wall = MeanLinearRed(frame, one_cm);
        std::printf("  ambient occlusion 1 cm from a tall wall %.3f\n", ao_wall);
        Expect(ao_wall >= 0.35f && ao_wall <= 0.65f, "the floor at a tall wall sees about half the sky");
    }

    // --- Normals under a non-uniform scale: a cube turned 45 degrees about z and then
    // stretched 3 times along y (as under a parent with that scale) is a slanted prism. The
    // sun shines straight down. The face that was +x has the normal S^-1 R (1, 0, 0) =
    // (1, 1/3, 0) normalized, so its brightness is n.y = 0.316 of the sun; turning the
    // normal with the model matrix itself would give (1, 3, 0) normalized, 0.949. ---
    {
        poligon::Renderer renderer(MakeConfig(backend));
        const poligon::MeshHandle cube = renderer.CreateCubeMesh();
        const glm::mat4 prism = glm::translate(identity, {0.0f, 2.2f, 0.0f}) * glm::scale(identity, {1.0f, 3.0f, 1.0f}) *
                                glm::rotate(identity, glm::radians(45.0f), {0.0f, 0.0f, 1.0f});
        const glm::vec3 face_center = glm::vec3(prism * glm::vec4(0.5f, 0.0f, 0.0f, 1.0f));
        const glm::vec3 expected_normal = glm::normalize(glm::transpose(glm::inverse(glm::mat3(prism))) * glm::vec3(1.0f, 0.0f, 0.0f));
        poligon::Camera camera;
        camera.position = face_center + expected_normal * 3.0f;
        camera.target = face_center;
        poligon::DirectionalLight sun;
        sun.direction = {0.0f, -1.0f, 0.0f};
        sun.color = {1.0f, 1.0f, 1.0f, 1.0f};
        sun.ambient = {0.0f, 0.0f, 0.0f, 1.0f};
        poligon::Environment environment;
        environment.shadows = false;
        renderer.BeginFrame(camera, sun, environment);
        renderer.DrawMesh(cube, prism, white);
        renderer.EndFrame();
        renderer.CaptureFrame(frame);
        const float lit = MeanLinearRed(frame, ToPixel(camera, face_center));
        std::printf("  slanted face of a stretched, turned cube: %.3f of the sun (expected %.3f)\n", lit, expected_normal.y);
        // The rest is the highlight and the light it takes from the diffuse part (a few hundredths).
        Expect(std::abs(lit - expected_normal.y) < 0.05f, "normals stay square to the surface under a non-uniform scale");
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

    // --- Clouds built up over frames (each frame walks a quarter of the pixels): the same
    // frames give the same picture in a fresh renderer, and the built-up picture stays close
    // to one with every pixel walked (the first frame). One renderer at a time. ---
    {
        poligon::DirectionalLight sun;
        sun.direction = {0.0f, -1.0f, 0.0f};
        sun.color = {1.0f, 1.0f, 1.0f, 1.0f};
        poligon::Environment environment;
        environment.draw_sky = true;
        environment.sky_light = true;
        environment.sky_light_intensity = 0.3f;
        environment.shadows = false;
        environment.clouds = true;
        environment.cloud_coverage = 0.5f;
        poligon::Camera tilted;
        tilted.position = {0.0f, 2.0f, 0.0f};
        tilted.target = {0.0f, 40.0f, -100.0f};
        const auto render_frames = [&](int frames, poligon::Image& image) {
            poligon::Renderer clouds_renderer(MakeConfig(backend));
            clouds_renderer.PrepareClouds();
            for (int i = 0; i < frames; ++i) {
                clouds_renderer.BeginFrame(tilted, sun, environment);
                clouds_renderer.EndFrame();
            }
            clouds_renderer.CaptureFrame(image);
        };
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
        poligon::Image first;
        poligon::Image built;
        poligon::Image built_again;
        render_frames(1, first);
        render_frames(12, built);
        render_frames(12, built_again);
        Expect(built.pixels == built_again.pixels, "clouds built over frames repeat to the bit");
        const glm::vec3 one = mean_of(first);
        const glm::vec3 many = mean_of(built);
        const float difference = glm::length(one - many);
        if (difference >= 6.0f) {
            std::printf("  sky after one frame %.1f %.1f %.1f, after twelve %.1f %.1f %.1f\n", one.x, one.y, one.z, many.x, many.y, many.z);
        }
        Expect(difference < 6.0f, "clouds built over frames look like clouds walked in one");
    }

    // --- The sky light map: with sky light, a surface takes the light of the sky it faces. Sky
    // above blue, ground below red, no sun: a floor facing up turns blue, a ceiling seen from
    // below red; a smooth metal floor mirrors the blue sky. ---
    {
        poligon::Renderer renderer(MakeConfig(backend));
        const poligon::MeshHandle plane = renderer.CreatePlaneMesh();
        poligon::DirectionalLight sun;
        sun.color = {0.0f, 0.0f, 0.0f, 1.0f};
        poligon::Environment environment;
        environment.sky_light = true;
        environment.sky_light_intensity = 1.0f;
        environment.sky_zenith = {0.0f, 0.0f, 1.0f, 1.0f};
        environment.sky_horizon = {0.0f, 0.0f, 1.0f, 1.0f};
        environment.sky_ground = {1.0f, 0.0f, 0.0f, 1.0f};
        environment.shadows = false;
        const auto middle = [](const poligon::Image& image) {
            const std::uint8_t* pixel = &image.pixels[static_cast<size_t>((kSize / 2) * kSize + kSize / 2) * 4];
            return glm::ivec3(pixel[0], pixel[1], pixel[2]);
        };
        poligon::Camera above = MakeTopDownCamera(3.0f);
        renderer.BeginFrame(above, sun, environment);
        renderer.DrawMesh(plane, glm::scale(identity, {40.0f, 1.0f, 40.0f}), white);
        renderer.EndFrame();
        renderer.CaptureFrame(frame);
        const glm::ivec3 floor_color = middle(frame);

        poligon::Camera below;
        below.position = {0.0f, -3.0f, 0.0f};
        below.target = {0.0f, 0.0f, 0.0f};
        below.up = {0.0f, 0.0f, -1.0f};
        // A ceiling: the plane turned upside down, facing the ground.
        const glm::mat4 ceiling = glm::rotate(identity, glm::radians(180.0f), {1.0f, 0.0f, 0.0f}) * glm::scale(identity, {40.0f, 1.0f, 40.0f});
        renderer.BeginFrame(below, sun, environment);
        renderer.DrawMesh(plane, ceiling, white);
        renderer.EndFrame();
        renderer.CaptureFrame(frame);
        const glm::ivec3 ceiling_color = middle(frame);

        poligon::Material metal = MakeMatte({1.0f, 1.0f, 1.0f, 1.0f});
        metal.metallic = 1.0f;
        metal.roughness = 0.1f;
        renderer.BeginFrame(above, sun, environment);
        renderer.DrawMesh(plane, glm::scale(identity, {40.0f, 1.0f, 40.0f}), metal);
        renderer.EndFrame();
        renderer.CaptureFrame(frame);
        const glm::ivec3 mirror_color = middle(frame);
        // The map is small (a matte surface's light at 4 x 4 per face), so a little of the other
        // half's color reaches a surface: at most 8% (80 of 255 after sRGB encoding).
        const bool ok = floor_color.z > 150 && floor_color.x < 80 && ceiling_color.x > 150 && ceiling_color.z < 110 &&
                        mirror_color.z > 150 && mirror_color.x < 40;
        if (!ok) {
            std::printf("  floor %d %d %d, ceiling %d %d %d, metal floor %d %d %d\n", floor_color.x, floor_color.y,
                        floor_color.z, ceiling_color.x, ceiling_color.y, ceiling_color.z, mirror_color.x, mirror_color.y,
                        mirror_color.z);
        }
        Expect(floor_color.z > 150 && floor_color.x < 80, "a floor facing up takes the sky's light");
        Expect(ceiling_color.x > 150 && ceiling_color.z < 110, "a ceiling facing down takes the ground's light");
        Expect(mirror_color.z > 150 && mirror_color.x < 40, "a smooth metal floor mirrors the sky");

        // Thick simple fog of a red color over a far floor: with fog_sky_color the fog takes the
        // blue sky's color instead.
        poligon::Camera level;
        level.position = {0.0f, 2.0f, 0.0f};
        level.target = {0.0f, 1.5f, -100.0f};
        level.far_plane = 2000.0f;
        environment.fog_color = {1.0f, 0.0f, 0.0f, 1.0f};
        environment.fog_density = 0.05f;
        glm::ivec3 fogged[2];
        for (int sky_color = 0; sky_color < 2; ++sky_color) {
            environment.fog_sky_color = static_cast<float>(sky_color);
            renderer.BeginFrame(level, sun, environment);
            renderer.DrawMesh(plane, glm::scale(identity, {2000.0f, 1.0f, 2000.0f}), MakeMatte({1.0f, 1.0f, 1.0f, 1.0f}));
            renderer.EndFrame();
            renderer.CaptureFrame(frame);
            const std::uint8_t* pixel = &frame.pixels[static_cast<size_t>((kSize / 2 + 8) * kSize + kSize / 2) * 4];  // floor about 60 m away
            fogged[sky_color] = glm::ivec3(pixel[0], pixel[1], pixel[2]);
        }
        if (!(fogged[0].x > fogged[0].z * 2 && fogged[1].z > fogged[1].x * 2)) {
            std::printf("  fogged floor %d %d %d, with the sky's color %d %d %d\n", fogged[0].x, fogged[0].y, fogged[0].z,
                        fogged[1].x, fogged[1].y, fogged[1].z);
        }
        Expect(fogged[0].x > fogged[0].z * 2, "fog has its own color");
        Expect(fogged[1].z > fogged[1].x * 2, "with fog_sky_color the fog takes the sky's color");
    }

    // --- Shadows that soften with distance from their caster: a plate 10 m above the floor,
    // the sun straight above; the shadow's edge on the floor, seen from 0.4 m above it, is
    // wider with a sun of 3 degrees than with the fixed filter, and stays the same from frame
    // to frame. ---
    {
        poligon::Renderer renderer(MakeConfig(backend));
        const poligon::MeshHandle plane = renderer.CreatePlaneMesh();
        poligon::DirectionalLight sun;
        sun.direction = {0.0f, -1.0f, 0.0f};
        sun.color = {1.0f, 1.0f, 1.0f, 1.0f};
        sun.ambient = {0.0f, 0.0f, 0.0f, 1.0f};
        poligon::Environment environment;
        environment.shadow_distance = 30.0f;
        poligon::Camera down = MakeTopDownCamera(0.4f);
        const poligon::Material floor_material = MakeMatte({1.0f, 1.0f, 1.0f, 1.0f});
        poligon::Material plate_material = MakeMatte({1.0f, 1.0f, 1.0f, 1.0f});
        plate_material.double_sided = false;
        // The plate covers x < 0, 10 m up, facing down (so the camera below sees nothing of it).
        const glm::mat4 plate = glm::translate(identity, {-10.0f, 10.0f, 0.0f}) *
                                glm::rotate(identity, glm::radians(180.0f), {1.0f, 0.0f, 0.0f}) * glm::scale(identity, {20.0f, 1.0f, 40.0f});
        int edge_widths[2] = {0, 0};
        poligon::Image first;
        for (int soft = 0; soft < 2; ++soft) {
            environment.sun_angular_size = soft == 1 ? 3.0f : 0.0f;
            for (int i = 0; i < 2; ++i) {
                renderer.BeginFrame(down, sun, environment);
                renderer.DrawMesh(plane, glm::scale(identity, {40.0f, 1.0f, 40.0f}), floor_material);
                renderer.DrawMesh(plane, plate, plate_material);
                renderer.EndFrame();
                renderer.CaptureFrame(frame);
                if (i == 0) {
                    first = frame;
                }
            }
            if (soft == 1) {
                Expect(first.pixels == frame.pixels, "soft shadows stay the same from frame to frame");
            }
            // Pixels across the edge (along x, through the middle row) that are neither lit nor dark.
            for (int x = 0; x < kSize; ++x) {
                const int red = RedAt(frame, x, kSize / 2);
                if (red > 25 && red < 230) {
                    ++edge_widths[soft];
                }
            }
        }
        if (!(edge_widths[1] > edge_widths[0] * 3 / 2)) {
            std::printf("  shadow edge %d pixels wide with the fixed filter, %d with a 3 degree sun\n", edge_widths[0],
                        edge_widths[1]);
        }
        Expect(edge_widths[1] > edge_widths[0] * 3 / 2, "a shadow cast from 10 m up is softer with a sun of some size");
    }

    // --- Motion vectors: with temporal anti-aliasing the depth pass writes each pixel's motion
    // since the previous frame. Seen in the motion debug view from above: a box moved with an
    // object id shows its motion, the same box moved without one and the still floor show
    // none, and plants bending in a changing wind show motion of their own. ---
    {
        poligon::RendererConfig config = MakeConfig(backend);
        config.post.temporal_aa = true;
        config.post.debug_view = poligon::DebugView::Motion;
        poligon::Renderer renderer(config);
        const poligon::MeshHandle plane = renderer.CreatePlaneMesh();
        const poligon::MeshHandle cube = renderer.CreateCubeMesh();
        const poligon::DirectionalLight sun = MakeFlatLight(30.0f);
        poligon::Environment environment;
        environment.wind_strength = 3.0f;
        poligon::Material grass = MakeMatte({1.0f, 1.0f, 1.0f, 1.0f});
        grass.wind_bend = 1.0f;
        const poligon::Camera camera = MakeTopDownCamera(12.0f);
        for (int i = 0; i < 3; ++i) {
            environment.wind_time = static_cast<float>(i) * 1.0f;
            renderer.BeginFrame(camera, sun, environment);
            renderer.DrawMesh(plane, glm::scale(identity, {40.0f, 1.0f, 40.0f}), MakeMatte({1.0f, 1.0f, 1.0f, 1.0f}));
            const float x = static_cast<float>(i) * 0.5f;
            renderer.DrawMesh(cube, glm::translate(identity, {x - 3.0f, 0.5f, -2.0f}), MakeMatte({1.0f, 1.0f, 1.0f, 1.0f}), 7);
            renderer.DrawMesh(cube, glm::translate(identity, {x - 3.0f, 0.5f, 2.0f}), MakeMatte({1.0f, 1.0f, 1.0f, 1.0f}));
            // A tall thin plant, which bends in the wind (its top moves between frames).
            const glm::mat4 plant = glm::translate(identity, {3.0f, 2.0f, 0.0f}) * glm::scale(identity, {1.0f, 4.0f, 1.0f});
            renderer.DrawMeshInstances(cube, grass, &plant, 1);
            renderer.EndFrame();
        }
        renderer.CaptureFrame(frame);
        const glm::ivec2 tracked = ToPixel(camera, {-2.0f, 1.0f, -2.0f});
        const glm::ivec2 untracked = ToPixel(camera, {-2.0f, 1.0f, 2.0f});
        const glm::ivec2 floor_pixel = ToPixel(camera, {0.0f, 0.0f, 4.0f});
        const glm::ivec2 plant_top = ToPixel(camera, {3.0f, 3.9f, 0.0f});
        const int tracked_red = RedAt(frame, tracked.x, tracked.y);
        const int untracked_red = RedAt(frame, untracked.x, untracked.y);
        const int floor_red = RedAt(frame, floor_pixel.x, floor_pixel.y);
        const int plant_red = RedAt(frame, plant_top.x, plant_top.y);
        const int plant_green = frame.pixels[static_cast<size_t>(plant_top.y * kSize + plant_top.x) * 4 + 1];
        std::printf("  motion view: tracked box %d, untracked box %d, floor %d, plant top %d %d\n", tracked_red, untracked_red,
                    floor_red, plant_red, plant_green);
        // 0.5 m to the right per frame seen from 12 m up: moved right, so it was to the left (red lower).
        Expect(std::abs(floor_red - 188) <= 4, "the still floor has no motion");
        Expect(std::abs(untracked_red - 188) <= 4, "a box moved without an object id has no motion of its own");
        Expect(tracked_red < 150, "a box moved with an object id has motion vectors");
        Expect(std::abs(plant_red - 188) > 4 || std::abs(plant_green - 188) > 4, "a plant in the wind has motion vectors");
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
    // target brightness (0.25, stored as 137). Then the light changes and the eye adapts over
    // half a second of game time, in 30 frames of 1/60 s or in 15 of 1/30 s: both must end at
    // the same picture, the one the time constant gives. Darker (light to a quarter) with a
    // time constant of 1 s: the eye is at 1/4 + 3/4 * exp(-0.5) = 0.705 of the old brightness,
    // the picture at 0.25 * 0.25 / 0.705 = 0.089. Brighter (light times 4) with 0.25 s: at
    // 4 - 3 * exp(-2) = 3.59, the picture at 0.25 * 4 / 3.59 = 0.279.
    for (const float brightness : {8.0f, 0.02f}) {
        for (const float change : {0.25f, 4.0f}) {
            float adapted[2] = {0.0f, 0.0f};
            for (int run = 0; run < 2; ++run) {
                poligon::RendererConfig config = MakeConfig(backend);
                config.post.auto_exposure = true;
                config.post.auto_exposure_target = 0.25f;
                config.post.auto_exposure_min = 0.0001f;
                config.post.auto_exposure_max = 1000.0f;
                config.post.auto_exposure_darker_seconds = 1.0f;
                config.post.auto_exposure_brighter_seconds = 0.25f;
                poligon::Renderer renderer(config);
                const poligon::MeshHandle plane = renderer.CreatePlaneMesh();
                const poligon::Camera camera = MakeTopDownCamera(5.0f);
                const glm::mat4 floor = glm::scale(identity, {40.0f, 1.0f, 40.0f});

                renderer.BeginFrame(camera, MakeFlatLight(brightness));
                renderer.DrawMesh(plane, floor, white);
                renderer.EndFrame();
                renderer.CaptureFrame(frame);
                Expect(RedIs(frame, kSize / 2, kSize / 2, 137, 3), "automatic exposure brings the picture to the target brightness");

                const int frames = run == 0 ? 30 : 15;
                poligon::Environment environment;
                environment.frame_seconds = 0.5f / static_cast<float>(frames);
                for (int i = 0; i < frames; ++i) {
                    renderer.BeginFrame(camera, MakeFlatLight(brightness * change), environment);
                    renderer.DrawMesh(plane, floor, white);
                    renderer.EndFrame();
                }
                renderer.CaptureFrame(frame);
                adapted[run] = ToLinear(RedAt(frame, kSize / 2, kSize / 2));
            }
            const float eye = change < 1.0f ? change + (1.0f - change) * std::exp(-0.5f / 1.0f)
                                            : change + (1.0f - change) * std::exp(-0.5f / 0.25f);
            const float expected = 0.25f * change / eye;
            if (std::abs(adapted[0] - expected) > 0.01f || std::abs(adapted[1] - expected) > 0.01f) {
                std::printf("  light x%.2f: after 0.5 s in 30 frames %.4f, in 15 frames %.4f, expected %.4f\n", change, adapted[0],
                            adapted[1], expected);
            }
            Expect(std::abs(adapted[0] - expected) <= 0.01f && std::abs(adapted[1] - expected) <= 0.01f,
                   change < 1.0f ? "in the dark the eye adapts by its time constant, at any frame rate"
                                 : "in brighter light the eye adapts by its faster time constant, at any frame rate");
        }
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

    // --- A change of size between frames (Renderer::SetWindowMode, without a window) ---
    // Every image that depends on the size is made again: the picture comes out at the new
    // size, with ambient occlusion, fog and anti-aliasing working as before.
    {
        poligon::RendererConfig config = MakeConfig(backend);
        config.post.temporal_aa = true;
        poligon::Renderer renderer(config);
        const poligon::MeshHandle plane = renderer.CreatePlaneMesh();
        const poligon::MeshHandle cube = renderer.CreateCubeMesh();
        poligon::Camera camera;
        camera.position = {0.0f, 3.0f, 5.0f};
        camera.target = {0.0f, 0.3f, 0.0f};
        poligon::Environment environment;
        environment.ambient_occlusion = true;
        environment.volumetric_fog = true;
        environment.fog_density = 0.01f;
        const auto draw = [&] {
            renderer.BeginFrame(camera, MakeFlatLight(1.0f), environment);
            renderer.DrawMesh(plane, glm::scale(identity, {40.0f, 1.0f, 40.0f}), white);
            renderer.DrawMesh(cube, glm::translate(identity, {0.0f, 0.5f, 0.0f}), MakeMatte({1.0f, 0.0f, 0.0f, 1.0f}));
            renderer.EndFrame();
            renderer.CaptureFrame(frame);
        };
        draw();
        Expect(renderer.GetWindowMode() == poligon::WindowMode::Windowed, "a renderer starts windowed");
        Expect(!renderer.SetWindowMode(poligon::WindowMode::Windowed, 8, 8), "a size below 16 pixels is refused");
        Expect(renderer.GetWidth() == kSize, "a refused size changes nothing");
        Expect(renderer.SetWindowMode(poligon::WindowMode::Fullscreen, 384, 192), "the size can change between frames");
        draw();
        Expect(renderer.GetWindowMode() == poligon::WindowMode::Fullscreen && renderer.GetWidth() == 384 &&
                   renderer.GetHeight() == 192 && frame.width == 384 && frame.height == 192,
               "the next frame has the new size");
        // The cube stays in the middle of the picture, red, whatever the size.
        const std::uint8_t* middle = &frame.pixels[static_cast<size_t>((frame.height / 2) * frame.width + frame.width / 2) * 4];
        Expect(middle[0] > 120 && middle[1] < 60, "the picture at the new size shows the scene");
        Expect(renderer.SetWindowMode(poligon::WindowMode::Windowed, kSize, kSize), "and back");
        draw();
        Expect(frame.width == kSize && frame.height == kSize, "back at the first size");
    }

    // --- Resize (what BeginFrame does when the window is dragged larger or maximized): after
    // it, a scene looks exactly as in a renderer made at the new size from the start. ---
    {
        poligon::Camera camera;
        camera.position = {3.0f, 2.5f, 4.0f};
        camera.target = {0.0f, 0.5f, 0.0f};
        const auto draw = [&](poligon::Renderer& renderer, poligon::Image& image) {
            const poligon::MeshHandle plane = renderer.CreatePlaneMesh();
            const poligon::MeshHandle cube = renderer.CreateCubeMesh();
            renderer.BeginFrame(camera, poligon::DirectionalLight{});
            renderer.DrawMesh(plane, glm::scale(identity, {10.0f, 1.0f, 10.0f}), poligon::Color{0.4f, 0.4f, 0.4f, 1.0f});
            renderer.DrawMesh(cube, glm::translate(identity, {0.0f, 0.5f, 0.0f}), poligon::Color{0.9f, 0.5f, 0.1f, 1.0f});
            renderer.EndFrame();
            renderer.CaptureFrame(image);
        };
        poligon::Image resized;
        poligon::Image direct;
        {
            poligon::Renderer renderer(MakeConfig(backend));
            draw(renderer, resized);
            Expect(!renderer.Resize(8, 8), "Resize refuses a size below 16 pixels");
            Expect(renderer.Resize(400, 160), "Resize accepts a wider picture");
            draw(renderer, resized);
        }
        {
            poligon::RendererConfig config = MakeConfig(backend);
            config.width = 400;
            config.height = 160;
            poligon::Renderer renderer(config);
            draw(renderer, direct);
        }
        Expect(resized.width == 400 && resized.height == 160, "after Resize the captured frame has the new size");
        int differing = 0;
        int largest = 0;
        if (resized.pixels.size() == direct.pixels.size()) {
            for (size_t i = 0; i < resized.pixels.size(); ++i) {
                const int difference = std::abs(static_cast<int>(resized.pixels[i]) - static_cast<int>(direct.pixels[i]));
                largest = std::max(largest, difference);
                differing += difference > 2 ? 1 : 0;
            }
        }
        if (largest > 2) {
            std::printf("  resized against direct: %d channel values differ by more than 2, the largest by %d\n", differing, largest);
        }
        Expect(resized.pixels.size() == direct.pixels.size() && differing <= static_cast<int>(resized.pixels.size() / 1000),
               "a resized renderer draws what one made at that size does (proportions included)");
    }

    std::printf("effects_render_test finished\n");
    return test::ExitCode();
}

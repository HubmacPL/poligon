// Renders textured squares without a window and checks individual pixels of the result.
// Usage: texture_render_test d3d12|vulkan
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "poligon/render/renderer.h"
#include "test_support.h"

namespace {

constexpr int kSize = 256;

bool PixelIs(const poligon::Image& image, int x, int y, int r, int g, int b, int tolerance) {
    const std::uint8_t* pixel = &image.pixels[static_cast<size_t>(y * image.width + x) * 4];
    const bool match = std::abs(pixel[0] - r) <= tolerance && std::abs(pixel[1] - g) <= tolerance &&
                       std::abs(pixel[2] - b) <= tolerance;
    if (!match) {
        std::printf("  pixel (%d, %d) is (%d, %d, %d), expected about (%d, %d, %d)\n", x, y, pixel[0], pixel[1],
                    pixel[2], r, g, b);
    }
    return match;
}

int RedAt(const poligon::Image& image, int x, int y) {
    return image.pixels[static_cast<size_t>(y * image.width + x) * 4];
}

// A camera straight above the origin looking down, with -Z at the top of the picture. The
// plane mesh then appears as an upright picture: its texture is seen the way the image file looks.
poligon::Camera MakeTopDownCamera(float height) {
    poligon::Camera camera;
    camera.position = {0.0f, height, 0.0f};
    camera.target = {0.0f, 0.0f, 0.0f};
    camera.up = {0.0f, 0.0f, -1.0f};
    camera.vertical_fov_degrees = 60.0f;
    return camera;
}

// Converts a point on the ground (x, z) to the pixel it is drawn at by MakeTopDownCamera(5).
int ToPixel(float ground_coordinate) {
    const float visible_half_extent = 5.0f * 0.57735027f;  // height * tan(30 degrees)
    return static_cast<int>(kSize * 0.5f + ground_coordinate / visible_half_extent * kSize * 0.5f);
}

} // namespace

int main(int argc, char** argv) {
    using test::Expect;

    poligon::RendererConfig config;
    config.width = kSize;
    config.height = kSize;
    config.headless = true;
    config.enable_validation = true;
    config.clear_color = {0.2f, 0.2f, 0.2f, 1.0f};
    // No tone mapping: with flat lighting a pixel then shows exactly the material color.
    config.post.tone_mapping = poligon::ToneMapping::None;
    if (argc == 2 && std::strcmp(argv[1], "d3d12") == 0) {
        config.backend = poligon::GraphicsBackend::Direct3D12;
    } else if (argc == 2 && std::strcmp(argv[1], "vulkan") == 0) {
        config.backend = poligon::GraphicsBackend::Vulkan;
    } else {
        std::printf("usage: texture_render_test d3d12|vulkan\n");
        return 2;
    }
    poligon::Renderer renderer(config);
    const poligon::MeshHandle plane = renderer.CreatePlaneMesh();

    // No directional light and full ambient light: a pixel shows exactly the material color.
    poligon::DirectionalLight flat_light;
    flat_light.color = {0.0f, 0.0f, 0.0f, 1.0f};
    flat_light.ambient = {1.0f, 1.0f, 1.0f, 1.0f};

    // A 2x2 image: red and green on top, blue and white below.
    poligon::Image quad_image = poligon::MakeSolidImage(2, 2, 255, 255, 255);
    const std::uint8_t corner_colors[4][3] = {{255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {255, 255, 255}};
    for (int i = 0; i < 4; ++i) {
        std::memcpy(&quad_image.pixels[static_cast<size_t>(i) * 4], corner_colors[i], 3);
    }
    poligon::TextureOptions no_mipmaps;
    no_mipmaps.generate_mipmaps = false;
    const poligon::TextureHandle quad_texture = renderer.CreateTexture(quad_image, no_mipmaps);

    // The plane is scaled to 4x4 units, so each quarter is 2x2 and has its center 1 unit from the middle.
    const glm::mat4 four_by_four = glm::scale(glm::mat4(1.0f), {4.0f, 1.0f, 4.0f});
    poligon::Image frame;

    {
        poligon::Material material;
        material.texture = quad_texture;
        renderer.BeginFrame(MakeTopDownCamera(5.0f), flat_light);
        renderer.DrawMesh(plane, glm::dvec3(0.0), four_by_four, material);
        renderer.EndFrame();
        renderer.CaptureFrame(frame);

        Expect(frame.width == kSize && frame.height == kSize, "the captured frame has the renderer's size");
        Expect(PixelIs(frame, 4, 4, 51, 51, 51, 1), "outside the plane the frame has the clear color, unchanged");
        Expect(PixelIs(frame, ToPixel(-1), ToPixel(-1), 255, 0, 0, 1), "top-left of the image is drawn top-left (red)");
        Expect(PixelIs(frame, ToPixel(1), ToPixel(-1), 0, 255, 0, 1), "top-right is green");
        Expect(PixelIs(frame, ToPixel(-1), ToPixel(1), 0, 0, 255, 1), "bottom-left is blue");
        Expect(PixelIs(frame, ToPixel(1), ToPixel(1), 255, 255, 255, 1), "bottom-right is white");
    }

    {
        // The material color multiplies the texture. Half of full red in linear light is 188 when stored.
        poligon::Material material;
        material.texture = quad_texture;
        material.color = {0.5f, 1.0f, 1.0f, 1.0f};
        renderer.BeginFrame(MakeTopDownCamera(5.0f), flat_light);
        renderer.DrawMesh(plane, glm::dvec3(0.0), four_by_four, material);
        renderer.EndFrame();
        renderer.CaptureFrame(frame);
        Expect(PixelIs(frame, ToPixel(-1), ToPixel(-1), 188, 0, 0, 2), "material color tints the texture");
        Expect(PixelIs(frame, ToPixel(1), ToPixel(-1), 0, 255, 0, 1), "channels with factor 1 are unchanged");
    }

    {
        // uv_scale 2 repeats the image twice in each direction: every quarter now shows all four colors.
        poligon::Material material;
        material.texture = quad_texture;
        material.uv_scale = {2.0f, 2.0f};
        renderer.BeginFrame(MakeTopDownCamera(5.0f), flat_light);
        renderer.DrawMesh(plane, glm::dvec3(0.0), four_by_four, material);
        renderer.EndFrame();
        renderer.CaptureFrame(frame);
        // Squares are small here, and a pixel that is not exactly in the middle of one already
        // gets a trace of its neighbor through filtering (a 1% mix shows as about 20 of 255 in
        // dark channels), hence the wider tolerance. The dominant color is what is checked.
        Expect(PixelIs(frame, ToPixel(-1.5f), ToPixel(-1.5f), 255, 0, 0, 40), "repeated: red at the far top-left");
        Expect(PixelIs(frame, ToPixel(-0.5f), ToPixel(-1.5f), 0, 255, 0, 40), "repeated: green right next to it");
        Expect(PixelIs(frame, ToPixel(0.5f), ToPixel(0.5f), 255, 0, 0, 40), "repeated: red again past the middle");
    }

    {
        // A plain color needs no texture.
        renderer.BeginFrame(MakeTopDownCamera(5.0f), flat_light);
        renderer.DrawMesh(plane, glm::dvec3(0.0), four_by_four, poligon::Color{0.0f, 1.0f, 0.0f, 1.0f});
        renderer.EndFrame();
        renderer.CaptureFrame(frame);
        Expect(PixelIs(frame, ToPixel(0), ToPixel(0), 0, 255, 0, 1), "a material without a texture has its plain color");
    }

    {
        // Alpha cutout: the left half of this image is transparent red, the right half solid green.
        // With a cutoff the transparent half is not drawn, so the background shows through.
        poligon::Image cutout_image = poligon::MakeSolidImage(2, 2, 0, 255, 0);
        for (int row = 0; row < 2; ++row) {
            const std::uint8_t transparent_red[4] = {255, 0, 0, 0};
            std::memcpy(&cutout_image.pixels[static_cast<size_t>(row) * 8], transparent_red, 4);
        }
        const poligon::TextureHandle cutout_texture = renderer.CreateTexture(cutout_image, no_mipmaps);

        poligon::Material material;
        material.texture = cutout_texture;
        material.alpha_cutoff = 0.5f;
        renderer.BeginFrame(MakeTopDownCamera(5.0f), flat_light);
        renderer.DrawMesh(plane, glm::dvec3(0.0), four_by_four, material);
        renderer.EndFrame();
        renderer.CaptureFrame(frame);
        Expect(PixelIs(frame, ToPixel(-1), ToPixel(0), 51, 51, 51, 1), "pixels below the alpha cutoff are not drawn");
        Expect(PixelIs(frame, ToPixel(1), ToPixel(0), 0, 255, 0, 1), "pixels above the cutoff are drawn normally");

        material.alpha_cutoff = 0.0f;
        renderer.BeginFrame(MakeTopDownCamera(5.0f), flat_light);
        renderer.DrawMesh(plane, glm::dvec3(0.0), four_by_four, material);
        renderer.EndFrame();
        renderer.CaptureFrame(frame);
        Expect(PixelIs(frame, ToPixel(-1), ToPixel(0), 255, 0, 0, 1), "without a cutoff alpha is ignored");
    }

    {
        // Double-sided: seen from below, the plane (which faces up) is invisible unless the
        // material is double-sided.
        poligon::Camera below = MakeTopDownCamera(5.0f);
        below.position = {0.0f, -5.0f, 0.0f};
        for (int pass = 0; pass < 2; ++pass) {
            poligon::Material material;
            material.color = {0.0f, 0.0f, 1.0f, 1.0f};
            material.double_sided = pass == 1;
            renderer.BeginFrame(below, flat_light);
            renderer.DrawMesh(plane, glm::dvec3(0.0), four_by_four, material);
            renderer.EndFrame();
            renderer.CaptureFrame(frame);
            if (pass == 0) {
                Expect(PixelIs(frame, ToPixel(0), ToPixel(0), 51, 51, 51, 1), "a back face is not drawn by default");
            } else {
                Expect(PixelIs(frame, ToPixel(0), ToPixel(0), 0, 0, 255, 1), "a double-sided material shows its back face");
            }
        }
    }

    {
        // Mipmaps: a fine black and white checker seen from far away must blend to gray.
        // Without mipmaps each pixel picks one square and is black or white.
        const std::uint8_t black[4] = {0, 0, 0, 255};
        const std::uint8_t white[4] = {255, 255, 255, 255};
        const poligon::Image fine_checker = poligon::MakeCheckerImage(256, 256, 4, black, white);
        const poligon::TextureHandle with_mipmaps = renderer.CreateTexture(fine_checker);
        const poligon::TextureHandle without_mipmaps = renderer.CreateTexture(fine_checker, no_mipmaps);

        bool all_gray_with = true;
        bool any_extreme_without = false;
        for (int pass = 0; pass < 2; ++pass) {
            poligon::Material material;
            material.texture = pass == 0 ? with_mipmaps : without_mipmaps;
            renderer.BeginFrame(MakeTopDownCamera(100.0f), flat_light);
            renderer.DrawMesh(plane, glm::dvec3(0.0), four_by_four, material);
            renderer.EndFrame();
            renderer.CaptureFrame(frame);
            // From 100 units the plane is about 9 pixels wide; look at the 3x3 pixels in its middle.
            for (int y = 127; y <= 129; ++y) {
                for (int x = 127; x <= 129; ++x) {
                    const int value = RedAt(frame, x, y);
                    if (pass == 0) {
                        all_gray_with = all_gray_with && value > 120 && value < 230;
                    } else {
                        any_extreme_without = any_extreme_without || value < 60 || value > 245;
                    }
                }
            }
        }
        Expect(all_gray_with, "with mipmaps a distant fine checker is gray");
        Expect(any_extreme_without, "without mipmaps it is not (this shows the check above means something)");
    }

    std::printf("texture_render_test finished on %s\n", renderer.GetBackendName());
    return test::ExitCode();
}

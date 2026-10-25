// Renders 2D over the picture without a window and checks pixels: rectangles land exactly on
// their pixels (also after Resize), colors are as given on screen, text from the built-in
// font covers what MeasureText says, and nothing outside the 2D changes the scene below.
// Usage: overlay_render_test d3d12|vulkan
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

#include "poligon/render/renderer.h"
#include "test_support.h"

namespace {

const std::uint8_t* PixelAt(const poligon::Image& image, int x, int y) {
    return &image.pixels[static_cast<size_t>(y * image.width + x) * 4];
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
        std::printf("usage: overlay_render_test d3d12|vulkan\n");
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
    poligon::Image frame;
    poligon::DirectionalLight light;
    light.color = {0.6f, 0.6f, 0.6f, 1.0f};
    light.ambient = {0.3f, 0.3f, 0.3f, 1.0f};

    // --- A rectangle covers exactly its pixels, at the size set by Resize ---
    {
        Expect(renderer.Resize(320, 200), "the picture can be resized");
        renderer.BeginFrame(poligon::Camera{}, light);
        renderer.DrawRect({10.0f, 20.0f, 30.0f, 15.0f}, {1.0f, 0.0f, 0.0f, 1.0f});
        renderer.DrawRect({100.0f, 150.0f, 8.0f, 8.0f}, {0.5f, 0.5f, 0.5f, 1.0f});
        renderer.DrawRect({200.0f, 150.0f, 8.0f, 8.0f}, {1.0f, 1.0f, 1.0f, 0.5f});
        renderer.EndFrame();
        renderer.CaptureFrame(frame);
        Expect(frame.width == 320 && frame.height == 200, "the frame has the resized size");
        bool inside = true;
        for (int y = 20; y < 35; ++y) {
            for (int x = 10; x < 40; ++x) {
                inside = inside && PixelAt(frame, x, y)[0] == 255 && PixelAt(frame, x, y)[1] == 0;
            }
        }
        bool outside = true;
        for (const glm::ivec2 pixel : {glm::ivec2(9, 25), glm::ivec2(40, 25), glm::ivec2(20, 19), glm::ivec2(20, 35)}) {
            outside = outside && PixelAt(frame, pixel.x, pixel.y)[0] == 0;
        }
        Expect(inside, "a rectangle colors every pixel inside it");
        Expect(outside, "and none outside it");
        // 0.5 on screen is stored as 128 (colors are given as they look, sRGB); half-transparent
        // white over black is half the light, stored as 188.
        const int gray = PixelAt(frame, 104, 154)[0];
        const int blended = PixelAt(frame, 204, 154)[0];
        if (std::abs(gray - 128) > 1 || std::abs(blended - 188) > 1) {
            std::printf("  gray rectangle %d (expected 128), half-transparent white %d (expected 188)\n", gray, blended);
        }
        Expect(std::abs(gray - 128) <= 1, "2D colors are as given on screen");
        Expect(std::abs(blended - 188) <= 1, "2D alpha blends in linear light");
        Expect(renderer.Resize(256, 256), "and back");
    }

    // --- Built-in font: the text covers the width MeasureText gives ---
    {
        const poligon::FontHandle font = renderer.GetBuiltinFont();
        const char* text = "Hello 42";
        const float size = 24.0f;
        const glm::vec2 measured = renderer.MeasureText(font, text, size);
        renderer.BeginFrame(poligon::Camera{}, light);
        renderer.DrawText(font, text, {20.0f, 50.0f}, size, {1.0f, 1.0f, 1.0f, 1.0f});
        renderer.EndFrame();
        renderer.CaptureFrame(frame);
        int lit = 0;
        int left = frame.width;
        int right = -1;
        int top = frame.height;
        int bottom = -1;
        for (int y = 0; y < frame.height; ++y) {
            for (int x = 0; x < frame.width; ++x) {
                if (PixelAt(frame, x, y)[0] > 128) {
                    ++lit;
                    left = std::min(left, x);
                    right = std::max(right, x);
                    top = std::min(top, y);
                    bottom = std::max(bottom, y);
                }
            }
        }
        std::printf("  \"%s\" at %.0f px: measured %.0f x %.0f, drawn from x %d to %d, y %d to %d, %d pixels\n", text, size, measured.x,
                    measured.y, left, right, top, bottom, lit);
        Expect(lit > 100, "text in the built-in font draws pixels");
        // The last advance includes a pixel (times 2 at this size) of space after the "2".
        Expect(left >= 20 && right < 20 + static_cast<int>(measured.x) && right >= 20 + static_cast<int>(measured.x) - 6,
               "the text is as wide as MeasureText says");
        Expect(top >= 50 && bottom < 50 + static_cast<int>(measured.y), "and no higher than a line");
        const glm::vec2 two_lines = renderer.MeasureText(font, "ab\nlonger line", size);
        Expect(two_lines.y == 2.0f * measured.y && two_lines.x > measured.x, "a line break starts a second line");
    }

    // --- Text does not change the scene under it outside its own box ---
    {
        const poligon::MeshHandle cube = renderer.CreateCubeMesh();
        const poligon::MeshHandle plane = renderer.CreatePlaneMesh();
        poligon::Camera camera;
        camera.position = {3.0f, 2.5f, 4.0f};
        camera.target = {0.0f, 0.5f, 0.0f};
        poligon::Image plain;
        poligon::Image with_text;
        const glm::vec2 position(30.0f, 100.0f);
        const glm::vec2 box = renderer.MeasureText(renderer.GetBuiltinFont(), "HUD 123", 12.0f);
        for (int pass = 0; pass < 2; ++pass) {
            renderer.BeginFrame(camera, light);
            renderer.DrawMesh(plane, glm::scale(glm::mat4(1.0f), {10.0f, 1.0f, 10.0f}), poligon::Color{0.4f, 0.4f, 0.4f, 1.0f});
            renderer.DrawMesh(cube, glm::translate(glm::mat4(1.0f), {0.0f, 0.5f, 0.0f}), poligon::Color{0.9f, 0.5f, 0.1f, 1.0f});
            if (pass == 1) {
                renderer.DrawText(renderer.GetBuiltinFont(), "HUD 123", position, 12.0f, {1.0f, 1.0f, 0.0f, 1.0f});
            }
            renderer.EndFrame();
            renderer.CaptureFrame(pass == 0 ? plain : with_text);
        }
        int changed_inside = 0;
        int changed_outside = 0;
        for (int y = 0; y < plain.height; ++y) {
            for (int x = 0; x < plain.width; ++x) {
                const bool differs = std::memcmp(PixelAt(plain, x, y), PixelAt(with_text, x, y), 3) != 0;
                const bool in_box = x >= position.x && x < position.x + box.x && y >= position.y && y < position.y + box.y;
                (in_box ? changed_inside : changed_outside) += differs ? 1 : 0;
            }
        }
        std::printf("  text over the scene: %d pixels changed inside its box, %d outside\n", changed_inside, changed_outside);
        Expect(changed_inside > 20 && changed_outside == 0, "text changes only its own pixels of the scene");
    }

    // --- A TrueType font, when the system has one to read (none is shipped or downloaded):
    // Polish letters are in its atlas, so they are drawn as letters, not as boxes. ---
    {
        std::ifstream file("C:/Windows/Fonts/arial.ttf", std::ios::binary);
        if (!file) {
            std::printf("  no C:/Windows/Fonts/arial.ttf; the TrueType part is skipped\n");
        } else {
            const std::vector<std::uint8_t> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            const poligon::FontHandle font = renderer.CreateTrueTypeFont(data.data(), data.size(), 32.0f);
            Expect(font.id != 0, "a TrueType font is made from its file's contents");
            const std::uint8_t not_a_font[128] = {1, 2, 3};
            Expect(renderer.CreateTrueTypeFont(not_a_font, sizeof(not_a_font), 32.0f).id == 0, "data that is not a font gives no font");
            if (font.id != 0) {
                // A missing character is a box half a line wide; the letter has its own width.
                const float letter = renderer.MeasureText(font, "\xC5\x81", 32.0f).x;   // "Ł"
                const float missing = renderer.MeasureText(font, "\xE4\xB8\xAD", 32.0f).x;  // a Chinese character
                std::printf("  TrueType: width of a Polish letter %.1f, of a missing character %.1f\n", letter, missing);
                Expect(letter > 0.0f && letter != missing, "Polish letters come from the font, not as boxes");
                renderer.BeginFrame(poligon::Camera{}, light);
                renderer.DrawText(font, "Zażółć gęślą jaźń", {10.0f, 10.0f}, 32.0f, {1.0f, 1.0f, 1.0f, 1.0f});
                renderer.EndFrame();
                renderer.CaptureFrame(frame);
                int lit = 0;
                for (size_t i = 0; i < frame.pixels.size(); i += 4) {
                    lit += frame.pixels[i] > 128 ? 1 : 0;
                }
                Expect(lit > 200, "TrueType text draws pixels");
                renderer.DestroyFont(font);
            }
        }
    }

    std::printf("overlay_render_test finished\n");
    return test::ExitCode();
}

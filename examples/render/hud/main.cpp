// Example: a hunting game's HUD over the scene: a crosshair from rectangles, a line of text in
// the built-in font (no font file needed), a semi-transparent panel and a sprite. All in
// pixels of the output image, drawn after tone mapping.
// Usage: example_hud OUTPUT.png
#include <cstdio>

#include "poligon/render/renderer.h"

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: example_hud OUTPUT.png\n");
        return 2;
    }
    poligon::RendererConfig config;
    config.width = 640;
    config.height = 360;
    config.headless = true;
    config.clear_color = {0.55f, 0.65f, 0.8f, 1.0f};
    poligon::Renderer renderer(config);
    const poligon::MeshHandle cube = renderer.CreateCubeMesh();
    const poligon::MeshHandle plane = renderer.CreatePlaneMesh();

    // A small sprite made in code: a compass needle, red half and white half.
    poligon::Image needle = poligon::MakeSolidImage(8, 32, 255, 255, 255);
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 8; ++x) {
            std::uint8_t* pixel = &needle.pixels[static_cast<size_t>(y * 8 + x) * 4];
            pixel[1] = 40;
            pixel[2] = 40;
        }
    }
    const poligon::TextureHandle needle_texture = renderer.CreateTexture(needle);

    poligon::Camera camera;
    camera.position = {3.0f, 2.5f, 4.0f};
    camera.target = {0.0f, 0.5f, 0.0f};
    renderer.BeginFrame(camera, poligon::DirectionalLight{});
    const glm::mat4 identity(1.0f);
    renderer.DrawMesh(plane, glm::dvec3(0.0), glm::scale(identity, {10.0f, 1.0f, 10.0f}), poligon::Color{0.4f, 0.45f, 0.3f, 1.0f});
    renderer.DrawMesh(cube, glm::dvec3(0.0), glm::translate(identity, {0.0f, 0.5f, 0.0f}), poligon::Color{0.6f, 0.4f, 0.25f, 1.0f});

    // Crosshair: four short bars around the middle, leaving the aim point free.
    const float cx = static_cast<float>(renderer.GetWidth()) * 0.5f;
    const float cy = static_cast<float>(renderer.GetHeight()) * 0.5f;
    const poligon::Color white{1.0f, 1.0f, 1.0f, 0.9f};
    renderer.DrawRect({cx - 12.0f, cy - 1.0f, 8.0f, 2.0f}, white);
    renderer.DrawRect({cx + 4.0f, cy - 1.0f, 8.0f, 2.0f}, white);
    renderer.DrawRect({cx - 1.0f, cy - 12.0f, 2.0f, 8.0f}, white);
    renderer.DrawRect({cx - 1.0f, cy + 4.0f, 2.0f, 8.0f}, white);

    // A panel with two lines of text, sized by MeasureText.
    const poligon::FontHandle font = renderer.GetBuiltinFont();
    const char* text = "Wind: 3 m/s NE\nDistance: 142 m";
    const glm::vec2 size = renderer.MeasureText(font, text, 24.0f);
    renderer.DrawRect({16.0f, 16.0f, size.x + 16.0f, size.y + 12.0f}, {0.0f, 0.0f, 0.0f, 0.45f});
    renderer.DrawText(font, text, {24.0f, 22.0f}, 24.0f, {1.0f, 0.95f, 0.8f, 1.0f});

    // The compass needle as a sprite: the whole texture, stretched to 12 x 48 pixels.
    renderer.DrawSprite(needle_texture, {static_cast<float>(renderer.GetWidth()) - 40.0f, 20.0f, 12.0f, 48.0f}, {0.0f, 0.0f, 1.0f, 1.0f});
    renderer.EndFrame();

    if (!renderer.SaveFrameToPng(argv[1])) {
        std::fprintf(stderr, "cannot write %s\n", argv[1]);
        return 1;
    }
    std::printf("saved %s\n", argv[1]);
    return 0;
}

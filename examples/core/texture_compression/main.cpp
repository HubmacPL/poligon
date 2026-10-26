// Example: compress an image to BC1 with its mipmaps, save it as a .ptex file and load it back.
// The asset cooker does this for every texture.
#include <cstdint>
#include <cstdio>
#include <string>

#include "poligon/core/texture_compression.h"

int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1] : "example_texture.ptex";

    const std::uint8_t green[4] = {40, 120, 30, 255};
    const std::uint8_t brown[4] = {110, 80, 40, 255};
    const poligon::Image image = poligon::MakeCheckerImage(256, 256, 32, green, brown);

    poligon::TextureCompressionOptions options;
    options.format = poligon::TextureFormat::Bc1;
    const poligon::CompressedImage compressed = poligon::CompressImage(image, options);

    size_t bytes = 0;
    for (const auto& level : compressed.levels) {
        bytes += level.size();
    }
    std::printf("%s, %d x %d, %zu levels, %zu bytes (plain RGBA, level 0 only: %zu)\n",
                poligon::GetTextureFormatName(compressed.format), compressed.width, compressed.height,
                compressed.levels.size(), bytes, image.pixels.size());

    poligon::CompressedImage loaded;
    std::string error;
    if (!poligon::SaveCompressedImage(path, compressed) || !poligon::LoadCompressedImage(path, loaded, error)) {
        std::printf("save or load failed: %s\n", error.c_str());
        return 1;
    }
    const bool same = loaded.levels == compressed.levels;
    std::printf("loaded back from %s: %s\n", path, same ? "same bytes" : "different bytes");
    return same ? 0 : 1;
}

#include "poligon/image.h"

#include <cmath>
#include <cstring>

#include <stb_image.h>
#include <stb_image_write.h>

#include "poligon/fatal_error.h"

namespace poligon {

Image MakeSolidImage(int width, int height, std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) {
    const std::uint8_t color[4] = {r, g, b, a};
    return MakeCheckerImage(width, height, 1, color, color);
}

Image MakeCheckerImage(int width, int height, int cell_size, const std::uint8_t first_rgba[4],
                       const std::uint8_t second_rgba[4]) {
    if (width < 1 || height < 1 || cell_size < 1) {
        FatalError("MakeCheckerImage: width, height and cell_size must be at least 1");
    }
    Image image;
    image.width = width;
    image.height = height;
    image.pixels.resize(static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const bool first = ((x / cell_size) + (y / cell_size)) % 2 == 0;
            std::memcpy(&image.pixels[(static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)) * 4],
                        first ? first_rgba : second_rgba, 4);
        }
    }
    return image;
}

namespace {

// sRGB byte -> linear light, and back. Tables keep the result identical everywhere and fast.
struct SrgbTables {
    float to_linear[256];

    SrgbTables() {
        for (int i = 0; i < 256; ++i) {
            const float encoded = static_cast<float>(i) / 255.0f;
            to_linear[i] = encoded <= 0.04045f ? encoded / 12.92f : std::pow((encoded + 0.055f) / 1.055f, 2.4f);
        }
    }
};

std::uint8_t LinearToSrgbByte(float linear) {
    const float encoded = linear <= 0.0031308f ? linear * 12.92f : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
    const float scaled = encoded * 255.0f + 0.5f;
    return static_cast<std::uint8_t>(scaled < 0.0f ? 0.0f : (scaled > 255.0f ? 255.0f : scaled));
}

} // namespace

Image MakeHalfSizeImage(const Image& image, bool is_color) {
    static const SrgbTables tables;
    if (image.width < 1 || image.height < 1 ||
        image.pixels.size() != static_cast<size_t>(image.width) * static_cast<size_t>(image.height) * 4) {
        FatalError("MakeHalfSizeImage: the image is empty or its pixel data has the wrong size");
    }

    Image half;
    half.width = image.width > 1 ? image.width / 2 : 1;
    half.height = image.height > 1 ? image.height / 2 : 1;
    half.pixels.resize(static_cast<size_t>(half.width) * static_cast<size_t>(half.height) * 4);

    for (int y = 0; y < half.height; ++y) {
        for (int x = 0; x < half.width; ++x) {
            // The four source pixels; at an edge of a 1-pixel-wide or -high image they repeat.
            const int x0 = x * 2;
            const int y0 = y * 2;
            const int x1 = x0 + 1 < image.width ? x0 + 1 : x0;
            const int y1 = y0 + 1 < image.height ? y0 + 1 : y0;
            const std::uint8_t* sources[4] = {
                &image.pixels[(static_cast<size_t>(y0) * static_cast<size_t>(image.width) + static_cast<size_t>(x0)) * 4],
                &image.pixels[(static_cast<size_t>(y0) * static_cast<size_t>(image.width) + static_cast<size_t>(x1)) * 4],
                &image.pixels[(static_cast<size_t>(y1) * static_cast<size_t>(image.width) + static_cast<size_t>(x0)) * 4],
                &image.pixels[(static_cast<size_t>(y1) * static_cast<size_t>(image.width) + static_cast<size_t>(x1)) * 4],
            };
            std::uint8_t* target =
                &half.pixels[(static_cast<size_t>(y) * static_cast<size_t>(half.width) + static_cast<size_t>(x)) * 4];
            for (int channel = 0; channel < 4; ++channel) {
                if (is_color && channel < 3) {
                    float sum = 0.0f;
                    for (const std::uint8_t* source : sources) {
                        sum += tables.to_linear[source[channel]];
                    }
                    target[channel] = LinearToSrgbByte(sum * 0.25f);
                } else {
                    int sum = 0;
                    for (const std::uint8_t* source : sources) {
                        sum += source[channel];
                    }
                    target[channel] = static_cast<std::uint8_t>((sum + 2) / 4);
                }
            }
        }
    }
    return half;
}

bool LoadImageFromFile(const char* path, Image& image, std::string& error) {
    int width = 0;
    int height = 0;
    int channels_in_file = 0;
    stbi_uc* decoded = stbi_load(path, &width, &height, &channels_in_file, 4);
    if (decoded == nullptr) {
        error = std::string("cannot load image ") + path + ": " + stbi_failure_reason();
        return false;
    }
    image.width = width;
    image.height = height;
    image.pixels.assign(decoded, decoded + static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
    stbi_image_free(decoded);
    error.clear();
    return true;
}

bool SaveImageToPng(const char* path, const Image& image) {
    if (image.width < 1 || image.height < 1 ||
        image.pixels.size() != static_cast<size_t>(image.width) * static_cast<size_t>(image.height) * 4) {
        return false;
    }
    return stbi_write_png(path, image.width, image.height, 4, image.pixels.data(), image.width * 4) != 0;
}

} // namespace poligon

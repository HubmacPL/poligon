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

bool LoadImageFromMemory(const void* data, size_t size, Image& image, std::string& error) {
    int width = 0;
    int height = 0;
    int channels_in_file = 0;
    stbi_uc* decoded = size > 0 && size <= 0x7fffffff
                           ? stbi_load_from_memory(static_cast<const stbi_uc*>(data), static_cast<int>(size), &width,
                                                   &height, &channels_in_file, 4)
                           : nullptr;
    if (decoded == nullptr) {
        error = std::string("cannot decode image from memory: ") + (size > 0 ? stbi_failure_reason() : "no data");
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

// Spreads the colors of visible pixels into transparent ones, one pixel further per pass.
float MeasureAlphaCoverage(const Image& image, float cutoff) {
    const size_t pixel_count = static_cast<size_t>(image.width) * static_cast<size_t>(image.height);
    if (pixel_count == 0) {
        return 0.0f;
    }
    const int threshold = static_cast<int>(std::ceil(cutoff * 255.0f));
    size_t covered = 0;
    for (size_t i = 0; i < pixel_count; ++i) {
        covered += image.pixels[i * 4 + 3] >= threshold ? 1 : 0;
    }
    return static_cast<float>(covered) / static_cast<float>(pixel_count);
}

void ScaleAlphaToCoverage(Image& image, float cutoff, float coverage) {
    // Coverage at a given alpha scale, without touching the image.
    const size_t pixel_count = static_cast<size_t>(image.width) * static_cast<size_t>(image.height);
    const auto coverage_at = [&image, pixel_count, cutoff](float scale) {
        size_t covered = 0;
        for (size_t i = 0; i < pixel_count; ++i) {
            covered += static_cast<float>(image.pixels[i * 4 + 3]) * scale >= cutoff * 255.0f ? 1 : 0;
        }
        return static_cast<float>(covered) / static_cast<float>(pixel_count);
    };
    // Coverage grows with the scale; find the scale by bisection.
    float low = 1.0f;
    float high = 8.0f;
    if (coverage_at(low) >= coverage) {
        return;
    }
    for (int step = 0; step < 12; ++step) {
        const float middle = (low + high) * 0.5f;
        if (coverage_at(middle) < coverage) {
            low = middle;
        } else {
            high = middle;
        }
    }
    const float scale = high;
    for (size_t i = 0; i < pixel_count; ++i) {
        const float alpha = static_cast<float>(image.pixels[i * 4 + 3]) * scale;
        image.pixels[i * 4 + 3] = static_cast<std::uint8_t>(alpha >= 255.0f ? 255 : static_cast<int>(alpha + 0.5f));
    }
}

void BleedColorsIntoTransparentPixels(Image& image, int passes) {
    const int width = image.width;
    const int height = image.height;
    std::vector<unsigned char> filled(static_cast<size_t>(width) * static_cast<size_t>(height));
    for (size_t i = 0; i < filled.size(); ++i) {
        filled[i] = image.pixels[i * 4 + 3] > 0 ? 1 : 0;
    }

    for (int pass = 0; pass < passes; ++pass) {
        std::vector<unsigned char> filled_next = filled;
        bool changed = false;
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const size_t index = static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x);
                if (filled[index]) {
                    continue;
                }
                int sum[3] = {0, 0, 0};
                int count = 0;
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int nx = x + dx;
                        const int ny = y + dy;
                        if (nx < 0 || ny < 0 || nx >= width || ny >= height) {
                            continue;
                        }
                        const size_t neighbor = static_cast<size_t>(ny) * static_cast<size_t>(width) + static_cast<size_t>(nx);
                        if (filled[neighbor]) {
                            sum[0] += image.pixels[neighbor * 4 + 0];
                            sum[1] += image.pixels[neighbor * 4 + 1];
                            sum[2] += image.pixels[neighbor * 4 + 2];
                            ++count;
                        }
                    }
                }
                if (count > 0) {
                    image.pixels[index * 4 + 0] = static_cast<std::uint8_t>(sum[0] / count);
                    image.pixels[index * 4 + 1] = static_cast<std::uint8_t>(sum[1] / count);
                    image.pixels[index * 4 + 2] = static_cast<std::uint8_t>(sum[2] / count);
                    filled_next[index] = 1;
                    changed = true;
                }
            }
        }
        filled = filled_next;
        if (!changed) {
            break;
        }
    }
}

} // namespace poligon

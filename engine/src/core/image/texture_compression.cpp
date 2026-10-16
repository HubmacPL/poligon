#include "poligon/core/texture_compression.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

#include <stb_dxt.h>

#include "poligon/core/fatal_error.h"

namespace poligon {

namespace {

int BlockBytes(TextureFormat format) {
    switch (format) {
    case TextureFormat::Bc1:
    case TextureFormat::Bc4:
        return 8;
    case TextureFormat::Bc3:
    case TextureFormat::Bc5:
        return 16;
    case TextureFormat::Rgba8:
        return 0;
    }
    return 0;
}

// One level compressed block by block; edge blocks repeat the last row and column.
std::vector<std::uint8_t> CompressLevel(const Image& image, TextureFormat format) {
    if (format == TextureFormat::Rgba8) {
        return image.pixels;
    }
    const int blocks_x = (image.width + 3) / 4;
    const int blocks_y = (image.height + 3) / 4;
    const int block_bytes = BlockBytes(format);
    std::vector<std::uint8_t> out(static_cast<size_t>(blocks_x) * static_cast<size_t>(blocks_y) * static_cast<size_t>(block_bytes));
    unsigned char rgba[64];
    unsigned char channels[32];
    for (int by = 0; by < blocks_y; ++by) {
        for (int bx = 0; bx < blocks_x; ++bx) {
            for (int y = 0; y < 4; ++y) {
                const int sy = std::min(by * 4 + y, image.height - 1);
                for (int x = 0; x < 4; ++x) {
                    const int sx = std::min(bx * 4 + x, image.width - 1);
                    const std::uint8_t* pixel = &image.pixels[(static_cast<size_t>(sy) * static_cast<size_t>(image.width) +
                                                               static_cast<size_t>(sx)) * 4];
                    std::memcpy(&rgba[(y * 4 + x) * 4], pixel, 4);
                    channels[(y * 4 + x) * 2] = pixel[0];
                    channels[(y * 4 + x) * 2 + 1] = pixel[1];
                }
            }
            unsigned char* dest = &out[(static_cast<size_t>(by) * static_cast<size_t>(blocks_x) + static_cast<size_t>(bx)) *
                                       static_cast<size_t>(block_bytes)];
            switch (format) {
            case TextureFormat::Bc1:
                stb_compress_dxt_block(dest, rgba, 0, STB_DXT_HIGHQUAL);
                break;
            case TextureFormat::Bc3:
                stb_compress_dxt_block(dest, rgba, 1, STB_DXT_HIGHQUAL);
                break;
            case TextureFormat::Bc4: {
                unsigned char red[16];
                for (int i = 0; i < 16; ++i) {
                    red[i] = rgba[i * 4];
                }
                stb_compress_bc4_block(dest, red);
                break;
            }
            case TextureFormat::Bc5:
                stb_compress_bc5_block(dest, channels);
                break;
            case TextureFormat::Rgba8:
                break;
            }
        }
    }
    return out;
}

bool ParseCompressed(std::istream& stream, CompressedImage& image, std::string& error, const std::string& what) {
    std::string line;
    std::getline(stream, line);
    std::istringstream header(line);
    std::string magic;
    std::string format_name;
    int version = 0;
    int level_count = 0;
    int is_color = 0;
    CompressedImage loaded;
    header >> magic >> version >> format_name >> loaded.width >> loaded.height >> level_count >> is_color;
    if (!header || magic != "poligon-texture" || version != 1 || !ParseTextureFormat(format_name.c_str(), loaded.format) ||
        loaded.width < 1 || loaded.height < 1 || level_count < 1 || level_count > 16) {
        error = what + ": not a poligon texture file";
        return false;
    }
    loaded.is_color = is_color != 0;
    int width = loaded.width;
    int height = loaded.height;
    for (int level = 0; level < level_count; ++level) {
        std::vector<std::uint8_t> bytes(GetCompressedLevelSize(loaded.format, width, height));
        stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!stream) {
            error = what + ": the file is shorter than its header says";
            return false;
        }
        loaded.levels.push_back(std::move(bytes));
        width = std::max(width / 2, 1);
        height = std::max(height / 2, 1);
    }
    image = std::move(loaded);
    return true;
}

} // namespace

size_t GetCompressedLevelSize(TextureFormat format, int width, int height) {
    if (format == TextureFormat::Rgba8) {
        return static_cast<size_t>(width) * static_cast<size_t>(height) * 4;
    }
    return static_cast<size_t>((width + 3) / 4) * static_cast<size_t>((height + 3) / 4) * static_cast<size_t>(BlockBytes(format));
}

const char* GetTextureFormatName(TextureFormat format) {
    switch (format) {
    case TextureFormat::Rgba8:
        return "rgba8";
    case TextureFormat::Bc1:
        return "bc1";
    case TextureFormat::Bc3:
        return "bc3";
    case TextureFormat::Bc4:
        return "bc4";
    case TextureFormat::Bc5:
        return "bc5";
    }
    return "rgba8";
}

bool ParseTextureFormat(const char* name, TextureFormat& format) {
    for (const TextureFormat candidate :
         {TextureFormat::Rgba8, TextureFormat::Bc1, TextureFormat::Bc3, TextureFormat::Bc4, TextureFormat::Bc5}) {
        if (std::strcmp(name, GetTextureFormatName(candidate)) == 0) {
            format = candidate;
            return true;
        }
    }
    return false;
}

CompressedImage CompressImage(const Image& source, const TextureCompressionOptions& options) {
    if (source.width < 1 || source.height < 1 ||
        source.pixels.size() != static_cast<size_t>(source.width) * static_cast<size_t>(source.height) * 4) {
        FatalError("CompressImage: the image is empty or its pixel data has the wrong size");
    }
    Image image = source;
    while (options.max_size > 0 && (image.width > options.max_size || image.height > options.max_size)) {
        image = MakeHalfSizeImage(image, options.is_color);
    }
    CompressedImage result;
    result.format = options.format;
    result.is_color = options.is_color;
    result.width = image.width;
    result.height = image.height;
    // The same chain of halvings as Renderer::CreateTexture: from unscaled images, with a
    // coverage-scaled copy for each level of a cut-out texture.
    const float coverage = options.coverage_cutoff > 0.0f ? MeasureAlphaCoverage(image, options.coverage_cutoff) : 0.0f;
    Image level_image = image;
    while (true) {
        if (options.coverage_cutoff > 0.0f && result.levels.size() > 0) {
            Image scaled = level_image;
            ScaleAlphaToCoverage(scaled, options.coverage_cutoff, coverage);
            result.levels.push_back(CompressLevel(scaled, options.format));
        } else {
            result.levels.push_back(CompressLevel(level_image, options.format));
        }
        if (!options.generate_mipmaps || (level_image.width == 1 && level_image.height == 1)) {
            break;
        }
        // Block-compressed chains end at 4 x 4: Direct3D 12 cannot copy smaller levels.
        if (options.format != TextureFormat::Rgba8 && (level_image.width / 2 < 4 || level_image.height / 2 < 4)) {
            break;
        }
        level_image = MakeHalfSizeImage(level_image, options.is_color);
    }
    return result;
}

bool SaveCompressedImage(const char* path, const CompressedImage& image) {
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    char header[160];
    std::snprintf(header, sizeof(header), "poligon-texture 1 %s %d %d %d %d\n", GetTextureFormatName(image.format), image.width,
                  image.height, static_cast<int>(image.levels.size()), image.is_color ? 1 : 0);
    file << header;
    for (const std::vector<std::uint8_t>& level : image.levels) {
        file.write(reinterpret_cast<const char*>(level.data()), static_cast<std::streamsize>(level.size()));
    }
    return static_cast<bool>(file);
}

bool LoadCompressedImage(const char* path, CompressedImage& image, std::string& error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = std::string("cannot open ") + path;
        return false;
    }
    return ParseCompressed(file, image, error, path);
}

bool LoadCompressedImageFromMemory(const void* data, size_t size, CompressedImage& image, std::string& error) {
    std::istringstream stream(std::string(static_cast<const char*>(data), size));
    return ParseCompressed(stream, image, error, "texture in memory");
}

} // namespace poligon

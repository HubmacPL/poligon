#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "poligon/core/image.h"

namespace poligon {

// Block-compressed textures: the graphics card reads them directly, at a quarter or an
// eighth of the memory of plain RGBA. Each 4 x 4 block of pixels is stored in 8 or 16 bytes.
// Compression uses stb_dxt (public domain / MIT), done once by the asset cooker.
//
//   Bc1  color without transparency (or with on/off transparency): 8 bytes per block
//   Bc3  color with transparency (cut-out leaves): 16 bytes per block
//   Bc4  one channel (a mask, a height): 8 bytes per block
//   Bc5  two channels: normal maps keep x and y, the shader rebuilds z; 16 bytes per block
//   Rgba8  no compression, for small or exact images
enum class TextureFormat { Rgba8, Bc1, Bc3, Bc4, Bc5 };

// An image ready for the graphics card: every mipmap level, compressed.
struct CompressedImage {
    TextureFormat format = TextureFormat::Rgba8;
    // true when the pixels are sRGB-encoded colors (see TextureOptions::is_color).
    bool is_color = true;
    int width = 0;
    int height = 0;
    // Level 0 is the full size, each next one half as large, down to 1 x 1 (block-compressed
    // images down to 4 x 4: Direct3D 12 cannot copy smaller levels of them).
    std::vector<std::vector<std::uint8_t>> levels;
};

struct TextureCompressionOptions {
    TextureFormat format = TextureFormat::Bc1;
    bool is_color = true;
    bool generate_mipmaps = true;
    // As TextureOptions::coverage_cutoff: keeps cut-out leaves from thinning out in mipmaps.
    float coverage_cutoff = 0.0f;
    // Larger images are halved until they fit (0 = no limit).
    int max_size = 0;
};

// Compresses an image and its mipmaps (computed as Renderer::CreateTexture does, so the
// look matches). The same image and options give the same bytes.
CompressedImage CompressImage(const Image& image, const TextureCompressionOptions& options);

// Bytes of one level of `width` x `height` pixels in `format`.
size_t GetCompressedLevelSize(TextureFormat format, int width, int height);

const char* GetTextureFormatName(TextureFormat format);  // "rgba8", "bc1", ...
bool ParseTextureFormat(const char* name, TextureFormat& format);

// Poligon's own texture file (.ptex): a line of text ("poligon-texture 1 FORMAT WIDTH
// HEIGHT LEVELS COLOR") followed by the levels' bytes.
bool SaveCompressedImage(const char* path, const CompressedImage& image);
bool LoadCompressedImage(const char* path, CompressedImage& image, std::string& error);
bool LoadCompressedImageFromMemory(const void* data, size_t size, CompressedImage& image, std::string& error);

} // namespace poligon

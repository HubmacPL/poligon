#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace poligon {

// An image in memory: 8 bits per channel, four channels (red, green, blue, alpha),
// rows from top to bottom, pixels from left to right, no padding.
// The pixel at column x, row y starts at pixels[(y * width + x) * 4].
struct Image {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> pixels;
};

// Creates an image filled with one color.
Image MakeSolidImage(int width, int height, std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a = 255);

// Creates a checkerboard of two colors; each square is cell_size pixels wide. The top-left square has the first color.
Image MakeCheckerImage(int width, int height, int cell_size, const std::uint8_t first_rgba[4],
                       const std::uint8_t second_rgba[4]);

// Returns the image at half the width and half the height (at least 1), each pixel being the
// average of the 2x2 pixels it covers. With is_color the red, green and blue bytes are treated
// as sRGB-encoded and averaged as light, which is what looks right; alpha is always averaged
// as a plain number. Used to build mipmaps.
Image MakeHalfSizeImage(const Image& image, bool is_color);

// Reads a PNG or JPEG file. Images without alpha get alpha 255; grayscale becomes gray RGB.
// Returns false and fills `error` if the file is missing or not a readable image.
bool LoadImageFromFile(const char* path, Image& image, std::string& error);

// Same, for a PNG or JPEG file that is already in memory (for example a texture stored inside a model file).
bool LoadImageFromMemory(const void* data, size_t size, Image& image, std::string& error);

// Writes an image as PNG. Returns false if the image is empty or the file cannot be written.
bool SaveImageToPng(const char* path, const Image& image);

} // namespace poligon

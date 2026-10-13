// Usage: image_test OUTPUT_DIRECTORY
#include <cstdio>
#include <cstdlib>
#include <string>

#include <stb_image_write.h>  // only to produce a JPEG file for the loading test

#include "poligon/image.h"
#include "test_support.h"

namespace {

const std::uint8_t* PixelAt(const poligon::Image& image, int x, int y) {
    return &image.pixels[static_cast<size_t>(y * image.width + x) * 4];
}

bool PixelIs(const poligon::Image& image, int x, int y, int r, int g, int b, int a, int tolerance = 0) {
    const std::uint8_t* pixel = PixelAt(image, x, y);
    return std::abs(pixel[0] - r) <= tolerance && std::abs(pixel[1] - g) <= tolerance &&
           std::abs(pixel[2] - b) <= tolerance && std::abs(pixel[3] - a) <= tolerance;
}

} // namespace

int main(int argc, char** argv) {
    using test::Expect;
    if (argc != 2) {
        std::printf("usage: image_test OUTPUT_DIRECTORY\n");
        return 2;
    }
    const std::string directory = argv[1];
    std::string error;

    const std::uint8_t red[4] = {255, 0, 0, 255};
    const std::uint8_t half_blue[4] = {0, 0, 255, 128};
    const poligon::Image checker = poligon::MakeCheckerImage(8, 6, 2, red, half_blue);
    Expect(checker.width == 8 && checker.height == 6 && checker.pixels.size() == 8 * 6 * 4, "checker has the asked size");
    Expect(PixelIs(checker, 0, 0, 255, 0, 0, 255), "the top-left square has the first color");
    Expect(PixelIs(checker, 2, 0, 0, 0, 255, 128), "the next square has the second color");
    Expect(PixelIs(checker, 2, 2, 255, 0, 0, 255), "squares alternate in both directions");

    const poligon::Image solid = poligon::MakeSolidImage(3, 2, 10, 20, 30);
    Expect(PixelIs(solid, 2, 1, 10, 20, 30, 255), "solid image has one color and full alpha by default");

    // Halving: 2x2 pixels become one. A black and white pair averages to 188 as a color
    // (half the light), but to 128 as plain numbers.
    {
        const std::uint8_t black[4] = {0, 0, 0, 0};
        const std::uint8_t white[4] = {255, 255, 255, 255};
        const poligon::Image fine = poligon::MakeCheckerImage(4, 2, 1, black, white);
        const poligon::Image as_color = poligon::MakeHalfSizeImage(fine, true);
        Expect(as_color.width == 2 && as_color.height == 1, "halving halves both sides");
        Expect(PixelIs(as_color, 0, 0, 188, 188, 188, 128), "colors are averaged as light, alpha as a number");
        const poligon::Image as_data = poligon::MakeHalfSizeImage(fine, false);
        Expect(PixelIs(as_data, 1, 0, 128, 128, 128, 128), "non-color data is averaged as plain numbers");

        const poligon::Image tall = poligon::MakeSolidImage(1, 5, 9, 9, 9);
        const poligon::Image tall_half = poligon::MakeHalfSizeImage(tall, true);
        Expect(tall_half.width == 1 && tall_half.height == 2, "a side never shrinks below one pixel");
        Expect(PixelIs(tall_half, 0, 1, 9, 9, 9, 255), "averaging equal pixels changes nothing");
    }

    // PNG is lossless: what is saved comes back exactly, including alpha.
    {
        const std::string path = directory + "/image_test.png";
        Expect(poligon::SaveImageToPng(path.c_str(), checker), "PNG is saved");
        poligon::Image loaded;
        Expect(poligon::LoadImageFromFile(path.c_str(), loaded, error), "PNG is loaded");
        Expect(loaded.width == checker.width && loaded.height == checker.height && loaded.pixels == checker.pixels,
               "a loaded PNG is identical to the saved image");
    }

    // JPEG is lossy and has no alpha: colors come back approximately, alpha is 255.
    {
        const std::string path = directory + "/image_test.jpg";
        const poligon::Image blocks = poligon::MakeCheckerImage(32, 32, 16, red, half_blue);
        Expect(stbi_write_jpg(path.c_str(), blocks.width, blocks.height, 4, blocks.pixels.data(), 95) != 0,
               "a JPEG file is produced for the test");
        poligon::Image loaded;
        Expect(poligon::LoadImageFromFile(path.c_str(), loaded, error), "JPEG is loaded");
        Expect(loaded.width == 32 && loaded.height == 32, "JPEG has the right size");
        Expect(PixelIs(loaded, 4, 4, 255, 0, 0, 255, 12), "the top-left block of the JPEG is red (rows are top to bottom)");
        Expect(PixelIs(loaded, 28, 4, 0, 0, 255, 255, 12), "the top-right block is blue with full alpha");
    }

    {
        poligon::Image loaded;
        Expect(!poligon::LoadImageFromFile((directory + "/no_such_image.png").c_str(), loaded, error),
               "a missing file is an error");
        Expect(error.find("no_such_image.png") != std::string::npos, "the error names the file");
    }
    {
        const std::string path = directory + "/image_test_not_an_image.png";
        std::FILE* file = nullptr;
        if (fopen_s(&file, path.c_str(), "wb") == 0 && file != nullptr) {
            std::fputs("this is text, not an image", file);
            std::fclose(file);
        }
        poligon::Image loaded;
        Expect(!poligon::LoadImageFromFile(path.c_str(), loaded, error), "a file that is not an image is an error");
    }
    {
        const poligon::Image empty;
        Expect(!poligon::SaveImageToPng((directory + "/image_test_empty.png").c_str(), empty),
               "an empty image cannot be saved");
    }

    return test::ExitCode();
}

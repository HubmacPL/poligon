// Small image operations needed when preparing downloaded assets.
//
// Usage: image_tool merge-alpha COLOR_IMAGE ALPHA_IMAGE OUTPUT.png
//
// merge-alpha: writes a PNG with the colors of COLOR_IMAGE and, as transparency, the
// brightness of ALPHA_IMAGE (white = visible, black = cut away). Some asset libraries ship
// a leaf texture as a JPEG, which cannot hold transparency, plus a separate mask image.
// Colors of the cut-away pixels are replaced by those of their nearest visible neighbors,
// so that no dark fringe appears where the texture is filtered across the cut edge.
//
// Usage: image_tool pack-orm OCCLUSION_IMAGE ROUGHNESS_IMAGE OUTPUT.png
//
// pack-orm: writes the combined data texture glTF and Poligon's Material::orm_texture
// expect: red = ambient occlusion, green = roughness, blue = metallic (always 0 here).
// Some libraries ship occlusion and roughness as separate grayscale images.
//
// Exit codes: 0 = done, 1 = failed, 2 = bad arguments.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "poligon/image.h"

namespace {

int MergeAlpha(const char* color_path, const char* alpha_path, const char* output_path) {
    poligon::Image color;
    poligon::Image alpha;
    std::string error;
    if (!poligon::LoadImageFromFile(color_path, color, error) || !poligon::LoadImageFromFile(alpha_path, alpha, error)) {
        std::fprintf(stderr, "image_tool: %s\n", error.c_str());
        return 1;
    }
    if (color.width != alpha.width || color.height != alpha.height) {
        std::fprintf(stderr, "image_tool: the images differ in size (%dx%d and %dx%d)\n", color.width, color.height,
                     alpha.width, alpha.height);
        return 1;
    }

    long long transparent = 0;
    const size_t pixel_count = static_cast<size_t>(color.width) * static_cast<size_t>(color.height);
    for (size_t i = 0; i < pixel_count; ++i) {
        color.pixels[i * 4 + 3] = alpha.pixels[i * 4];
        transparent += alpha.pixels[i * 4] < 128 ? 1 : 0;
    }
    poligon::BleedColorsIntoTransparentPixels(color, 16);

    if (!poligon::SaveImageToPng(output_path, color)) {
        std::fprintf(stderr, "image_tool: cannot write %s\n", output_path);
        return 1;
    }
    std::printf("image_tool: wrote %s, %dx%d, %.0f%% of pixels cut away\n", output_path, color.width, color.height,
                100.0 * static_cast<double>(transparent) / static_cast<double>(pixel_count));
    return 0;
}

int PackOrm(const char* occlusion_path, const char* roughness_path, const char* output_path) {
    poligon::Image occlusion;
    poligon::Image roughness;
    std::string error;
    if (!poligon::LoadImageFromFile(occlusion_path, occlusion, error) ||
        !poligon::LoadImageFromFile(roughness_path, roughness, error)) {
        std::fprintf(stderr, "image_tool: %s\n", error.c_str());
        return 1;
    }
    if (occlusion.width != roughness.width || occlusion.height != roughness.height) {
        std::fprintf(stderr, "image_tool: the images differ in size (%dx%d and %dx%d)\n", occlusion.width,
                     occlusion.height, roughness.width, roughness.height);
        return 1;
    }
    const size_t pixel_count = static_cast<size_t>(occlusion.width) * static_cast<size_t>(occlusion.height);
    for (size_t i = 0; i < pixel_count; ++i) {
        occlusion.pixels[i * 4 + 1] = roughness.pixels[i * 4];
        occlusion.pixels[i * 4 + 2] = 0;
        occlusion.pixels[i * 4 + 3] = 255;
    }
    if (!poligon::SaveImageToPng(output_path, occlusion)) {
        std::fprintf(stderr, "image_tool: cannot write %s\n", output_path);
        return 1;
    }
    std::printf("image_tool: wrote %s, %dx%d\n", output_path, occlusion.width, occlusion.height);
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 5 && std::strcmp(argv[1], "merge-alpha") == 0) {
        return MergeAlpha(argv[2], argv[3], argv[4]);
    }
    if (argc == 5 && std::strcmp(argv[1], "pack-orm") == 0) {
        return PackOrm(argv[2], argv[3], argv[4]);
    }
    std::fprintf(stderr, "usage: image_tool merge-alpha COLOR_IMAGE ALPHA_IMAGE OUTPUT.png\n"
                         "       image_tool pack-orm OCCLUSION_IMAGE ROUGHNESS_IMAGE OUTPUT.png\n");
    return 2;
}

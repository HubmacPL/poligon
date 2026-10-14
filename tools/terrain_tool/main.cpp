// Prepares height maps for the terrain.
//
//   terrain_tool join OUT.heights SPACING COLUMNS ROWS FOLDER PREFIX
//       Joins elevation tiles (float TIFFs as Poland's Geoportal serves them) into one
//       height map. Tiles are named PREFIX_c<column>_r<row>.tif, column 0 in the west,
//       row 0 in the north. SPACING is the distance between their points in meters.
//   terrain_tool rescale IN.heights OUT.heights HORIZONTAL VERTICAL
//       Turns real land into game land: HORIZONTAL 0.5 halves all distances, VERTICAL
//       multiplies heights, and the lowest point is moved to height 0.
//   terrain_tool preview IN.heights OUT.png
//       Writes a picture of the map as if lit from the north-west, at most 1024 pixels wide.
//   terrain_tool info IN.heights
//       Prints size, height range and how steep the land is.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "poligon/height_map.h"
#include "poligon/image.h"

namespace {

int Fail(const std::string& message) {
    std::fprintf(stderr, "terrain_tool: %s\n", message.c_str());
    return 1;
}

void PrintInfo(const poligon::HeightMap& map) {
    float lowest = 0.0f;
    float highest = 0.0f;
    map.GetHeightRange(lowest, highest);
    std::printf("%d x %d points, %.3f m apart: %.0f x %.0f m\n", map.width, map.depth, static_cast<double>(map.spacing),
                static_cast<double>(map.GetSizeX()), static_cast<double>(map.GetSizeZ()));
    std::printf("heights from %.2f to %.2f m (%.2f m difference)\n", static_cast<double>(lowest), static_cast<double>(highest),
                static_cast<double>(highest - lowest));
    // How much of the land is how steep, from a grid of 200 x 200 places.
    int counts[4] = {0, 0, 0, 0};
    float steepest = 0.0f;
    const int samples = 200;
    for (int j = 0; j < samples; ++j) {
        for (int i = 0; i < samples; ++i) {
            const float slope = map.GetSlopeDegrees(map.GetSizeX() * (static_cast<float>(i) + 0.5f) / samples,
                                                    map.GetSizeZ() * (static_cast<float>(j) + 0.5f) / samples);
            steepest = std::fmax(steepest, slope);
            ++counts[slope < 10.0f ? 0 : slope < 25.0f ? 1 : slope < 45.0f ? 2 : 3];
        }
    }
    const double total = samples * samples / 100.0;
    std::printf("slopes: %.1f%% under 10 degrees, %.1f%% 10 to 25, %.1f%% 25 to 45, %.1f%% over 45; steepest found %.0f\n",
                counts[0] / total, counts[1] / total, counts[2] / total, counts[3] / total, static_cast<double>(steepest));
}

} // namespace

int main(int argc, char** argv) {
    const std::string command = argc > 1 ? argv[1] : "";
    std::string error;

    if (command == "join" && argc == 8) {
        const float spacing = std::strtof(argv[3], nullptr);
        const int columns = std::atoi(argv[4]);
        const int rows = std::atoi(argv[5]);
        if (spacing <= 0.0f || columns < 1 || rows < 1) {
            return Fail("SPACING, COLUMNS and ROWS must be above zero");
        }
        poligon::HeightMap whole;
        int total_missing = 0;
        for (int row = 0; row < rows; ++row) {
            for (int column = 0; column < columns; ++column) {
                const std::string path = std::string(argv[6]) + "/" + argv[7] + "_c" + std::to_string(column) + "_r" +
                                         std::to_string(row) + ".tif";
                poligon::HeightMap tile;
                int missing = 0;
                if (!poligon::LoadHeightMapFromTiff(path.c_str(), spacing, tile, missing, error)) {
                    return Fail(error);
                }
                if (whole.IsEmpty()) {
                    whole.width = tile.width * columns;
                    whole.depth = tile.depth * rows;
                    whole.spacing = spacing;
                    whole.heights.assign(static_cast<size_t>(whole.width) * static_cast<size_t>(whole.depth), 0.0f);
                } else if (tile.width * columns != whole.width || tile.depth * rows != whole.depth) {
                    return Fail(path + ": this tile has another size than the first one");
                }
                if (missing > 0) {
                    std::printf("%s: %d points without data\n", path.c_str(), missing);
                }
                total_missing += missing;
                poligon::PasteHeightMap(whole, tile, column * tile.width, row * tile.depth);
            }
        }
        const int filled = poligon::FillMissingHeights(whole);
        if (!poligon::SaveHeightMap(argv[2], whole)) {
            return Fail(std::string("cannot write ") + argv[2]);
        }
        PrintInfo(whole);
        std::printf("points without data: %d, filled from their neighbors: %d\nsaved %s\n", total_missing, filled, argv[2]);
        return 0;
    }

    if (command == "rescale" && argc == 6) {
        poligon::HeightMap map;
        if (!poligon::LoadHeightMap(argv[2], map, error)) {
            return Fail(error);
        }
        const float horizontal = std::strtof(argv[4], nullptr);
        const float vertical = std::strtof(argv[5], nullptr);
        if (horizontal <= 0.0f || vertical <= 0.0f) {
            return Fail("HORIZONTAL and VERTICAL must be above zero");
        }
        poligon::RescaleHeightMap(map, horizontal, vertical);
        if (!poligon::SaveHeightMap(argv[3], map)) {
            return Fail(std::string("cannot write ") + argv[3]);
        }
        PrintInfo(map);
        std::printf("saved %s\n", argv[3]);
        return 0;
    }

    if (command == "preview" && argc == 4) {
        poligon::HeightMap map;
        if (!poligon::LoadHeightMap(argv[2], map, error)) {
            return Fail(error);
        }
        const int width = map.width < 1024 ? map.width : 1024;
        const int height = static_cast<int>(static_cast<float>(width) * map.GetSizeZ() / map.GetSizeX());
        float lowest = 0.0f;
        float highest = 0.0f;
        map.GetHeightRange(lowest, highest);
        poligon::Image image = poligon::MakeSolidImage(width, height, 0, 0, 0);
        const glm::vec3 to_light = glm::normalize(glm::vec3(-1.0f, 1.2f, -1.0f));
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const float map_x = map.GetSizeX() * (static_cast<float>(x) + 0.5f) / static_cast<float>(width);
                const float map_z = map.GetSizeZ() * (static_cast<float>(y) + 0.5f) / static_cast<float>(height);
                // Slopes are exaggerated five times for the picture; real land is too gentle to read otherwise.
                glm::vec3 normal = map.GetNormal(map_x, map_z);
                normal = glm::normalize(glm::vec3(normal.x * 5.0f, normal.y, normal.z * 5.0f));
                const float light = glm::clamp(glm::dot(normal, to_light), 0.0f, 1.0f);
                const float level = (map.GetHeight(map_x, map_z) - lowest) / (highest - lowest + 0.001f);
                const float value = light * (0.45f + 0.55f * level);
                std::uint8_t* pixel = &image.pixels[static_cast<size_t>(y * width + x) * 4];
                pixel[0] = static_cast<std::uint8_t>(255.0f * glm::clamp(value * 1.05f, 0.0f, 1.0f));
                pixel[1] = static_cast<std::uint8_t>(255.0f * glm::clamp(value, 0.0f, 1.0f));
                pixel[2] = static_cast<std::uint8_t>(255.0f * glm::clamp(value * 0.85f, 0.0f, 1.0f));
            }
        }
        if (!poligon::SaveImageToPng(argv[3], image)) {
            return Fail(std::string("cannot write ") + argv[3]);
        }
        std::printf("saved %s (%d x %d)\n", argv[3], width, height);
        return 0;
    }

    if (command == "info" && argc == 3) {
        poligon::HeightMap map;
        if (!poligon::LoadHeightMap(argv[2], map, error)) {
            return Fail(error);
        }
        PrintInfo(map);
        return 0;
    }

    std::fprintf(stderr, "usage: terrain_tool join OUT.heights SPACING COLUMNS ROWS FOLDER PREFIX\n"
                         "       terrain_tool rescale IN.heights OUT.heights HORIZONTAL VERTICAL\n"
                         "       terrain_tool preview IN.heights OUT.png\n"
                         "       terrain_tool info IN.heights\n");
    return 2;
}

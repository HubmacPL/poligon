// Prepares height maps for the terrain.
//
//   terrain_tool join OUT.heights SPACING COLUMNS ROWS FOLDER PREFIX
//       Joins elevation tiles (float TIFFs as Poland's Geoportal serves them) into one
//       height map. Tiles are named PREFIX_c<column>_r<row>.tif, column 0 in the west,
//       row 0 in the north. SPACING is the distance between their points in meters.
//   terrain_tool rescale IN.heights OUT.heights HORIZONTAL VERTICAL
//       Turns real land into game land: HORIZONTAL 0.5 halves all distances, VERTICAL
//       multiplies heights, and the lowest point is moved to height 0.
//   terrain_tool crop IN.heights OUT.heights CENTER_X CENTER_Z SIZE
//       Cuts out a square of SIZE meters whose middle is CENTER_X, CENTER_Z (meters from
//       the map's north-west corner).
//   terrain_tool preview IN.heights OUT.png
//       Writes a picture of the map as if lit from the north-west, at most 1024 pixels wide.
//   terrain_tool info IN.heights
//       Prints size, height range and how steep the land is.
//   terrain_tool generate CACHE_FOLDER PREVIEW_FOLDER [NAME=VALUE ...]
//       Generates land (poligon/terrain_generator.h), or loads it from CACHE_FOLDER when
//       these settings were generated before, and writes pictures of it and its maps to
//       PREVIEW_FOLDER: relief.png (lit from the north-west, lakes blue), detail.png (the
//       middle 1024 x 1024 points, one pixel each), flow.png, moisture.png, sediment.png,
//       rock.png. NAME=VALUE changes a setting, for example
//       seed=7 size=2048 erosion_steps=100 (names as in TerrainGeneratorSettings).
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "poligon/height_map.h"
#include "poligon/image.h"
#include "poligon/terrain_generator.h"

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

// Sets one generator setting from "name=value"; false for an unknown name.
bool SetGeneratorSetting(poligon::TerrainGeneratorSettings& s, const char* text) {
    const char* equals = std::strchr(text, '=');
    if (equals == nullptr) {
        return false;
    }
    const std::string name(text, equals);
    const char* value = equals + 1;
    struct FloatSetting {
        const char* name;
        float* value;
    };
    const FloatSetting floats[] = {{"size", &s.size}, {"spacing", &s.spacing}, {"coarse_spacing", &s.coarse_spacing},
                                   {"map_spacing", &s.map_spacing}, {"relief", &s.relief}, {"initial_relief", &s.initial_relief},
                                   {"initial_hill_size", &s.initial_hill_size},
                                   {"years_per_step", &s.years_per_step}, {"uplift", &s.uplift},
                                   {"erodibility", &s.erodibility}, {"area_exponent", &s.area_exponent},
                                   {"hillslope_diffusion", &s.hillslope_diffusion}, {"basin_radius", &s.basin_radius},
                                   {"basin_start", &s.basin_start}, {"basin_sinking", &s.basin_sinking},
                                   {"detail_height", &s.detail_height}, {"talus_degrees", &s.talus_degrees},
                                   {"droplets_per_square_meter", &s.droplets_per_square_meter},
                                   {"droplet_strength", &s.droplet_strength}, {"min_lake_area", &s.min_lake_area},
                                   {"min_lake_depth", &s.min_lake_depth}};
    for (const FloatSetting& setting : floats) {
        if (name == setting.name) {
            *setting.value = std::strtof(value, nullptr);
            return true;
        }
    }
    struct IntSetting {
        const char* name;
        int* value;
    };
    const IntSetting ints[] = {{"erosion_steps", &s.erosion_steps}, {"basin_count", &s.basin_count},
                               {"outlet_edges", &s.outlet_edges}, {"thermal_iterations", &s.thermal_iterations},
                               {"droplet_steps", &s.droplet_steps}};
    for (const IntSetting& setting : ints) {
        if (name == setting.name) {
            *setting.value = std::atoi(value);
            return true;
        }
    }
    if (name == "seed") {
        s.seed = std::strtoull(value, nullptr, 10);
        return true;
    }
    return false;
}

// A picture of one map: black (low) to a color (high), `to_unit` turning a value into 0..1.
template <typename ToUnit>
bool SaveMapPicture(const std::string& path, const poligon::TerrainMaps& maps, const std::vector<float>& map, glm::vec3 color,
                    ToUnit to_unit) {
    const int size = maps.map_width < 1024 ? maps.map_width : 1024;
    poligon::Image image = poligon::MakeSolidImage(size, size, 0, 0, 0);
    const float extent = static_cast<float>(maps.map_width - 1) * maps.map_spacing;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const float value = glm::clamp(to_unit(maps.Sample(map, extent * (static_cast<float>(x) + 0.5f) / static_cast<float>(size),
                                                               extent * (static_cast<float>(y) + 0.5f) / static_cast<float>(size))),
                                           0.0f, 1.0f);
            std::uint8_t* pixel = &image.pixels[static_cast<size_t>(y * size + x) * 4];
            for (int c = 0; c < 3; ++c) {
                pixel[c] = static_cast<std::uint8_t>(255.0f * value * color[c]);
            }
        }
    }
    return poligon::SaveImageToPng(path.c_str(), image);
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

    if (command == "crop" && argc == 7) {
        poligon::HeightMap map;
        if (!poligon::LoadHeightMap(argv[2], map, error)) {
            return Fail(error);
        }
        const float size = std::strtof(argv[6], nullptr);
        const int points = static_cast<int>(size / map.spacing);
        const int ix = static_cast<int>((std::strtof(argv[4], nullptr) - size * 0.5f) / map.spacing);
        const int iz = static_cast<int>((std::strtof(argv[5], nullptr) - size * 0.5f) / map.spacing);
        if (points < 2 || ix < 0 || iz < 0 || ix + points > map.width || iz + points > map.depth) {
            return Fail("the square does not lie inside the map");
        }
        const poligon::HeightMap part = poligon::CropHeightMap(map, ix, iz, points, points);
        if (!poligon::SaveHeightMap(argv[3], part)) {
            return Fail(std::string("cannot write ") + argv[3]);
        }
        PrintInfo(part);
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

    if (command == "generate" && argc >= 4) {
        poligon::TerrainGeneratorSettings settings;
        for (int i = 4; i < argc; ++i) {
            if (!SetGeneratorSetting(settings, argv[i])) {
                return Fail(std::string("unknown setting ") + argv[i]);
            }
        }
        const auto start = std::chrono::steady_clock::now();
        poligon::TerrainMaps maps;
        bool generated = false;
        if (!poligon::LoadOrGenerateTerrain(settings, argv[2], maps, generated, error)) {
            return Fail(error);
        }
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        std::printf("%s in %.1f s, hash %016llx\n", generated ? "generated" : "loaded", seconds,
                    static_cast<unsigned long long>(poligon::HashTerrainMaps(maps)));
        PrintInfo(maps.height);
        for (const poligon::Lake& lake : maps.lakes) {
            std::printf("lake at %.0f, %.0f: level %.2f m, %.0f m2, %.1f m deep\n", static_cast<double>(lake.center.x),
                        static_cast<double>(lake.center.y), static_cast<double>(lake.level), static_cast<double>(lake.area),
                        static_cast<double>(lake.max_depth));
        }

        const std::string folder = argv[3];
        const poligon::HeightMap& map = maps.height;
        float lowest = 0.0f;
        float highest = 0.0f;
        map.GetHeightRange(lowest, highest);
        const glm::vec3 to_light = glm::normalize(glm::vec3(-1.0f, 1.2f, -1.0f));
        // A lit picture of the square from (start_x, start_z), `extent` meters wide.
        const auto draw_relief = [&](float start_x, float start_z, float extent, int width, float exaggeration) {
        poligon::Image image = poligon::MakeSolidImage(width, width, 0, 0, 0);
        for (int y = 0; y < width; ++y) {
            for (int x = 0; x < width; ++x) {
                const float map_x = start_x + extent * (static_cast<float>(x) + 0.5f) / static_cast<float>(width);
                const float map_z = start_z + extent * (static_cast<float>(y) + 0.5f) / static_cast<float>(width);
                glm::vec3 normal = map.GetNormal(map_x, map_z);
                normal = glm::normalize(glm::vec3(normal.x * exaggeration, normal.y, normal.z * exaggeration));
                const float light = glm::clamp(glm::dot(normal, to_light), 0.0f, 1.0f);
                const float level = (map.GetHeight(map_x, map_z) - lowest) / (highest - lowest + 0.001f);
                glm::vec3 color = glm::vec3(0.55f + 0.45f * level, 0.6f + 0.35f * level, 0.4f + 0.4f * level) * (0.25f + 0.75f * light);
                const float water = maps.GetWaterDepth(map_x, map_z);
                if (water > 0.0f) {
                    color = glm::mix(glm::vec3(0.35f, 0.55f, 0.75f), glm::vec3(0.05f, 0.15f, 0.35f), glm::clamp(water / 6.0f, 0.0f, 1.0f));
                }
                std::uint8_t* pixel = &image.pixels[static_cast<size_t>(y * width + x) * 4];
                for (int c = 0; c < 3; ++c) {
                    pixel[c] = static_cast<std::uint8_t>(255.0f * glm::clamp(color[c], 0.0f, 1.0f));
                }
            }
        }
        return image;
        };
        const int overview = map.width < 1024 ? map.width : 1024;
        bool saved = poligon::SaveImageToPng((folder + "/relief.png").c_str(), draw_relief(0.0f, 0.0f, map.GetSizeX(), overview, 3.0f));
        const float detail_extent = std::fmin(1024.0f * map.spacing, map.GetSizeX());
        saved = saved && poligon::SaveImageToPng((folder + "/detail.png").c_str(),
                                                 draw_relief((map.GetSizeX() - detail_extent) * 0.5f, (map.GetSizeZ() - detail_extent) * 0.5f,
                                                             detail_extent, 1024, 1.0f));
        saved = saved && SaveMapPicture(folder + "/flow.png", maps, maps.flow, glm::vec3(0.5f, 0.8f, 1.0f),
                                        [](float v) { return (std::log10(v) - 1.5f) / 5.0f; });
        saved = saved && SaveMapPicture(folder + "/moisture.png", maps, maps.moisture, glm::vec3(0.3f, 0.7f, 1.0f),
                                        [](float v) { return v; });
        saved = saved && SaveMapPicture(folder + "/sediment.png", maps, maps.sediment, glm::vec3(1.0f, 0.85f, 0.5f),
                                        [](float v) { return std::sqrt(v / 3.0f); });
        saved = saved && SaveMapPicture(folder + "/rock.png", maps, maps.rock, glm::vec3(1.0f, 1.0f, 1.0f),
                                        [](float v) { return v; });
        if (!saved) {
            return Fail("cannot write the pictures to " + folder);
        }
        std::printf("pictures saved to %s\n", folder.c_str());
        return 0;
    }

    std::fprintf(stderr, "usage: terrain_tool join OUT.heights SPACING COLUMNS ROWS FOLDER PREFIX\n"
                         "       terrain_tool rescale IN.heights OUT.heights HORIZONTAL VERTICAL\n"
                         "       terrain_tool crop IN.heights OUT.heights CENTER_X CENTER_Z SIZE\n"
                         "       terrain_tool preview IN.heights OUT.png\n"
                         "       terrain_tool info IN.heights\n"
                         "       terrain_tool generate CACHE_FOLDER PREVIEW_FOLDER [NAME=VALUE ...]\n");
    return 2;
}

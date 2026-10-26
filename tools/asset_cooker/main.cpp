// Turns the sources named in an asset list into files that load fast (poligon/assets/assets.h).
//
//   asset_cooker LIST SOURCE_FOLDER COOKED_FOLDER [--level LEVEL [--terrain-cache FOLDER]] [--pack ARCHIVE]
//
// Cooks every asset whose cooked file is missing or older than its sources or the list.
// --level also cooks the streamed world the level names (its "world" line, see
// poligon/world/land.h), when its files are missing or were made from other settings; the
// generated land is kept in the terrain cache (default build/terrain_cache). --pack then
// writes one archive of the whole cooked folder. Prints what it did; exit code 1 when an
// asset failed (the others are still cooked).
// Example: asset_cooker tools/forest_scene/assets.txt ../poligon-assets/source ../poligon-assets/cooked
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>

#include "poligon/assets/assets.h"
#include "poligon/assets/cooking.h"
#include "poligon/world/land.h"
#include "poligon/world/level.h"
#include "poligon/world/plant_rules.h"
#include "poligon/world/terrain.h"

namespace {

int Usage() {
    std::fprintf(stderr, "usage: asset_cooker LIST SOURCE_FOLDER COOKED_FOLDER [--level LEVEL [--terrain-cache FOLDER]] [--pack ARCHIVE]\n");
    return 2;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        return Usage();
    }
    std::string level_path;
    std::string terrain_cache = "build/terrain_cache";
    std::string archive;
    for (int i = 4; i < argc; i += 2) {
        if (i + 1 >= argc) {
            return Usage();
        }
        if (std::strcmp(argv[i], "--level") == 0) {
            level_path = argv[i + 1];
        } else if (std::strcmp(argv[i], "--terrain-cache") == 0) {
            terrain_cache = argv[i + 1];
        } else if (std::strcmp(argv[i], "--pack") == 0) {
            archive = argv[i + 1];
        } else {
            return Usage();
        }
    }
    poligon::AssetList list;
    std::string error;
    if (!poligon::LoadAssetList(argv[1], list, error)) {
        std::fprintf(stderr, "asset_cooker: %s\n", error.c_str());
        return 1;
    }
    const auto started = std::chrono::steady_clock::now();
    const poligon::CookReport report = poligon::CookAssets(list, argv[1], argv[2], argv[3]);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    std::printf("asset_cooker: %d cooked, %d up to date, %d failed, in %.1f s\n", report.cooked, report.up_to_date,
                static_cast<int>(report.errors.size()), seconds);
    for (const std::string& failure : report.errors) {
        std::fprintf(stderr, "asset_cooker: %s\n", failure.c_str());
    }
    if (!level_path.empty()) {
        // The level's lines for plant rules and ground layers are kept for their readers.
        std::vector<std::string> prefixes = poligon::GetPlantRuleWords();
        for (const std::string& word : poligon::GetGroundLayerWords()) {
            prefixes.push_back(word);
        }
        poligon::Level level;
        std::vector<poligon::GroundLayer> layers;
        bool cooked = false;
        const auto world_started = std::chrono::steady_clock::now();
        if (!poligon::LoadLevel(level_path.c_str(), level, error, prefixes) || !poligon::ParseGroundLayers(level.other_lines, layers, error) ||
            !poligon::CookLevelWorld(level, layers, argv[3], terrain_cache.c_str(), cooked, error)) {
            std::fprintf(stderr, "asset_cooker: %s: %s\n", level_path.c_str(), error.c_str());
            return 1;
        }
        std::printf("asset_cooker: world %s %s in %.1f s\n", level.world.c_str(), cooked ? "cooked" : "up to date",
                    std::chrono::duration<double>(std::chrono::steady_clock::now() - world_started).count());
    }
    if (!archive.empty()) {
        if (!poligon::PackAssets(argv[3], archive.c_str(), error)) {
            std::fprintf(stderr, "asset_cooker: %s\n", error.c_str());
            return 1;
        }
        std::printf("asset_cooker: packed %s\n", archive.c_str());
    }
    return report.errors.empty() ? 0 : 1;
}

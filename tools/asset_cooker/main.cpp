// Turns the sources named in an asset list into files that load fast (poligon/assets.h).
//
//   asset_cooker LIST SOURCE_FOLDER COOKED_FOLDER [--pack ARCHIVE]
//
// Cooks every asset whose cooked file is missing or older than its sources or the list,
// and with --pack also writes one archive of the whole cooked folder. Prints what it did;
// exit code 1 when an asset failed (the others are still cooked).
// Example: asset_cooker tools/forest_scene/assets.txt ../poligon-assets build/cooked
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>

#include "poligon/assets.h"

int main(int argc, char** argv) {
    if (argc != 4 && !(argc == 6 && std::strcmp(argv[4], "--pack") == 0)) {
        std::fprintf(stderr, "usage: asset_cooker LIST SOURCE_FOLDER COOKED_FOLDER [--pack ARCHIVE]\n");
        return 2;
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
    if (argc == 6) {
        if (!poligon::PackAssets(argv[3], argv[5], error)) {
            std::fprintf(stderr, "asset_cooker: %s\n", error.c_str());
            return 1;
        }
        std::printf("asset_cooker: packed %s\n", argv[5]);
    }
    return report.errors.empty() ? 0 : 1;
}

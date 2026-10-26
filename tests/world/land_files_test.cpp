// Checks cooked land (poligon/world/land.h): a level that names a streamed world is cooked
// into patch files, which read back, from the cooked folder and from an archive, equal the
// patches cut from the generated maps to the bit; cooking again is skipped while nothing
// changed and done again when a ground layer changes. Also checks that ground layers and
// plant rules share a level file.
// Usage: land_files_test OUTPUT_DIRECTORY
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "poligon/assets/assets.h"
#include "poligon/assets/cooking.h"
#include "poligon/world/land.h"
#include "poligon/world/level.h"
#include "poligon/world/plant_rules.h"
#include "poligon/world/terrain.h"
#include "test_support.h"

namespace {

bool SameMaps(const poligon::TerrainMaps& a, const poligon::TerrainMaps& b) {
    return a.height.width == b.height.width && a.height.depth == b.height.depth && a.height.first_x == b.height.first_x &&
           a.height.first_z == b.height.first_z && a.height.heights == b.height.heights && a.map_width == b.map_width &&
           a.map_depth == b.map_depth && a.map_first_x == b.map_first_x && a.map_first_z == b.map_first_z &&
           a.water_level == b.water_level && a.flow == b.flow && a.moisture == b.moisture && a.sediment == b.sediment &&
           a.rock == b.rock && a.water_distance == b.water_distance && a.lakes.size() == b.lakes.size();
}

bool SamePatch(const poligon::LandPatch& a, const poligon::LandPatch& b) {
    return a.patch == b.patch && SameMaps(a.maps, b.maps) && a.weights[0].pixels == b.weights[0].pixels &&
           a.weights[1].pixels == b.weights[1].pixels && a.weights[0].width == b.weights[0].width;
}

const char* const kLevel = R"(poligon-level 1
terrain.seed 9
terrain.size 512
terrain.erosion_steps 30
terrain.initial_hill_size 250
terrain.basin_count 1
terrain.basin_radius 100
terrain.thermal_iterations 4
terrain.droplets_per_square_meter 0.03

world tests/land
world.patch_size 128
world.background_spacing 16

ground ground/grass
tile 3
strength 0.5

plant trees/pine
spacing 8
density 0.01
slope 0 25 4

ground ground/rock
tile 6
slope 25 90 5
patch 40 0.5
)";

} // namespace

int main(int argc, char** argv) {
    using test::Expect;
    if (argc != 2) {
        std::printf("usage: land_files_test OUTPUT_DIRECTORY\n");
        return 2;
    }
    const std::filesystem::path output(argv[1]);
    const std::filesystem::path cooked = output / "land_files_cooked";
    const std::filesystem::path cache = output / "land_files_cache";
    std::error_code ec;
    std::filesystem::remove_all(cooked, ec);
    std::filesystem::create_directories(cooked, ec);

    // --- The level: ground layers and plant rules in one file ---
    const std::filesystem::path level_path = output / "land_files_test.level";
    std::ofstream(level_path, std::ios::binary) << kLevel;
    std::vector<std::string> prefixes = poligon::GetPlantRuleWords();
    for (const std::string& word : poligon::GetGroundLayerWords()) {
        prefixes.push_back(word);
    }
    poligon::Level level;
    std::string error;
    Expect(poligon::LoadLevel(level_path.string().c_str(), level, error, prefixes), "the level is read");
    std::vector<poligon::GroundLayer> layers;
    std::vector<poligon::PlantRule> rules;
    Expect(poligon::ParseGroundLayers(level.other_lines, layers, error) && layers.size() == 2, "two ground layers are read");
    Expect(poligon::ParsePlantRules(level.other_lines, rules, error) && rules.size() == 1, "and the one plant rule between them");
    Expect(layers.size() == 2 && layers[1].texture == "ground/rock" && layers[1].tile_size == 6.0f &&
               layers[1].rule.conditions.slope_degrees.low == 25.0f && layers[1].rule.patch_size == 40.0f && layers[0].rule.strength == 0.5f,
           "a ground layer's values are read");
    Expect(rules.size() == 1 && rules[0].conditions.slope_degrees.high == 25.0f, "the plant rule keeps its own conditions");
    Expect(level.world == "tests/land" && level.world_patch_size == 128.0 && level.world_background_spacing == 16.0f,
           "the level names its streamed world");
    Expect(!poligon::ParseGroundLayers({{1, "tile 3"}}, layers, error) && error.find("line 1") != std::string::npos,
           "a ground setting before any layer is an error that names its line");

    // --- Cooking ---
    poligon::ParseGroundLayers(level.other_lines, layers, error);
    bool did_cook = false;
    Expect(poligon::CookLevelWorld(level, layers, cooked.string().c_str(), cache.string().c_str(), did_cook, error) && did_cook,
           "the world is cooked");
    bool again = true;
    Expect(poligon::CookLevelWorld(level, layers, cooked.string().c_str(), cache.string().c_str(), again, error) && !again,
           "cooking again with the same settings is skipped");

    // The same land cut from the generated maps.
    poligon::TerrainMaps maps;
    bool generated = false;
    poligon::LoadOrGenerateTerrain(level.terrain, cache.string().c_str(), maps, generated, error);
    std::vector<poligon::TerrainLayerRule> layer_rules;
    for (const poligon::GroundLayer& layer : layers) {
        layer_rules.push_back(layer.rule);
    }
    const poligon::LandFromMaps from_maps(maps, 128.0, glm::dvec3(0.0), layer_rules.data(), 2, 1u, 16.0f);

    // --- Reading back, from the folder and from an archive ---
    const std::string archive = (output / "land_files.pak").string();
    Expect(poligon::PackAssets(cooked.string().c_str(), archive.c_str(), error), "the cooked world is packed into an archive");
    for (const bool from_archive : {false, true}) {
        poligon::AssetLibrary library;
        Expect(library.Open(from_archive ? archive.c_str() : cooked.string().c_str(), error), "the cooked files are opened");
        poligon::LandFromFiles from_files(library, "tests/land", glm::dvec3(10.0, 0.0, 20.0));
        Expect(from_files.IsOpen(), from_archive ? "the world opens from the archive" : "the world opens from the folder");
        if (!from_files.IsOpen()) {
            continue;
        }
        const poligon::LandInfo& read = from_files.GetInfo();
        const poligon::LandInfo& made = from_maps.GetInfo();
        Expect(read.grid.patch_count_x == 4 && read.grid.patch_size == 128.0 && read.world_origin.x == 10.0 &&
                   read.background.heights == made.background.heights &&
                   read.background_weights[1].pixels == made.background_weights[1].pixels && read.lakes.size() == made.lakes.size(),
               "the world's description equals the land's");
        bool all_same = true;
        for (int z = 0; z < 4; ++z) {
            for (int x = 0; x < 4; ++x) {
                all_same = all_same && SamePatch(*from_files.GetPatch({x, z}), from_maps.CutPatch({x, z}));
            }
        }
        Expect(all_same, from_archive ? "every patch read from the archive equals the cut patch to the bit"
                                      : "every patch read from the folder equals the cut patch to the bit");
    }

    // A changed ground layer cooks the world again.
    layers[1].rule.strength = 2.0f;
    Expect(poligon::CookLevelWorld(level, layers, cooked.string().c_str(), cache.string().c_str(), again, error) && again,
           "a changed ground layer cooks the world again");
    return test::ExitCode();
}

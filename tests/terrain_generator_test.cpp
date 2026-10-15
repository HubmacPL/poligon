// Checks the terrain generator: the same settings give the same land to the bit, a saved
// file loads back the same, and the maps make physical sense (lakes hold water up to a
// level, water gathers in valleys, ground is wetter near water than on a hilltop).
// Usage: terrain_generator_test OUTPUT_DIRECTORY
#include <cstdio>
#include <cstdlib>
#include <string>

#include "poligon/terrain.h"
#include "poligon/terrain_generator.h"
#include "test_support.h"

namespace {

// A small, quick land: 512 m with one basin in it.
poligon::TerrainGeneratorSettings SmallSettings(std::uint64_t seed) {
    poligon::TerrainGeneratorSettings settings;
    settings.seed = seed;
    settings.size = 512.0f;
    settings.erosion_steps = 40;
    settings.initial_hill_size = 300.0f;
    settings.initial_relief = 20.0f;
    settings.relief = 30.0f;
    settings.basin_count = 1;
    settings.basin_radius = 110.0f;
    settings.basin_sinking = 0.8f;
    settings.thermal_iterations = 6;
    settings.droplets_per_square_meter = 0.05f;
    settings.min_lake_area = 100.0f;
    settings.min_lake_depth = 0.2f;
    return settings;
}

} // namespace

int main(int argc, char** argv) {
    using test::Expect;
    if (argc != 2) {
        std::printf("usage: terrain_generator_test OUTPUT_DIRECTORY\n");
        return 2;
    }
    const std::string directory = argv[1];

    poligon::TerrainMaps first;
    poligon::TerrainMaps second;
    poligon::GenerateTerrain(SmallSettings(5), first);
    poligon::GenerateTerrain(SmallSettings(5), second);
    const std::uint64_t hash = poligon::HashTerrainMaps(first);
    Expect(hash == poligon::HashTerrainMaps(second) && first.height.heights == second.height.heights,
           "the same settings give the same land to the bit");
    poligon::TerrainMaps other;
    poligon::GenerateTerrain(SmallSettings(6), other);
    Expect(poligon::HashTerrainMaps(other) != hash, "another seed gives other land");

    Expect(first.height.width == 513 && first.height.depth == 513 && first.map_width == 257 && first.map_depth == 257,
           "512 m at 1 m gives 513 points, the maps at 2 m 257");
    const size_t map_count = static_cast<size_t>(first.map_width) * static_cast<size_t>(first.map_depth);
    Expect(first.flow.size() == map_count && first.moisture.size() == map_count && first.sediment.size() == map_count &&
               first.rock.size() == map_count && first.water_level.size() == map_count && first.water_distance.size() == map_count,
           "every map covers the map grid");
    float lowest = 0.0f;
    float highest = 0.0f;
    first.height.GetHeightRange(lowest, highest);
    Expect(highest - lowest > 20.0f && highest - lowest < 50.0f, "the relief is about what was asked for");
    Expect(lowest > -1.0f, "no pits dug below the lowest ground of the large-scale stage");

    // Lakes: water stands up to one level, deepest inside.
    Expect(!first.lakes.empty(), "the basin holds a lake");
    if (!first.lakes.empty()) {
        const poligon::Lake& lake = first.lakes[0];
        Expect(lake.max_depth >= 0.2f && lake.area >= 100.0f, "the lake is at least as large and deep as asked");
        int wet = 0;
        for (int z = 0; z < first.map_depth; ++z) {
            for (int x = 0; x < first.map_width; ++x) {
                const float px = static_cast<float>(x) * first.map_spacing;
                const float pz = static_cast<float>(z) * first.map_spacing;
                if (first.GetWaterDepth(px, pz) > 0.0f) {
                    ++wet;
                }
            }
        }
        Expect(wet > 20, "the lake covers ground on the map grid");
        // Moisture is highest at the water and lower on the highest ground.
        const size_t lake_point = static_cast<size_t>(lake.center.y / first.map_spacing + 0.5f) * static_cast<size_t>(first.map_width) +
                                  static_cast<size_t>(lake.center.x / first.map_spacing + 0.5f);
        size_t top_point = 0;
        for (size_t i = 0; i < first.height.heights.size(); ++i) {
            if (first.height.heights[i] == highest) {
                const size_t x = (i % static_cast<size_t>(first.height.width)) / 2;
                const size_t z = (i / static_cast<size_t>(first.height.width)) / 2;
                top_point = z * static_cast<size_t>(first.map_width) + x;
            }
        }
        Expect(first.moisture[top_point] < 0.6f && first.moisture[top_point] < first.moisture[lake_point],
               "a hilltop is drier than the lake shore");
        Expect(first.water_distance[top_point] > 20.0f, "a hilltop is far from water");
    }

    // Flow: the largest drained area is far larger than a hilltop's own point.
    float most_flow = 0.0f;
    for (const float flow : first.flow) {
        most_flow = flow > most_flow ? flow : most_flow;
    }
    Expect(most_flow > 20000.0f, "water from a large part of the land gathers in one valley");

    // Layer weights from rules: layer 1 under water, layer 0 elsewhere.
    if (!first.lakes.empty()) {
        poligon::TerrainLayerRule rules[2];
        rules[0].strength = 0.1f;
        rules[1].water_depth = {0.3f, 1000.0f, 0.1f};
        poligon::Image weights[2];
        poligon::MakeTerrainLayerWeights(first, rules, 2, 1u, weights);
        Expect(weights[0].width == first.map_width && weights[0].height == first.map_depth && weights[1].width == first.map_width,
               "the weight images have one pixel per map point");
        int under_water = 0;
        int right = 0;
        for (int z = 0; z < first.map_depth; ++z) {
            for (int x = 0; x < first.map_width; ++x) {
                const size_t pixel = static_cast<size_t>(z * first.map_width + x) * 4;
                const float depth = first.GetWaterDepth(static_cast<float>(x) * first.map_spacing, static_cast<float>(z) * first.map_spacing);
                const int layer0 = weights[0].pixels[pixel];
                const int layer1 = weights[0].pixels[pixel + 1];
                if (depth > 0.5f) {
                    ++under_water;
                    right += layer1 > 200 ? 1 : 0;
                }
                if (std::abs(layer0 + layer1 - 255) > 1) {
                    right = -1000000;
                }
            }
        }
        Expect(under_water > 0 && right == under_water, "a rule for deep water covers the lake bed, and weights add up to one");
        poligon::Image again[2];
        poligon::MakeTerrainLayerWeights(first, rules, 2, 1u, again);
        Expect(again[0].pixels == weights[0].pixels, "the same maps and rules give the same weights");
    }

    // Save and load give back the same land.
    const std::string path = directory + "/terrain_generator_test.bin";
    Expect(poligon::SaveTerrainMaps(path.c_str(), first), "generated land is saved");
    poligon::TerrainMaps loaded;
    std::string error;
    Expect(poligon::LoadTerrainMaps(path.c_str(), loaded, error) && poligon::HashTerrainMaps(loaded) == hash,
           "saved land loads back the same to the bit");
    Expect(poligon::HashTerrainGeneratorSettings(SmallSettings(5)) == poligon::HashTerrainGeneratorSettings(SmallSettings(5)) &&
               poligon::HashTerrainGeneratorSettings(SmallSettings(5)) != poligon::HashTerrainGeneratorSettings(SmallSettings(6)),
           "settings hashes name the settings");

    std::printf("terrain_generator_test finished (hash %016llx, %d lakes)\n", static_cast<unsigned long long>(hash),
                static_cast<int>(first.lakes.size()));
    return test::ExitCode();
}

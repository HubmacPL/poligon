#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "poligon/world/height_map.h"

namespace poligon {

// Land made by simulating the processes that shape real land, not by noise alone.
//
// 1. Large scale, on a coarse grid (4 m by default): the crust rises (uplift, faster in
//    some places, slower or sinking in a few basins), rivers cut into it (stream power
//    erosion, solved implicitly: Braun and Willett 2013), slopes creep down (hillslope
//    diffusion), and water that cannot leave a hollow fills it to its rim and flows over
//    (Priority-Flood: Barnes, Lehman and Mulla 2014). Sediment carried into such a lake
//    settles there. This gives the river network, valleys, ridges and lake basins.
// 2. Fine scale, on the height grid (1 m by default): the coarse land is interpolated, gets
//    small bumps, loose material slides down slopes steeper than it can stand (thermal
//    erosion, Musgrave, Kolb and Mace 1989), and many short-lived raindrops carve gullies
//    and drop what they carry where they slow down (droplet erosion, Beyer 2015).
// 3. Maps, on the map grid (2 m by default): where lakes stand and how high, how much
//    land drains through each point (flow), how wet the ground is (from the topographic
//    wetness index, Beven and Kirkby 1979, and the distance to water), how much loose
//    material lies there (sediment) and where bare rock shows (rock).
//
// Everything runs on one thread with our own random numbers, so the same settings give the
// same land to the bit (on one machine and executable). Generating the default 6 x 6 km
// takes about a minute; LoadOrGenerateTerrain keeps the result in a file.

struct TerrainGeneratorSettings {
    std::uint64_t seed = 1;
    // Side of the square land in meters.
    float size = 6000.0f;
    // Meters between height points, between points of the large-scale simulation, and
    // between points of the maps. The last two must be whole multiples of the first.
    float spacing = 1.0f;
    float coarse_spacing = 4.0f;
    float map_spacing = 2.0f;
    // Height from the lowest point to the highest top after the large-scale stage, meters.
    float relief = 110.0f;

    // --- Large scale ---
    // The older land the simulation starts from: rolling hills of this height (meters)
    // and distance from one to the next (meters).
    float initial_relief = 60.0f;
    float initial_hill_size = 1400.0f;
    // Steps of the simulation and years per step.
    int erosion_steps = 150;
    float years_per_step = 4000.0f;
    // Highest uplift, meters per year; most of the land rises at 55 to 100% of it.
    float uplift = 0.0002f;
    // How easily rivers cut (K of the stream power law E = K A^m S) and the exponent m of
    // the drained area A.
    float erodibility = 0.00004f;
    float area_exponent = 0.45f;
    // How fast slopes creep, square meters per year.
    float hillslope_diffusion = 0.01f;
    // Areas that, late in the simulation, stop rising and sink a little (as land does over
    // faults or melting ice); lakes form in them. `basin_start` is the share of the steps
    // after which they start, `basin_sinking` how much slower than the highest uplift
    // their middle moves (1.2 = it sinks at 0.2 of that rate).
    int basin_count = 3;
    float basin_radius = 350.0f;
    float basin_start = 0.8f;
    float basin_sinking = 0.45f;
    // Edges rivers may leave the land through (the others are closed): 1 = north (z = 0),
    // 2 = east (x = size), 4 = south (z = size), 8 = west (x = 0). Outlet edges stand for
    // land that goes on beyond the map: they keep the old hills' heights and rise slowly.
    int outlet_edges = 1 | 2 | 4 | 8;

    // --- Fine scale ---
    // Size of the small bumps added to the interpolated land, meters.
    float detail_height = 1.2f;
    // Steepest slope loose material stands at, and how many times it is let slide.
    float talus_degrees = 36.0f;
    int thermal_iterations = 20;
    // Raindrops per square meter of land, how strongly each one cuts, and how many steps
    // of one meter it runs at most (longer runs join rills into gullies).
    float droplets_per_square_meter = 0.15f;
    float droplet_strength = 6.0f;
    int droplet_steps = 96;

    // --- Maps ---
    // Smaller hollows hold no water.
    float min_lake_area = 300.0f;
    float min_lake_depth = 0.4f;
};

// A lake: water standing in a hollow up to the height where it would spill over.
struct Lake {
    glm::vec2 center{0.0f};  // middle of its surface, meters from the land's first point
    float level = 0.0f;      // height of the water surface
    float area = 0.0f;       // square meters of water
    float max_depth = 0.0f;  // meters
};

// What the generator gives: the height map and the maps that describe the land.
// All positions are meters from the land's first point, x along a row, z from row to row.
struct TerrainMaps {
    HeightMap height;

    // The maps share one grid: map_width x map_depth points, map_spacing meters apart,
    // starting at the same corner as the height map. Point (ix, iz) is at iz * map_width + ix.
    int map_width = 0;
    int map_depth = 0;
    float map_spacing = 2.0f;
    // For a window of larger maps (CutTerrainMaps): the larger grid's point where the
    // window starts. Positions stay those of the larger maps.
    int map_first_x = 0;
    int map_first_z = 0;
    // Height of the water surface, or kNoWater where no lake stands (or borders: the level
    // reaches one point past the shore, so that the shore can follow the finer heights).
    std::vector<float> water_level;
    // Square meters of land whose water flows through the point (lakes pass it on).
    std::vector<float> flow;
    // 0 = dry, 1 = wet.
    std::vector<float> moisture;
    // Meters of loose material laid down by water and slides.
    std::vector<float> sediment;
    // 0 = soil, 1 = bare rock.
    std::vector<float> rock;
    // Meters to the nearest lake water (0 in water, at most 1000).
    std::vector<float> water_distance;
    std::vector<Lake> lakes;

    static constexpr float kNoWater = -1.0e30f;

    // A map's value at any place, blended from the four points around it; outside the
    // grid the nearest edge counts. 0 for a map the land does not have (empty): ask
    // HasMap first where 0 would mean something (land from a height map has none of them).
    float Sample(const std::vector<float>& map, float x, float z) const;
    bool HasMap(const std::vector<float>& map) const;
    // Meters of water above the ground there; 0 on land, and where there is no water map.
    float GetWaterDepth(float x, float z) const;
};

// A range of values with soft ends: full weight from `low` to `high`, falling to nothing
// over `fade` beyond each end. The default range takes everything.
struct TerrainRange {
    float low = -1.0e30f;
    float high = 1.0e30f;
    float fade = 1.0f;
};

// Conditions on the maps at a place, each a soft range; together they give a weight from
// 0 to 1 (the product). Used by ground layers (poligon/world/terrain.h) and plant rules
// (poligon/world/plant_rules.h). Ranges left at their default are not even looked up. A
// condition on a map the land does not have (land from a height map: everything but slope
// and height) is not met: the weight is 0, never a made-up value.
struct MapConditions {
    TerrainRange slope_degrees;
    TerrainRange height;          // meters
    TerrainRange moisture;        // 0 = dry, 1 = wet
    TerrainRange sediment;        // meters of loose material
    TerrainRange rock;            // 0 = soil, 1 = bare rock
    TerrainRange flow;            // log10 of the square meters that drain through
    TerrainRange water_distance;  // meters to lake water
    TerrainRange water_depth;     // meters of water above the ground; 0 on land

    // `place` in meters on the map.
    float Weight(const struct TerrainMaps& maps, glm::vec2 place) const;
};

// For text files that set conditions (plant rules, ground layers): the range a word names
// ("slope", "height", "moisture", "sediment", "rock", "flow", "water_distance",
// "water_depth"), or nullptr for another word; and reading "low high fade" into a range
// (false for wrong numbers or low above high).
TerrainRange* FindMapCondition(MapConditions& conditions, const std::string& word);
bool ReadTerrainRange(const std::vector<std::string>& values, TerrainRange& range);

// Runs the whole simulation. Ends the process with FatalError on invalid settings.
void GenerateTerrain(const TerrainGeneratorSettings& settings, TerrainMaps& maps);

// A window of the maps over the rectangle [low, high] (meters), widened by `margin` meters
// and kept inside the maps: the height map and every map, with their grids' first points
// (HeightMap::first_x, TerrainMaps::map_first_x) set so that positions stay those of the
// whole maps. Within the rectangle every Sample, GetHeight and MapConditions::Weight gives
// the whole maps' numbers to the bit, as long as `margin` is at least two map points. Lakes
// are copied whole (they are few).
TerrainMaps CutTerrainMaps(const TerrainMaps& maps, glm::vec2 low, glm::vec2 high, float margin);

// A number that changes when any bit of the result changes (FNV-1a over every map).
std::uint64_t HashTerrainMaps(const TerrainMaps& maps);
// A number that names the result of these settings with this version of the generator;
// used to name cache files.
std::uint64_t HashTerrainGeneratorSettings(const TerrainGeneratorSettings& settings);

// Poligon's own file for generated land: a line of text, then every map as raw numbers.
bool SaveTerrainMaps(const char* path, const TerrainMaps& maps);
bool LoadTerrainMaps(const char* path, TerrainMaps& maps, std::string& error);

// Sets one setting by its name in TerrainGeneratorSettings ("seed", "size",
// "erosion_steps"...) from text. False for an unknown name or a bad number.
bool SetTerrainGeneratorSetting(TerrainGeneratorSettings& settings, const std::string& name, const std::string& value);
// Every setting as name and text, in a fixed order; numbers keep all their digits.
std::vector<std::pair<std::string, std::string>> GetTerrainGeneratorSettings(const TerrainGeneratorSettings& settings);

// A place to look at the largest lake from: on gentle ground 150 to 500 m from its middle,
// as high above it as possible (up to 40 m) with the lake's middle in view from eye
// height, at least 300 m from the map's edge.
struct LakeViewpoint {
    glm::vec2 position{0.0f};  // meters on the map
    // Facing the lake, as FreeCamera::yaw_degrees (0 = looking along -z).
    float yaw_degrees = 0.0f;
    // From the place to the lake's middle: unit direction (map x, z) and distance.
    glm::vec2 direction{0.0f, -1.0f};
    float distance = 0.0f;
    int lake = -1;  // index into TerrainMaps::lakes
};
// False (and the map's middle) when the land has no lake or no place fits.
bool FindLakeViewpoint(const TerrainMaps& maps, float eye_height, LakeViewpoint& viewpoint);

// Loads the land for these settings from `cache_directory` if it was generated before;
// otherwise generates it and saves it there (the folder is created when missing).
// `generated` says which happened. Returns false only when the file cannot be written.
bool LoadOrGenerateTerrain(const TerrainGeneratorSettings& settings, const char* cache_directory, TerrainMaps& maps,
                           bool& generated, std::string& error);

} // namespace poligon

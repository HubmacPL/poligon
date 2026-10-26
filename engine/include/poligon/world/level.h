#pragma once

#include <string>
#include <vector>

#include "poligon/core/math.h"
#include "poligon/world/terrain_generator.h"

namespace poligon {

// A level: everything about a place that is data, not code, in one text file in the style
// of the render settings file. Plants placed by rules are described in the same file
// (poligon/world/plant_rules.h).
//
//     poligon-level 1
//
//     # The land: TerrainGeneratorSettings by name.
//     terrain.seed 1
//     terrain.size 6000
//
//     # The look: a render settings file, relative to this one.
//     render_settings forest_render_settings.txt
//
//     # A streamed world (poligon/world/streaming.h): the cooked land's name, and how it is
//     # cut. Without "world" the land is generated and held whole.
//     world worlds/forest
//     world.patch_size 512
//     world.background_spacing 64
//
//     # Where the player starts: "lake" (on a slope above the largest lake, looking at it)
//     # or map x, z in meters and the direction to look in degrees.
//     player_start lake
//
//     # Things put by hand: asset name, map x, z, turn in degrees, scale.
//     object rocks/boulder 3540 5120 30 1.5
//
// Unknown words, wrong numbers of values and bad numbers are errors that name the line.

struct LevelObject {
    std::string asset;
    glm::dvec2 position{0.0};  // meters on the map
    float turn_degrees = 0.0f;
    float scale = 1.0f;
};

struct Level {
    TerrainGeneratorSettings terrain;
    // Relative to the level file; empty = the game's own look.
    std::string render_settings;
    // The cooked streamed world (poligon/world/land.h: CookLevelWorld, LandFromFiles); empty
    // = the land is generated and held whole.
    std::string world;
    double world_patch_size = 512.0;
    float world_background_spacing = 64.0f;
    // true: above the largest lake (FindLakeViewpoint); false: start_position and start_turn.
    bool start_at_lake = true;
    glm::dvec2 start_position{0.0};
    float start_turn_degrees = 0.0f;
    std::vector<LevelObject> objects;
    // Lines this file did not understand by itself, for parts of the engine that read the
    // same file (plant rules): each with its line number.
    std::vector<std::pair<int, std::string>> other_lines;
};

// other_line_prefixes: first words of lines that other readers take (kept in other_lines
// instead of being errors), for example {"plant", "rule"}.
bool LoadLevel(const char* path, Level& level, std::string& error, const std::vector<std::string>& other_line_prefixes = {});
bool ParseLevel(const std::string& text, Level& level, std::string& error, const std::vector<std::string>& other_line_prefixes = {});
// Writes every terrain setting, the start and the objects (other_lines are written back as they were).
bool SaveLevel(const char* path, const Level& level);

} // namespace poligon

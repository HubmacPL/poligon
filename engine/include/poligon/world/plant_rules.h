#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "poligon/world/terrain_generator.h"

namespace poligon {

// Rules that say where plants grow, judged from the generator's maps: how dense, how far
// apart, in which ranges of slope, height, moisture, sediment, rock, flow, distance to
// water and water depth, in clumps or evenly, how large, and whether a plant floats on the
// water (water lilies). The same rules drive both kinds of plants:
//
// - "plant" rules place things once, over a whole area (trees, shrubs, reeds, rocks:
//   PlacePlants, for the Vegetation system);
// - "cover" rules are ground cover that grows on the fly around the player (grass, small
//   plants: GroundCoverLayer::conditions, see poligon/world/ground_cover.h).
//
// In a level file (poligon/world/level.h) a rule is a block of lines:
//
//     plant trees/pine
//     density 0.01            # plants per square meter where every condition holds fully
//     spacing 4               # no two plants of this rule closer than this, meters
//     scale 0.8 1.2
//     slope 0 30 5            # low, high, fade: full weight inside, nothing beyond high + fade
//     moisture 0.2 0.8 0.1
//     water_distance 6 1e9 4
//     patch 120 0.55          # clumps about 120 m across covering 55% of the land
//     area 1200               # within 1200 m of the player's start (0 = the whole map)
//
// Conditions: slope, height, moisture, sediment, rock, flow (log10 of square meters),
// water_distance, water_depth. Other lines: sink (share of the height pushed into the
// ground), align 0|1 (tilt with the slope), on_water 0|1 (float at the water's level),
// seed, every_nth (cover only: use every n-th variant of the kind), group NAME (plant
// rules of one group share one grid, the spacing of the group's first rule: in each cell at
// most one of them grows, so trees of different kinds never stand in each other).

struct PlantRule {
    bool is_cover = false;   // "cover" (ground cover) or "plant" (placed once)
    std::string kind;        // the asset (or other name) the game maps to its plants
    float density = 0.01f;
    float spacing = 3.0f;
    float scale_min = 0.8f;
    float scale_max = 1.2f;
    MapConditions conditions;
    float patch_size = 0.0f;   // 0 = no clumps
    float patch_cover = 0.5f;
    float area = 0.0f;         // 0 = everywhere
    float sink = 0.0f;
    bool align_to_ground = false;
    bool on_water = false;
    int seed = 0;
    int every_nth = 1;
    std::string group;         // empty = a group of its own
    int line = 0;              // where the rule starts in its file
};

// The words that begin rule lines, for LoadLevel's other_line_prefixes.
const std::vector<std::string>& GetPlantRuleWords();

// Reads the rules from the lines a level kept for other readers (Level::other_lines).
bool ParsePlantRules(const std::vector<std::pair<int, std::string>>& lines, std::vector<PlantRule>& rules, std::string& error);

// One place a plant goes.
struct PlantPlacement {
    glm::vec2 position{0.0f};  // meters on the map
    float height = 0.0f;       // ground (or water surface, for on_water rules) there
    float scale = 1.0f;
    float turn = 0.0f;         // radians around the vertical
    int variant = 0;           // 0 .. variant_count - 1
};

// Where a "plant" rule puts its plants within `area` meters of `center` (map meters; the
// rule's own area, if set, limits further). A jittered grid of `spacing`: in each cell one
// candidate place, kept with the probability density x spacing^2 x the conditions' weight
// x the clumps. Each cell decides from its own coordinates and the seed, so the result does
// not depend on the order of anything and is the same on every run. `keep` (optional) can
// refuse places the game wants empty.
std::vector<PlantPlacement> PlacePlants(const TerrainMaps& maps, const PlantRule& rule, glm::vec2 center, float area, int variant_count,
                                        const std::function<bool(glm::vec2)>& keep = nullptr);

// The same for rules that share a grid (PlantRule::group): `rules` all of one group; the
// first rule's spacing and seed make the grid; in each cell one candidate place, which
// goes to at most one rule, chosen with the probabilities each rule alone would have.
// Returned placements carry the index (into `rules`) of the rule that got them.
std::vector<std::pair<int, PlantPlacement>> PlacePlantGroup(const TerrainMaps& maps, const std::vector<const PlantRule*>& rules,
                                                             glm::vec2 center, float area, const std::vector<int>& variant_counts,
                                                             const std::function<bool(int rule, glm::vec2)>& keep = nullptr);

// The clump factor of a rule at a map place: 1 inside a clump, 0 between them, soft at the
// edges. Shared by PlacePlants and the ground cover.
float GetPatchFactor(float patch_size, float patch_cover, std::uint32_t seed, glm::vec2 place);

} // namespace poligon

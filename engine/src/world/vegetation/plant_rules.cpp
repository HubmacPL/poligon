#include "poligon/world/plant_rules.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <sstream>

namespace poligon {

namespace {

std::uint32_t HashCell(std::int32_t x, std::int32_t z, std::uint32_t seed) {
    std::uint32_t h = seed * 0x9E3779B1u ^ (static_cast<std::uint32_t>(x) * 0x85EBCA6Bu) ^ (static_cast<std::uint32_t>(z) * 0xC2B2AE35u);
    h ^= h >> 16;
    h *= 0x7FEB352Du;
    h ^= h >> 15;
    h *= 0x846CA68Bu;
    h ^= h >> 16;
    return h;
}

// Several random numbers in 0..1 from one cell.
float CellRandom(std::uint32_t& state) {
    state = state * 747796405u + 2891336453u;
    std::uint32_t word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    word = (word >> 22u) ^ word;
    return static_cast<float>(word >> 8) / 16777216.0f;
}

float ValueNoise(float x, float z, std::uint32_t seed) {
    const float fx = std::floor(x);
    const float fz = std::floor(z);
    const std::int32_t ix = static_cast<std::int32_t>(fx);
    const std::int32_t iz = static_cast<std::int32_t>(fz);
    float tx = x - fx;
    float tz = z - fz;
    tx = tx * tx * (3.0f - 2.0f * tx);
    tz = tz * tz * (3.0f - 2.0f * tz);
    const auto corner = [&](std::int32_t cx, std::int32_t cz) {
        return static_cast<float>(HashCell(ix + cx, iz + cz, seed) >> 8) / 16777216.0f;
    };
    const float a = corner(0, 0) + (corner(1, 0) - corner(0, 0)) * tx;
    const float b = corner(0, 1) + (corner(1, 1) - corner(0, 1)) * tx;
    return a + (b - a) * tz;
}

bool ReadRange(const std::vector<std::string>& values, TerrainRange& range) {
    if (values.size() != 3) {
        return false;
    }
    char* end = nullptr;
    float numbers[3];
    for (int i = 0; i < 3; ++i) {
        numbers[i] = std::strtof(values[static_cast<size_t>(i)].c_str(), &end);
        if (*end != '\0') {
            return false;
        }
    }
    range = {numbers[0], numbers[1], numbers[2]};
    return numbers[0] <= numbers[1] && numbers[2] >= 0.0f;
}

} // namespace

float GetPatchFactor(float patch_size, float patch_cover, std::uint32_t seed, glm::vec2 place) {
    if (patch_size <= 0.0f) {
        return 1.0f;
    }
    const float noise = ValueNoise(place.x / patch_size, place.y / patch_size, seed) * 0.65f +
                        ValueNoise(place.x / patch_size * 2.03f + 17.0f, place.y / patch_size * 2.03f + 31.0f, seed + 101u) * 0.35f;
    const float edge = 1.0f - patch_cover;
    const float t = std::clamp((noise - (edge - 0.08f)) / 0.16f, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

const std::vector<std::string>& GetPlantRuleWords() {
    static const std::vector<std::string> words = {"plant", "cover", "density", "spacing", "scale", "slope", "height", "moisture",
                                                   "sediment", "rock", "flow", "water_distance", "water_depth", "patch", "area",
                                                   "sink", "align", "on_water", "seed", "every_nth", "group"};
    return words;
}

bool ParsePlantRules(const std::vector<std::pair<int, std::string>>& lines, std::vector<PlantRule>& rules, std::string& error) {
    std::vector<PlantRule> parsed;
    for (const auto& [line_number, line] : lines) {
        std::istringstream words(line);
        std::string key;
        words >> key;
        std::vector<std::string> values;
        for (std::string word; words >> word;) {
            values.push_back(word);
        }
        const auto fail = [&, line_number = line_number](const std::string& message) {
            error = "line " + std::to_string(line_number) + ": " + message;
            return false;
        };
        if (key == "plant" || key == "cover") {
            if (values.size() != 1) {
                return fail("\"" + key + "\" takes the name of a kind");
            }
            PlantRule rule;
            rule.is_cover = key == "cover";
            rule.kind = values[0];
            rule.line = line_number;
            rule.seed = static_cast<int>(parsed.size()) * 7 + 1;
            parsed.push_back(rule);
            continue;
        }
        if (parsed.empty()) {
            return fail("\"" + key + "\" before any plant or cover rule");
        }
        PlantRule& rule = parsed.back();
        MapConditions& c = rule.conditions;
        TerrainRange* range = key == "slope"            ? &c.slope_degrees
                              : key == "height"         ? &c.height
                              : key == "moisture"       ? &c.moisture
                              : key == "sediment"       ? &c.sediment
                              : key == "rock"           ? &c.rock
                              : key == "flow"           ? &c.flow
                              : key == "water_distance" ? &c.water_distance
                              : key == "water_depth"    ? &c.water_depth
                                                        : nullptr;
        char* end = nullptr;
        const auto number = [&](size_t index, float& out) {
            out = std::strtof(values[index].c_str(), &end);
            return *end == '\0';
        };
        if (range != nullptr) {
            if (!ReadRange(values, *range)) {
                return fail("\"" + key + "\" takes low, high and fade (low not above high)");
            }
        } else if (key == "density" || key == "spacing" || key == "area" || key == "sink") {
            float value = 0.0f;
            if (values.size() != 1 || !number(0, value) || value < 0.0f) {
                return fail("\"" + key + "\" takes one number, not negative");
            }
            (key == "density" ? rule.density : key == "spacing" ? rule.spacing : key == "area" ? rule.area : rule.sink) = value;
            if (key == "spacing" && value < 0.05f) {
                return fail("\"spacing\" must be at least 0.05 m");
            }
        } else if (key == "scale") {
            if (values.size() != 2 || !number(0, rule.scale_min) || !number(1, rule.scale_max) || rule.scale_min <= 0.0f ||
                rule.scale_max < rule.scale_min) {
                return fail("\"scale\" takes the smallest and largest scale");
            }
        } else if (key == "patch") {
            if (values.size() != 2 || !number(0, rule.patch_size) || !number(1, rule.patch_cover) || rule.patch_cover < 0.0f ||
                rule.patch_cover > 1.0f) {
                return fail("\"patch\" takes a size in meters and the share of land covered (0 to 1)");
            }
        } else if (key == "align" || key == "on_water") {
            if (values.size() != 1 || (values[0] != "0" && values[0] != "1")) {
                return fail("\"" + key + "\" takes 0 or 1");
            }
            (key == "align" ? rule.align_to_ground : rule.on_water) = values[0] == "1";
        } else if (key == "group") {
            if (values.size() != 1) {
                return fail("\"group\" takes one name");
            }
            rule.group = values[0];
        } else if (key == "seed" || key == "every_nth") {
            const long value = values.size() == 1 ? std::strtol(values[0].c_str(), &end, 10) : -1;
            if (values.size() != 1 || *end != '\0' || value < (key == "seed" ? 0 : 1)) {
                return fail("\"" + key + "\" takes one whole number");
            }
            (key == "seed" ? rule.seed : rule.every_nth) = static_cast<int>(value);
        } else {
            return fail("unknown word \"" + key + "\" in a plant rule");
        }
    }
    rules = std::move(parsed);
    return true;
}

std::vector<std::pair<int, PlantPlacement>> PlacePlantGroup(const TerrainMaps& maps, const std::vector<const PlantRule*>& rules,
                                                             glm::vec2 center, float area, const std::vector<int>& variant_counts,
                                                             const std::function<bool(int rule, glm::vec2)>& keep) {
    std::vector<std::pair<int, PlantPlacement>> placements;
    if (rules.empty() || rules.size() != variant_counts.size() || maps.height.IsEmpty()) {
        return placements;
    }
    const PlantRule& first = *rules.front();
    float radius = 0.0f;
    for (const PlantRule* rule : rules) {
        radius = std::max(radius, rule->area > 0.0f ? std::min(area, rule->area) : area);
    }
    const float spacing = std::max(first.spacing, 0.05f);
    const float size_x = maps.height.GetSizeX();
    const float size_z = maps.height.GetSizeZ();
    const std::int32_t first_x = static_cast<std::int32_t>(std::floor(std::max(center.x - radius, 0.0f) / spacing));
    const std::int32_t last_x = static_cast<std::int32_t>(std::floor(std::min(center.x + radius, size_x) / spacing));
    const std::int32_t first_z = static_cast<std::int32_t>(std::floor(std::max(center.y - radius, 0.0f) / spacing));
    const std::int32_t last_z = static_cast<std::int32_t>(std::floor(std::min(center.y + radius, size_z) / spacing));
    const std::uint32_t seed = static_cast<std::uint32_t>(first.seed) * 2654435761u + 12345u;
    std::vector<float> chances(rules.size());
    for (std::int32_t cz = first_z; cz <= last_z; ++cz) {
        for (std::int32_t cx = first_x; cx <= last_x; ++cx) {
            std::uint32_t state = HashCell(cx, cz, seed);
            const glm::vec2 place((static_cast<float>(cx) + 0.1f + 0.8f * CellRandom(state)) * spacing,
                                  (static_cast<float>(cz) + 0.1f + 0.8f * CellRandom(state)) * spacing);
            const float pick = CellRandom(state);
            const float size_pick = CellRandom(state);
            const float turn = CellRandom(state) * 6.2831853f;
            const float variant_pick = CellRandom(state);
            if (place.x >= size_x || place.y >= size_z) {
                continue;
            }
            // Each rule's chance here; the pick falls into one of them or into none.
            float total = 0.0f;
            for (size_t r = 0; r < rules.size(); ++r) {
                const PlantRule& rule = *rules[r];
                const float rule_radius = rule.area > 0.0f ? std::min(area, rule.area) : area;
                float chance = 0.0f;
                if (rule.density > 0.0f && glm::length(place - center) <= rule_radius) {
                    chance = std::min(rule.density * spacing * spacing, 1.0f);
                    chance *= rule.conditions.Weight(maps, place);
                    if (chance > 0.0f) {
                        chance *= GetPatchFactor(rule.patch_size, rule.patch_cover,
                                                 static_cast<std::uint32_t>(rule.seed) * 2654435761u + 12345u, place);
                    }
                }
                chances[r] = chance;
                total += chance;
            }
            if (total <= 0.0f) {
                continue;
            }
            const float scale_down = total > 1.0f ? 1.0f / total : 1.0f;
            float reached = 0.0f;
            int chosen = -1;
            for (size_t r = 0; r < rules.size(); ++r) {
                reached += chances[r] * scale_down;
                if (pick < reached) {
                    chosen = static_cast<int>(r);
                    break;
                }
            }
            if (chosen < 0 || (keep && !keep(chosen, place))) {
                continue;
            }
            const PlantRule& rule = *rules[static_cast<size_t>(chosen)];
            PlantPlacement placement;
            placement.position = place;
            placement.height = maps.height.GetHeight(place.x, place.y);
            if (rule.on_water) {
                placement.height += maps.GetWaterDepth(place.x, place.y);
            }
            placement.scale = rule.scale_min + (rule.scale_max - rule.scale_min) * size_pick;
            placement.turn = turn;
            const int variant_count = std::max(variant_counts[static_cast<size_t>(chosen)], 1);
            placement.variant = std::min(static_cast<int>(variant_pick * static_cast<float>(variant_count)), variant_count - 1);
            placements.emplace_back(chosen, placement);
        }
    }
    return placements;
}

std::vector<PlantPlacement> PlacePlants(const TerrainMaps& maps, const PlantRule& rule, glm::vec2 center, float area, int variant_count,
                                        const std::function<bool(glm::vec2)>& keep) {
    std::vector<PlantPlacement> placements;
    if (variant_count < 1 || rule.density <= 0.0f || maps.height.IsEmpty()) {
        return placements;
    }
    const float radius = rule.area > 0.0f ? std::min(area, rule.area) : area;
    const float spacing = std::max(rule.spacing, 0.05f);
    const float chance_per_cell = std::min(rule.density * spacing * spacing, 1.0f);
    const float size_x = maps.height.GetSizeX();
    const float size_z = maps.height.GetSizeZ();
    const std::int32_t first_x = static_cast<std::int32_t>(std::floor(std::max(center.x - radius, 0.0f) / spacing));
    const std::int32_t last_x = static_cast<std::int32_t>(std::floor(std::min(center.x + radius, size_x) / spacing));
    const std::int32_t first_z = static_cast<std::int32_t>(std::floor(std::max(center.y - radius, 0.0f) / spacing));
    const std::int32_t last_z = static_cast<std::int32_t>(std::floor(std::min(center.y + radius, size_z) / spacing));
    const std::uint32_t seed = static_cast<std::uint32_t>(rule.seed) * 2654435761u + 12345u;
    for (std::int32_t cz = first_z; cz <= last_z; ++cz) {
        for (std::int32_t cx = first_x; cx <= last_x; ++cx) {
            std::uint32_t state = HashCell(cx, cz, seed);
            const glm::vec2 place((static_cast<float>(cx) + 0.1f + 0.8f * CellRandom(state)) * spacing,
                                  (static_cast<float>(cz) + 0.1f + 0.8f * CellRandom(state)) * spacing);
            const float pick = CellRandom(state);
            const float scale = rule.scale_min + (rule.scale_max - rule.scale_min) * CellRandom(state);
            const float turn = CellRandom(state) * 6.2831853f;
            const float variant_pick = CellRandom(state);
            if (place.x >= size_x || place.y >= size_z || glm::length(place - center) > radius) {
                continue;
            }
            if (pick >= chance_per_cell) {
                continue;
            }
            const float weight = rule.conditions.Weight(maps, place) * GetPatchFactor(rule.patch_size, rule.patch_cover, seed, place);
            if (pick >= chance_per_cell * weight || (keep && !keep(place))) {
                continue;
            }
            PlantPlacement placement;
            placement.position = place;
            placement.height = maps.height.GetHeight(place.x, place.y);
            if (rule.on_water) {
                placement.height += maps.GetWaterDepth(place.x, place.y);
            }
            placement.scale = scale;
            placement.turn = turn;
            placement.variant = std::min(static_cast<int>(variant_pick * static_cast<float>(variant_count)), variant_count - 1);
            placements.push_back(placement);
        }
    }
    return placements;
}

} // namespace poligon

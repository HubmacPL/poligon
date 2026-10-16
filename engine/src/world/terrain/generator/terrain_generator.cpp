#include "poligon/world/terrain_generator.h"

#include "generator_internal.h"

namespace poligon {

namespace terrain_generator_internal {

void ValidateSettings(const TerrainGeneratorSettings& s) {
    const auto multiple = [&](float value) {
        const double ratio = value / s.spacing;
        return value >= s.spacing && std::fabs(ratio - std::round(ratio)) < 1e-4;
    };
    if (s.spacing <= 0.0f || s.size < 64.0f * s.coarse_spacing || !multiple(s.coarse_spacing) || !multiple(s.map_spacing)) {
        FatalError("GenerateTerrain: needs spacing > 0, coarse and map spacing as whole multiples of it, and a size of at least 64 coarse steps");
    }
    const auto divides = [&](float value) {
        const double steps = s.size / value;
        return std::fabs(steps - std::round(steps)) < 1e-4;
    };
    if (!divides(s.spacing) || !divides(s.coarse_spacing) || !divides(s.map_spacing)) {
        FatalError("GenerateTerrain: the size must be a whole number of every spacing");
    }
    if ((s.outlet_edges & 15) == 0) {
        FatalError("GenerateTerrain: at least one outlet edge is needed, or rivers would have nowhere to go");
    }
    if (s.erosion_steps < 0 || s.thermal_iterations < 0 || s.relief <= 0.0f) {
        FatalError("GenerateTerrain: step counts must not be negative and the relief must be above zero");
    }
}

} // namespace terrain_generator_internal

using namespace terrain_generator_internal;


float TerrainMaps::Sample(const std::vector<float>& map, float x, float z) const {
    if (map.empty() || map_width < 2 || map_depth < 2) {
        return 0.0f;
    }
    const float gx = glm::clamp(x / map_spacing, 0.0f, static_cast<float>(map_width - 1));
    const float gz = glm::clamp(z / map_spacing, 0.0f, static_cast<float>(map_depth - 1));
    const int ix = glm::min(static_cast<int>(gx), map_width - 2);
    const int iz = glm::min(static_cast<int>(gz), map_depth - 2);
    const float fx = gx - static_cast<float>(ix);
    const float fz = gz - static_cast<float>(iz);
    const float* row = &map[static_cast<size_t>(iz) * static_cast<size_t>(map_width) + static_cast<size_t>(ix)];
    const float* next_row = row + map_width;
    return glm::mix(glm::mix(row[0], row[1], fx), glm::mix(next_row[0], next_row[1], fx), fz);
}

float TerrainMaps::GetWaterDepth(float x, float z) const {
    if (water_level.empty()) {
        return 0.0f;
    }
    const int ix = glm::clamp(static_cast<int>(std::lround(x / map_spacing)), 0, map_width - 1);
    const int iz = glm::clamp(static_cast<int>(std::lround(z / map_spacing)), 0, map_depth - 1);
    const float level = water_level[static_cast<size_t>(iz) * static_cast<size_t>(map_width) + static_cast<size_t>(ix)];
    if (level == kNoWater) {
        return 0.0f;
    }
    return glm::max(level - height.GetHeight(x, z), 0.0f);
}

void GenerateTerrain(const TerrainGeneratorSettings& settings, TerrainMaps& maps) {
    ValidateSettings(settings);
    CoarseResult coarse;
    SimulateLargeScale(settings, coarse);

    maps = TerrainMaps{};
    maps.map_spacing = settings.map_spacing;
    maps.map_width = static_cast<int>(std::lround(settings.size / settings.map_spacing)) + 1;
    maps.map_depth = maps.map_width;
    FineState fine;
    fine.deposited.assign(static_cast<size_t>(maps.map_width) * static_cast<size_t>(maps.map_depth), 0.0f);
    fine.eroded.assign(fine.deposited.size(), 0.0f);
    BuildFineLand(settings, coarse, fine);
    ThermalErosion(settings, maps.map_width, settings.thermal_iterations, fine);
    DropletErosion(settings, maps.map_width, fine);
    // A few more slides settle the sharp edges raindrops leave.
    ThermalErosion(settings, maps.map_width, std::min(settings.thermal_iterations, 4), fine);
    BuildMaps(settings, coarse, fine, maps);

    maps.height.width = fine.width;
    maps.height.depth = fine.width;
    maps.height.spacing = settings.spacing;
    maps.height.heights = std::move(fine.heights);
}

std::uint64_t HashTerrainMaps(const TerrainMaps& maps) {
    std::uint64_t hash = 0xCBF29CE484222325ull;
    const int sizes[4] = {maps.height.width, maps.height.depth, maps.map_width, maps.map_depth};
    HashBytes(hash, sizes, 4);
    HashBytes(hash, &maps.height.spacing, 1);
    HashBytes(hash, &maps.map_spacing, 1);
    HashBytes(hash, maps.height.heights.data(), maps.height.heights.size());
    for (const std::vector<float>* map :
         {&maps.water_level, &maps.flow, &maps.moisture, &maps.sediment, &maps.rock, &maps.water_distance}) {
        HashBytes(hash, map->data(), map->size());
    }
    for (const Lake& lake : maps.lakes) {
        const float values[5] = {lake.center.x, lake.center.y, lake.level, lake.area, lake.max_depth};
        HashBytes(hash, values, 5);
    }
    return hash;
}

std::uint64_t HashTerrainGeneratorSettings(const TerrainGeneratorSettings& s) {
    std::uint64_t hash = 0xCBF29CE484222325ull;
    HashBytes(hash, &kGeneratorVersion, 1);
    HashBytes(hash, &s.seed, 1);
    const float floats[] = {s.size, s.spacing, s.coarse_spacing, s.map_spacing, s.relief, s.initial_relief,
                            s.initial_hill_size, s.years_per_step, s.uplift,
                            s.erodibility, s.area_exponent, s.hillslope_diffusion, s.basin_radius, s.basin_start,
                            s.basin_sinking, s.detail_height,
                            s.talus_degrees, s.droplets_per_square_meter, s.droplet_strength, s.min_lake_area,
                            s.min_lake_depth};
    HashBytes(hash, floats, sizeof(floats) / sizeof(floats[0]));
    const int ints[] = {s.erosion_steps, s.basin_count, s.outlet_edges, s.thermal_iterations, s.droplet_steps};
    HashBytes(hash, ints, sizeof(ints) / sizeof(ints[0]));
    return hash;
}

namespace {

struct SettingField {
    const char* name;
    float* real = nullptr;
    int* whole = nullptr;
};

std::vector<SettingField> SettingFields(TerrainGeneratorSettings& s) {
    return {{"size", &s.size},
            {"spacing", &s.spacing},
            {"coarse_spacing", &s.coarse_spacing},
            {"map_spacing", &s.map_spacing},
            {"relief", &s.relief},
            {"initial_relief", &s.initial_relief},
            {"initial_hill_size", &s.initial_hill_size},
            {"erosion_steps", nullptr, &s.erosion_steps},
            {"years_per_step", &s.years_per_step},
            {"uplift", &s.uplift},
            {"erodibility", &s.erodibility},
            {"area_exponent", &s.area_exponent},
            {"hillslope_diffusion", &s.hillslope_diffusion},
            {"basin_count", nullptr, &s.basin_count},
            {"basin_radius", &s.basin_radius},
            {"basin_start", &s.basin_start},
            {"basin_sinking", &s.basin_sinking},
            {"outlet_edges", nullptr, &s.outlet_edges},
            {"detail_height", &s.detail_height},
            {"talus_degrees", &s.talus_degrees},
            {"thermal_iterations", nullptr, &s.thermal_iterations},
            {"droplets_per_square_meter", &s.droplets_per_square_meter},
            {"droplet_strength", &s.droplet_strength},
            {"droplet_steps", nullptr, &s.droplet_steps},
            {"min_lake_area", &s.min_lake_area},
            {"min_lake_depth", &s.min_lake_depth}};
}

} // namespace

namespace {

float RangeWeight(const TerrainRange& range, float value) {
    if (value < range.low) {
        const float t = std::clamp(1.0f - (range.low - value) / std::max(range.fade, 1e-6f), 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }
    if (value > range.high) {
        const float t = std::clamp(1.0f - (value - range.high) / std::max(range.fade, 1e-6f), 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }
    return 1.0f;
}

bool IsSet(const TerrainRange& range) {
    return range.low > -1.0e29f || range.high < 1.0e29f;
}

} // namespace

float MapConditions::Weight(const TerrainMaps& maps, glm::vec2 place) const {
    float weight = 1.0f;
    if (IsSet(slope_degrees)) {
        weight *= RangeWeight(slope_degrees, maps.height.GetSlopeDegrees(place.x, place.y));
    }
    if (weight > 0.0f && IsSet(height)) {
        weight *= RangeWeight(height, maps.height.GetHeight(place.x, place.y));
    }
    if (weight > 0.0f && IsSet(moisture)) {
        weight *= RangeWeight(moisture, maps.Sample(maps.moisture, place.x, place.y));
    }
    if (weight > 0.0f && IsSet(sediment)) {
        weight *= RangeWeight(sediment, maps.Sample(maps.sediment, place.x, place.y));
    }
    if (weight > 0.0f && IsSet(rock)) {
        weight *= RangeWeight(rock, maps.Sample(maps.rock, place.x, place.y));
    }
    if (weight > 0.0f && IsSet(flow)) {
        weight *= RangeWeight(flow, std::log10(std::max(maps.Sample(maps.flow, place.x, place.y), 1.0f)));
    }
    if (weight > 0.0f && IsSet(water_distance)) {
        weight *= RangeWeight(water_distance, maps.Sample(maps.water_distance, place.x, place.y));
    }
    if (weight > 0.0f && IsSet(water_depth)) {
        weight *= RangeWeight(water_depth, maps.GetWaterDepth(place.x, place.y));
    }
    return weight;
}

bool SetTerrainGeneratorSetting(TerrainGeneratorSettings& settings, const std::string& name, const std::string& value) {
    if (value.empty()) {
        return false;
    }
    char* end = nullptr;
    if (name == "seed") {
        const unsigned long long seed = std::strtoull(value.c_str(), &end, 10);
        if (*end != '\0' || value[0] == '-') {
            return false;
        }
        settings.seed = seed;
        return true;
    }
    for (const SettingField& field : SettingFields(settings)) {
        if (name != field.name) {
            continue;
        }
        if (field.real != nullptr) {
            const float number = std::strtof(value.c_str(), &end);
            if (*end != '\0' || !std::isfinite(number)) {
                return false;
            }
            *field.real = number;
        } else {
            const long number = std::strtol(value.c_str(), &end, 10);
            if (*end != '\0') {
                return false;
            }
            *field.whole = static_cast<int>(number);
        }
        return true;
    }
    return false;
}

std::vector<std::pair<std::string, std::string>> GetTerrainGeneratorSettings(const TerrainGeneratorSettings& settings) {
    TerrainGeneratorSettings copy = settings;
    std::vector<std::pair<std::string, std::string>> result;
    result.emplace_back("seed", std::to_string(copy.seed));
    for (const SettingField& field : SettingFields(copy)) {
        char text[64];
        if (field.real != nullptr) {
            std::snprintf(text, sizeof(text), "%.9g", static_cast<double>(*field.real));
        } else {
            std::snprintf(text, sizeof(text), "%d", *field.whole);
        }
        result.emplace_back(field.name, text);
    }
    return result;
}

bool FindLakeViewpoint(const TerrainMaps& maps, float eye_height, LakeViewpoint& viewpoint) {
    const HeightMap& map = maps.height;
    viewpoint = LakeViewpoint{};
    viewpoint.position = glm::vec2(map.GetSizeX() * 0.5f, map.GetSizeZ() * 0.5f);
    for (size_t i = 0; i < maps.lakes.size(); ++i) {
        if (viewpoint.lake < 0 || maps.lakes[i].area > maps.lakes[static_cast<size_t>(viewpoint.lake)].area) {
            viewpoint.lake = static_cast<int>(i);
        }
    }
    if (viewpoint.lake < 0) {
        return false;
    }
    const Lake& lake = maps.lakes[static_cast<size_t>(viewpoint.lake)];
    float best_score = -1.0e30f;
    bool found = false;
    const float margin = 300.0f;
    for (float radius = 150.0f; radius <= 500.0f; radius += 50.0f) {
        for (int step = 0; step < 72; ++step) {
            const float angle = static_cast<float>(step) * 0.0872665f;
            const glm::vec2 place = lake.center + radius * glm::vec2(std::cos(angle), std::sin(angle));
            if (place.x < margin || place.y < margin || place.x > map.GetSizeX() - margin || place.y > map.GetSizeZ() - margin) {
                continue;
            }
            if (maps.Sample(maps.water_distance, place.x, place.y) < 30.0f || map.GetSlopeDegrees(place.x, place.y) > 12.0f) {
                continue;
            }
            const float above = map.GetHeight(place.x, place.y) - lake.level;
            if (above < 6.0f) {
                continue;
            }
            // The line from the eye to the lake's middle must stay above the ground.
            const glm::vec3 eye(place.x, map.GetHeight(place.x, place.y) + eye_height, place.y);
            const glm::vec3 target(lake.center.x, lake.level, lake.center.y);
            bool seen = true;
            for (int sample = 1; sample < 64 && seen; ++sample) {
                const glm::vec3 point = glm::mix(eye, target, static_cast<float>(sample) / 64.0f);
                seen = map.GetHeight(point.x, point.z) < point.y - 0.3f;
            }
            if (!seen) {
                continue;
            }
            const float score = std::min(above, 40.0f) - radius * 0.02f;
            if (score > best_score) {
                best_score = score;
                viewpoint.position = place;
                found = true;
            }
        }
    }
    if (!found) {
        return false;
    }
    viewpoint.direction = glm::normalize(lake.center - viewpoint.position);
    viewpoint.distance = glm::length(lake.center - viewpoint.position);
    viewpoint.yaw_degrees = glm::degrees(std::atan2(-viewpoint.direction.x, -viewpoint.direction.y));
    return true;
}

bool SaveTerrainMaps(const char* path, const TerrainMaps& maps) {
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    char header[256];
    std::snprintf(header, sizeof(header), "poligon-terrain-maps 1 %d %d %.9g %d %d %.9g %d\n", maps.height.width,
                  maps.height.depth, static_cast<double>(maps.height.spacing), maps.map_width, maps.map_depth,
                  static_cast<double>(maps.map_spacing), static_cast<int>(maps.lakes.size()));
    file << header;
    const auto write = [&file](const std::vector<float>& values) {
        file.write(reinterpret_cast<const char*>(values.data()), static_cast<std::streamsize>(values.size() * sizeof(float)));
    };
    write(maps.height.heights);
    for (const std::vector<float>* map :
         {&maps.water_level, &maps.flow, &maps.moisture, &maps.sediment, &maps.rock, &maps.water_distance}) {
        write(*map);
    }
    for (const Lake& lake : maps.lakes) {
        const float values[5] = {lake.center.x, lake.center.y, lake.level, lake.area, lake.max_depth};
        file.write(reinterpret_cast<const char*>(values), sizeof(values));
    }
    return static_cast<bool>(file);
}

bool LoadTerrainMaps(const char* path, TerrainMaps& maps, std::string& error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = std::string("cannot open ") + path;
        return false;
    }
    std::string line;
    std::getline(file, line);
    TerrainMaps loaded;
    int version = 0;
    int lake_count = 0;
    double spacing = 0.0;
    double map_spacing = 0.0;
    std::istringstream header(line);
    std::string magic;
    header >> magic >> version >> loaded.height.width >> loaded.height.depth >> spacing >> loaded.map_width >>
        loaded.map_depth >> map_spacing >> lake_count;
    if (!header || magic != "poligon-terrain-maps" || version != 1 || loaded.height.width < 2 || loaded.height.depth < 2 || loaded.map_width < 2 ||
        loaded.map_depth < 2 || lake_count < 0) {
        error = std::string(path) + ": not a poligon terrain maps file";
        return false;
    }
    loaded.height.spacing = static_cast<float>(spacing);
    loaded.map_spacing = static_cast<float>(map_spacing);
    const auto read = [&file](std::vector<float>& values, size_t count) {
        values.resize(count);
        file.read(reinterpret_cast<char*>(values.data()), static_cast<std::streamsize>(count * sizeof(float)));
    };
    read(loaded.height.heights, static_cast<size_t>(loaded.height.width) * static_cast<size_t>(loaded.height.depth));
    const size_t map_count = static_cast<size_t>(loaded.map_width) * static_cast<size_t>(loaded.map_depth);
    for (std::vector<float>* map : {&loaded.water_level, &loaded.flow, &loaded.moisture, &loaded.sediment, &loaded.rock,
                                    &loaded.water_distance}) {
        read(*map, map_count);
    }
    for (int i = 0; i < lake_count; ++i) {
        float values[5] = {};
        file.read(reinterpret_cast<char*>(values), sizeof(values));
        Lake lake;
        lake.center = glm::vec2(values[0], values[1]);
        lake.level = values[2];
        lake.area = values[3];
        lake.max_depth = values[4];
        loaded.lakes.push_back(lake);
    }
    if (!file) {
        error = std::string(path) + ": the file is shorter than its header says";
        return false;
    }
    maps = std::move(loaded);
    return true;
}

bool LoadOrGenerateTerrain(const TerrainGeneratorSettings& settings, const char* cache_directory, TerrainMaps& maps,
                           bool& generated, std::string& error) {
    char name[64];
    std::snprintf(name, sizeof(name), "terrain_%016llx.bin",
                  static_cast<unsigned long long>(HashTerrainGeneratorSettings(settings)));
    const std::filesystem::path path = std::filesystem::path(cache_directory) / name;
    std::string load_error;
    if (std::filesystem::exists(path) && LoadTerrainMaps(path.string().c_str(), maps, load_error)) {
        generated = false;
        return true;
    }
    GenerateTerrain(settings, maps);
    generated = true;
    std::error_code ignored;
    std::filesystem::create_directories(cache_directory, ignored);
    if (!SaveTerrainMaps(path.string().c_str(), maps)) {
        error = "cannot write " + path.string();
        return false;
    }
    return true;
}

} // namespace poligon

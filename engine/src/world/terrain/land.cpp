#include "poligon/world/land.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <list>
#include <string>
#include <utility>

#include "poligon/core/binary_io.h"
#include "poligon/core/fatal_error.h"
#include "poligon/core/worker.h"

namespace poligon {

HeightMap MakeCoarseHeightMap(const HeightMap& map, int step) {
    if (map.IsEmpty() || step < 1) {
        FatalError("MakeCoarseHeightMap: needs a map and a step of at least 1");
    }
    HeightMap coarse;
    coarse.spacing = map.spacing * static_cast<float>(step);
    // The last point lies at or beyond the map's far edge, where heights are those of the edge.
    coarse.width = (map.width - 2) / step + 2;
    coarse.depth = (map.depth - 2) / step + 2;
    coarse.heights.resize(static_cast<size_t>(coarse.width) * static_cast<size_t>(coarse.depth));
    for (int z = 0; z < coarse.depth; ++z) {
        for (int x = 0; x < coarse.width; ++x) {
            const int source_x = std::min(x * step, map.width - 1);
            const int source_z = std::min(z * step, map.depth - 1);
            coarse.heights[static_cast<size_t>(z) * static_cast<size_t>(coarse.width) + static_cast<size_t>(x)] =
                map.heights[static_cast<size_t>(source_z) * static_cast<size_t>(map.width) + static_cast<size_t>(source_x)];
        }
    }
    return coarse;
}

HeightMap MakeOcclusionFloor(const HeightMap& map, int step) {
    HeightMap floor = MakeCoarseHeightMap(map, step);
    // Lowest over each row's runs first, then over the columns of those: separable.
    std::vector<float> rows(static_cast<size_t>(map.depth) * static_cast<size_t>(floor.width));
    for (int z = 0; z < map.depth; ++z) {
        for (int x = 0; x < floor.width; ++x) {
            const int from = std::max(x * step - step, 0);
            const int to = std::min(x * step + step, map.width - 1);
            float lowest = map.heights[static_cast<size_t>(z) * static_cast<size_t>(map.width) + static_cast<size_t>(from)];
            for (int i = from + 1; i <= to; ++i) {
                lowest = std::min(lowest, map.heights[static_cast<size_t>(z) * static_cast<size_t>(map.width) + static_cast<size_t>(i)]);
            }
            rows[static_cast<size_t>(z) * static_cast<size_t>(floor.width) + static_cast<size_t>(x)] = lowest;
        }
    }
    for (int z = 0; z < floor.depth; ++z) {
        const int from = std::max(z * step - step, 0);
        const int to = std::min(z * step + step, map.depth - 1);
        for (int x = 0; x < floor.width; ++x) {
            float lowest = rows[static_cast<size_t>(from) * static_cast<size_t>(floor.width) + static_cast<size_t>(x)];
            for (int i = from + 1; i <= to; ++i) {
                lowest = std::min(lowest, rows[static_cast<size_t>(i) * static_cast<size_t>(floor.width) + static_cast<size_t>(x)]);
            }
            floor.heights[static_cast<size_t>(z) * static_cast<size_t>(floor.width) + static_cast<size_t>(x)] = lowest;
        }
    }
    return floor;
}

namespace {

// The patches a land source keeps, newest or last used first; shared by the threads that ask.
struct KeptPatches {
    int capacity = 32;
    mutable Mutex mutex;
    mutable std::list<std::shared_ptr<const LandPatch>> patches;

    // The patch if it is kept (it moves to the front: patches in use stay, the oldest unused go first).
    std::shared_ptr<const LandPatch> Find(PatchCoord patch) const {
        MutexLock lock(mutex);
        for (auto it = patches.begin(); it != patches.end(); ++it) {
            if ((*it)->patch == patch) {
                patches.splice(patches.begin(), patches, it);
                return patches.front();
            }
        }
        return nullptr;
    }

    // Keeps a patch made outside the lock; if another thread kept the same patch meanwhile,
    // that one is returned (both are equal).
    std::shared_ptr<const LandPatch> Keep(std::shared_ptr<const LandPatch> made) {
        MutexLock lock(mutex);
        for (const std::shared_ptr<const LandPatch>& kept : patches) {
            if (kept->patch == made->patch) {
                return kept;
            }
        }
        patches.push_front(made);
        while (static_cast<int>(patches.size()) > capacity) {
            patches.pop_back();
        }
        return made;
    }
};

} // namespace

struct LandFromMaps::Impl {
    const TerrainMaps* maps = nullptr;
    std::vector<TerrainLayerRule> rules;
    std::uint32_t weight_seed = 0;
    LandInfo info;
    KeptPatches kept;
};

LandFromMaps::LandFromMaps(const TerrainMaps& maps, double patch_size, const glm::dvec3& world_origin, const TerrainLayerRule* rules,
                           int rule_count, std::uint32_t weight_seed, float background_spacing, int kept_patches)
    : impl_(std::make_unique<Impl>()) {
    const HeightMap& height = maps.height;
    const double map_points = patch_size / static_cast<double>(maps.map_spacing);
    if (height.IsEmpty() || patch_size <= 0.0 || map_points != std::floor(map_points) ||
        patch_size / static_cast<double>(height.spacing) != std::floor(patch_size / static_cast<double>(height.spacing)) ||
        rule_count < 0 || rule_count > kMaxTerrainWeightLayers || (rule_count > 0 && rules == nullptr)) {
        FatalError("LandFromMaps: needs maps, a patch size that is a whole number of height and map points, and 0 to 8 rules");
    }
    const int step = static_cast<int>(std::lround(background_spacing / height.spacing));
    if (step < 1 || std::fabs(static_cast<float>(step) * height.spacing - background_spacing) > 1e-3f) {
        FatalError("LandFromMaps: the background spacing must be a whole multiple of the height spacing");
    }
    Impl& impl = *impl_;
    impl.maps = &maps;
    impl.rules.assign(rules, rules + rule_count);
    impl.weight_seed = weight_seed;
    impl.kept.capacity = std::max(kept_patches, 1);
    LandInfo& info = impl.info;
    info.grid.patch_size = patch_size;
    info.grid.origin = {0.0, 0.0};
    info.size_x = height.GetSizeX();
    info.size_z = height.GetSizeZ();
    info.grid.patch_count_x = static_cast<int>(std::ceil(static_cast<double>(info.size_x) / patch_size));
    info.grid.patch_count_z = static_cast<int>(std::ceil(static_cast<double>(info.size_z) / patch_size));
    info.world_origin = world_origin;
    info.spacing = height.spacing;
    info.map_spacing = maps.map_spacing;
    info.lakes = maps.lakes;
    info.background = MakeCoarseHeightMap(height, step);
    info.occlusion_floor = MakeOcclusionFloor(height, step);
    info.has_lake_viewpoint = FindLakeViewpoint(maps, kLandViewpointEyeHeight, info.lake_viewpoint);
    if (rule_count > 0) {
        // The whole land's weights, at the background's points (the last ones repeat the edge).
        Image whole[2];
        MakeTerrainLayerWeights(maps, rules, rule_count, weight_seed, whole);
        const int map_step = static_cast<int>(std::lround(background_spacing / maps.map_spacing));
        for (int i = 0; i < 2; ++i) {
            Image& image = info.background_weights[i];
            image = MakeSolidImage(info.background.width, info.background.depth, 0, 0, 0, 0);
            for (int z = 0; z < image.height; ++z) {
                const int from_z = std::min(z * map_step, whole[i].height - 1);
                for (int x = 0; x < image.width; ++x) {
                    const int from_x = std::min(x * map_step, whole[i].width - 1);
                    const size_t from = (static_cast<size_t>(from_z) * static_cast<size_t>(whole[i].width) + static_cast<size_t>(from_x)) * 4;
                    const size_t to = (static_cast<size_t>(z) * static_cast<size_t>(image.width) + static_cast<size_t>(x)) * 4;
                    std::copy(whole[i].pixels.begin() + static_cast<std::ptrdiff_t>(from),
                              whole[i].pixels.begin() + static_cast<std::ptrdiff_t>(from + 4),
                              image.pixels.begin() + static_cast<std::ptrdiff_t>(to));
                }
            }
        }
    }
}

LandFromMaps::~LandFromMaps() = default;

const LandInfo& LandFromMaps::GetInfo() const {
    return impl_->info;
}

LandPatch LandFromMaps::CutPatch(PatchCoord patch) const {
    const Impl& impl = *impl_;
    const LandInfo& info = impl.info;
    const float size = static_cast<float>(info.grid.patch_size);
    const glm::vec2 low(static_cast<float>(patch.x) * size, static_cast<float>(patch.z) * size);
    const glm::vec2 high(std::min(low.x + size, info.size_x), std::min(low.y + size, info.size_z));
    LandPatch land;
    land.patch = patch;
    land.maps = CutTerrainMaps(*impl.maps, low, high, kLandPatchMargin);
    if (!impl.rules.empty()) {
        // Weights over the window give the whole land's weights; the patch's points are cut
        // out, and beyond the land's far edges the edge repeats (as the whole land's would).
        Image window[2];
        MakeTerrainLayerWeights(land.maps, impl.rules.data(), static_cast<int>(impl.rules.size()), impl.weight_seed, window);
        const int points = static_cast<int>(std::lround(size / info.map_spacing)) + 1;
        const int first_x = static_cast<int>(std::lround(low.x / info.map_spacing)) - land.maps.map_first_x;
        const int first_z = static_cast<int>(std::lround(low.y / info.map_spacing)) - land.maps.map_first_z;
        for (int i = 0; i < 2; ++i) {
            land.weights[i] = MakeSolidImage(points, points, 0, 0, 0, 0);
            for (int z = 0; z < points; ++z) {
                const int from_z = std::min(first_z + z, land.maps.map_depth - 1);
                for (int x = 0; x < points; ++x) {
                    const int from_x = std::min(first_x + x, land.maps.map_width - 1);
                    const size_t from = (static_cast<size_t>(from_z) * static_cast<size_t>(window[i].width) + static_cast<size_t>(from_x)) * 4;
                    const size_t to = (static_cast<size_t>(z) * static_cast<size_t>(points) + static_cast<size_t>(x)) * 4;
                    std::copy(window[i].pixels.begin() + static_cast<std::ptrdiff_t>(from),
                              window[i].pixels.begin() + static_cast<std::ptrdiff_t>(from + 4),
                              land.weights[i].pixels.begin() + static_cast<std::ptrdiff_t>(to));
                }
            }
        }
    }
    return land;
}

std::shared_ptr<const LandPatch> LandFromMaps::FindPatch(PatchCoord patch) const {
    return impl_->kept.Find(patch);
}

std::shared_ptr<const LandPatch> LandFromMaps::GetPatch(PatchCoord patch) {
    if (std::shared_ptr<const LandPatch> found = impl_->kept.Find(patch)) {
        return found;
    }
    return impl_->kept.Keep(std::make_shared<const LandPatch>(CutPatch(patch)));
}

struct LandFromHeightMap::Impl {
    const HeightMap* map = nullptr;
    const PatchGroundSource* ground = nullptr;
    LandInfo info;
    KeptPatches kept;
};

LandFromHeightMap::LandFromHeightMap(const HeightMap& map, double patch_size, const glm::dvec3& world_origin,
                                     const PatchGroundSource* ground, float ground_spacing, float background_spacing, int kept_patches)
    : impl_(std::make_unique<Impl>()) {
    const double height_points = patch_size / static_cast<double>(map.spacing);
    const double ground_points = ground_spacing > 0.0f ? patch_size / static_cast<double>(ground_spacing) : 0.5;
    if (map.IsEmpty() || patch_size <= 0.0 || height_points != std::floor(height_points) || ground_points != std::floor(ground_points)) {
        FatalError("LandFromHeightMap: needs heights and a patch size that is a whole number of height and ground points");
    }
    const int step = static_cast<int>(std::lround(background_spacing / map.spacing));
    if (step < 1 || std::fabs(static_cast<float>(step) * map.spacing - background_spacing) > 1e-3f) {
        FatalError("LandFromHeightMap: the background spacing must be a whole multiple of the height spacing");
    }
    Impl& impl = *impl_;
    impl.map = &map;
    impl.ground = ground;
    impl.kept.capacity = std::max(kept_patches, 1);
    LandInfo& info = impl.info;
    info.grid.patch_size = patch_size;
    info.grid.origin = {0.0, 0.0};
    info.size_x = map.GetSizeX();
    info.size_z = map.GetSizeZ();
    info.grid.patch_count_x = static_cast<int>(std::ceil(static_cast<double>(info.size_x) / patch_size));
    info.grid.patch_count_z = static_cast<int>(std::ceil(static_cast<double>(info.size_z) / patch_size));
    info.world_origin = world_origin;
    info.spacing = map.spacing;
    info.map_spacing = ground_spacing;
    info.background = MakeCoarseHeightMap(map, step);
    info.occlusion_floor = MakeOcclusionFloor(map, step);
}

LandFromHeightMap::~LandFromHeightMap() = default;

const LandInfo& LandFromHeightMap::GetInfo() const {
    return impl_->info;
}

LandPatch LandFromHeightMap::CutPatch(PatchCoord patch) const {
    const Impl& impl = *impl_;
    const LandInfo& info = impl.info;
    const HeightMap& map = *impl.map;
    const float size = static_cast<float>(info.grid.patch_size);
    const glm::vec2 low(static_cast<float>(patch.x) * size, static_cast<float>(patch.z) * size);
    const glm::vec2 high(std::min(low.x + size, info.size_x), std::min(low.y + size, info.size_z));
    LandPatch land;
    land.patch = patch;
    // The heights over the patch and its margin, inside the map, as CutTerrainMaps cuts them;
    // the ground's grid over the same rectangle, with no maps on it.
    const auto first_point = [](float place, float spacing, int count) {
        return glm::clamp(static_cast<int>(std::floor((place - kLandPatchMargin) / spacing)), 0, count - 2);
    };
    const auto last_point = [](float place, float spacing, int count) {
        return glm::clamp(static_cast<int>(std::ceil((place + kLandPatchMargin) / spacing)), 1, count - 1);
    };
    const int hx0 = first_point(low.x, map.spacing, map.width);
    const int hz0 = first_point(low.y, map.spacing, map.depth);
    land.maps.height = CutHeightMap(map, hx0, hz0, last_point(high.x, map.spacing, map.width) - hx0 + 1,
                                    last_point(high.y, map.spacing, map.depth) - hz0 + 1);
    const int ground_width = static_cast<int>(std::lround(info.size_x / info.map_spacing)) + 1;
    const int ground_depth = static_cast<int>(std::lround(info.size_z / info.map_spacing)) + 1;
    land.maps.map_spacing = info.map_spacing;
    land.maps.map_first_x = first_point(low.x, info.map_spacing, ground_width);
    land.maps.map_first_z = first_point(low.y, info.map_spacing, ground_depth);
    land.maps.map_width = last_point(high.x, info.map_spacing, ground_width) - land.maps.map_first_x + 1;
    land.maps.map_depth = last_point(high.y, info.map_spacing, ground_depth) - land.maps.map_first_z + 1;
    if (impl.ground != nullptr) {
        LandPatchGround ground;
        impl.ground->GetGround(patch, ground);
        const int points = static_cast<int>(std::lround(size / info.map_spacing)) + 1;
        const auto fits = [points](const Image& image) {
            return image.width == points && image.height == points &&
                   image.pixels.size() == static_cast<size_t>(points) * static_cast<size_t>(points) * 4;
        };
        const bool weights = !ground.weights[0].pixels.empty() || !ground.weights[1].pixels.empty();
        const bool indices = !ground.layer_indices.pixels.empty() || !ground.layer_weights.pixels.empty();
        if ((weights && indices) || (weights && (!fits(ground.weights[0]) || !fits(ground.weights[1]))) ||
            (indices && (!fits(ground.layer_indices) || !fits(ground.layer_weights)))) {
            FatalError("LandFromHeightMap: a patch's ground must be two weight images or layer indices with their weights, "
                       "each of (patch size / ground spacing + 1) points on a side");
        }
        land.weights[0] = std::move(ground.weights[0]);
        land.weights[1] = std::move(ground.weights[1]);
        land.layer_indices = std::move(ground.layer_indices);
        land.layer_weights = std::move(ground.layer_weights);
    }
    return land;
}

std::shared_ptr<const LandPatch> LandFromHeightMap::FindPatch(PatchCoord patch) const {
    return impl_->kept.Find(patch);
}

std::shared_ptr<const LandPatch> LandFromHeightMap::GetPatch(PatchCoord patch) {
    if (std::shared_ptr<const LandPatch> found = impl_->kept.Find(patch)) {
        return found;
    }
    return impl_->kept.Keep(std::make_shared<const LandPatch>(CutPatch(patch)));
}

// --- Cooked land ---

namespace {

constexpr const char* kWorldFileHeader = "poligon-world 2\n";  // 2: the occlusion floor
// 2: a flag byte and the layer indices with their weights after the weights.
constexpr const char* kPatchFileHeader = "poligon-patch 2\n";
constexpr const char* kPatchFileHeaderVersion1 = "poligon-patch 1\n";

std::string PatchFileName(const std::string& name, PatchCoord patch) {
    return name + "/" + std::to_string(patch.x) + "_" + std::to_string(patch.z) + ".ppatch";
}

void WriteHeightMap(BinaryWriter& writer, const HeightMap& map) {
    writer.Raw(static_cast<std::int32_t>(map.width));
    writer.Raw(static_cast<std::int32_t>(map.depth));
    writer.Raw(map.spacing);
    writer.Raw(static_cast<std::int32_t>(map.first_x));
    writer.Raw(static_cast<std::int32_t>(map.first_z));
    writer.Array(map.heights);
}

bool ReadHeightMap(BinaryReader& reader, HeightMap& map) {
    std::int32_t values[4] = {};
    return reader.Raw(values[0]) && reader.Raw(values[1]) && reader.Raw(map.spacing) && reader.Raw(values[2]) && reader.Raw(values[3]) &&
           reader.Array(map.heights) && (map.width = values[0], map.depth = values[1], map.first_x = values[2], map.first_z = values[3],
                                         map.heights.size() == static_cast<size_t>(map.width) * static_cast<size_t>(map.depth));
}

void WriteImages(BinaryWriter& writer, const Image images[2]) {
    for (int i = 0; i < 2; ++i) {
        writer.Raw(static_cast<std::int32_t>(images[i].width));
        writer.Raw(static_cast<std::int32_t>(images[i].height));
        writer.Array(images[i].pixels);
    }
}

bool ReadImages(BinaryReader& reader, Image images[2]) {
    for (int i = 0; i < 2; ++i) {
        std::int32_t width = 0;
        std::int32_t height = 0;
        if (!reader.Raw(width) || !reader.Raw(height) || !reader.Array(images[i].pixels) ||
            images[i].pixels.size() != static_cast<size_t>(width) * static_cast<size_t>(height) * 4) {
            return false;
        }
        images[i].width = width;
        images[i].height = height;
    }
    return true;
}

std::vector<std::uint8_t> WritePatch(const LandPatch& patch) {
    BinaryWriter writer;
    writer.data.assign(kPatchFileHeader, kPatchFileHeader + std::strlen(kPatchFileHeader));
    writer.Raw(static_cast<std::int32_t>(patch.patch.x));
    writer.Raw(static_cast<std::int32_t>(patch.patch.z));
    const TerrainMaps& maps = patch.maps;
    WriteHeightMap(writer, maps.height);
    writer.Raw(static_cast<std::int32_t>(maps.map_width));
    writer.Raw(static_cast<std::int32_t>(maps.map_depth));
    writer.Raw(maps.map_spacing);
    writer.Raw(static_cast<std::int32_t>(maps.map_first_x));
    writer.Raw(static_cast<std::int32_t>(maps.map_first_z));
    for (const std::vector<float>* map : {&maps.water_level, &maps.flow, &maps.moisture, &maps.sediment, &maps.rock, &maps.water_distance}) {
        writer.Array(*map);
    }
    writer.Array(maps.lakes);
    WriteImages(writer, patch.weights);
    const bool indices = !patch.layer_indices.pixels.empty();
    writer.Raw(static_cast<std::uint8_t>(indices ? 1 : 0));
    if (indices) {
        const Image images[2] = {patch.layer_indices, patch.layer_weights};
        WriteImages(writer, images);
    }
    return writer.data;
}

bool ReadPatch(const std::vector<std::uint8_t>& bytes, LandPatch& patch) {
    BinaryReader reader(bytes.data(), bytes.size());
    const size_t version1_length = std::strlen(kPatchFileHeaderVersion1);
    const bool version1 = bytes.size() >= version1_length && std::memcmp(bytes.data(), kPatchFileHeaderVersion1, version1_length) == 0;
    std::int32_t x = 0;
    std::int32_t z = 0;
    std::int32_t map_values[4] = {};
    TerrainMaps& maps = patch.maps;
    if (!reader.Header(version1 ? kPatchFileHeaderVersion1 : kPatchFileHeader) || !reader.Raw(x) || !reader.Raw(z) ||
        !ReadHeightMap(reader, maps.height) ||
        !reader.Raw(map_values[0]) || !reader.Raw(map_values[1]) || !reader.Raw(maps.map_spacing) || !reader.Raw(map_values[2]) ||
        !reader.Raw(map_values[3])) {
        return false;
    }
    patch.patch = {x, z};
    maps.map_width = map_values[0];
    maps.map_depth = map_values[1];
    maps.map_first_x = map_values[2];
    maps.map_first_z = map_values[3];
    for (std::vector<float>* map : {&maps.water_level, &maps.flow, &maps.moisture, &maps.sediment, &maps.rock, &maps.water_distance}) {
        if (!reader.Array(*map)) {
            return false;
        }
    }
    if (!reader.Array(maps.lakes) || !ReadImages(reader, patch.weights)) {
        return false;
    }
    if (version1) {
        return true;
    }
    std::uint8_t indices = 0;
    if (!reader.Raw(indices)) {
        return false;
    }
    if (indices != 0) {
        Image images[2];
        if (!ReadImages(reader, images)) {
            return false;
        }
        patch.layer_indices = std::move(images[0]);
        patch.layer_weights = std::move(images[1]);
    }
    return true;
}

std::vector<std::uint8_t> WriteInfo(const LandInfo& info, std::uint64_t settings_hash) {
    BinaryWriter writer;
    writer.data.assign(kWorldFileHeader, kWorldFileHeader + std::strlen(kWorldFileHeader));
    writer.Raw(settings_hash);
    writer.Raw(info.grid.patch_size);
    writer.Raw(static_cast<std::int32_t>(info.grid.patch_count_x));
    writer.Raw(static_cast<std::int32_t>(info.grid.patch_count_z));
    writer.Raw(info.spacing);
    writer.Raw(info.map_spacing);
    writer.Raw(info.size_x);
    writer.Raw(info.size_z);
    writer.Array(info.lakes);
    WriteHeightMap(writer, info.background);
    WriteHeightMap(writer, info.occlusion_floor);
    WriteImages(writer, info.background_weights);
    writer.Raw(static_cast<std::uint8_t>(info.has_lake_viewpoint ? 1 : 0));
    writer.Raw(info.lake_viewpoint);
    return writer.data;
}

bool ReadInfo(const std::vector<std::uint8_t>& bytes, LandInfo& info, std::uint64_t& settings_hash) {
    BinaryReader reader(bytes.data(), bytes.size());
    std::int32_t count_x = 0;
    std::int32_t count_z = 0;
    if (!reader.Header(kWorldFileHeader) || !reader.Raw(settings_hash) || !reader.Raw(info.grid.patch_size) || !reader.Raw(count_x) ||
        !reader.Raw(count_z) || !reader.Raw(info.spacing) || !reader.Raw(info.map_spacing) || !reader.Raw(info.size_x) ||
        !reader.Raw(info.size_z) || !reader.Array(info.lakes) || !ReadHeightMap(reader, info.background) ||
        !ReadHeightMap(reader, info.occlusion_floor) || !ReadImages(reader, info.background_weights)) {
        return false;
    }
    std::uint8_t has_viewpoint = 0;
    if (!reader.Raw(has_viewpoint) || !reader.Raw(info.lake_viewpoint)) {
        return false;
    }
    info.has_lake_viewpoint = has_viewpoint != 0;
    info.grid.patch_count_x = count_x;
    info.grid.patch_count_z = count_z;
    info.grid.origin = {0.0, 0.0};
    return true;
}

bool WriteBytes(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(file);
}

// FNV-1a over raw bytes, continuing from `hash`.
std::uint64_t HashBytes(std::uint64_t hash, const void* data, size_t size) {
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    for (size_t i = 0; i < size; ++i) {
        hash = (hash ^ bytes[i]) * 1099511628211ull;
    }
    return hash;
}

} // namespace

namespace {

// Writes every patch (cut by the land's CutPatch), then the description.
template <typename Land>
bool CookPatches(const Land& land, const std::string& name, std::uint64_t settings_hash, const char* cooked_folder,
                 std::string& error) {
    const std::filesystem::path folder(cooked_folder);
    const LandInfo& info = land.GetInfo();
    for (int z = 0; z < info.grid.patch_count_z; ++z) {
        for (int x = 0; x < info.grid.patch_count_x; ++x) {
            const PatchCoord patch{x, z};
            if (!WriteBytes(folder / PatchFileName(name, patch), WritePatch(land.CutPatch(patch)))) {
                error = "cannot write " + (folder / PatchFileName(name, patch)).string();
                return false;
            }
        }
    }
    // The description last: a world whose description is there is whole.
    if (!WriteBytes(folder / (name + ".pworld"), WriteInfo(info, settings_hash))) {
        error = "cannot write " + (folder / (name + ".pworld")).string();
        return false;
    }
    return true;
}

} // namespace

bool CookLand(const LandFromMaps& land, const std::string& name, std::uint64_t settings_hash, const char* cooked_folder,
              std::string& error) {
    return CookPatches(land, name, settings_hash, cooked_folder, error);
}

bool CookLand(const LandFromHeightMap& land, const std::string& name, std::uint64_t settings_hash, const char* cooked_folder,
              std::string& error) {
    return CookPatches(land, name, settings_hash, cooked_folder, error);
}

bool CookLevelWorld(const Level& level, const std::vector<GroundLayer>& layers, const char* cooked_folder, const char* terrain_cache,
                    bool& cooked, std::string& error) {
    cooked = false;
    if (level.world.empty()) {
        error = "the level names no streamed world";
        return false;
    }
    // Everything the cooked files depend on, in one number.
    std::uint64_t hash = 1469598103934665603ull;
    const std::uint64_t generator = HashTerrainGeneratorSettings(level.terrain);
    const int format = 3;  // 2: the lake viewpoint, 3: the occlusion floor
    hash = HashBytes(hash, &format, sizeof(format));
    hash = HashBytes(hash, &generator, sizeof(generator));
    hash = HashBytes(hash, &level.world_patch_size, sizeof(level.world_patch_size));
    hash = HashBytes(hash, &level.world_background_spacing, sizeof(level.world_background_spacing));
    for (const GroundLayer& layer : layers) {
        hash = HashBytes(hash, &layer.rule, sizeof(layer.rule));
    }
    const std::filesystem::path description = std::filesystem::path(cooked_folder) / (level.world + ".pworld");
    std::vector<std::uint8_t> bytes;
    {
        std::ifstream file(description, std::ios::binary);
        bytes.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    }
    LandInfo existing;
    std::uint64_t existing_hash = 0;
    if (!bytes.empty() && ReadInfo(bytes, existing, existing_hash) && existing_hash == hash) {
        return true;  // up to date
    }
    TerrainMaps maps;
    bool generated = false;
    if (!LoadOrGenerateTerrain(level.terrain, terrain_cache, maps, generated, error)) {
        return false;
    }
    std::vector<TerrainLayerRule> rules;
    for (const GroundLayer& layer : layers) {
        rules.push_back(layer.rule);
    }
    const LandFromMaps land(maps, level.world_patch_size, glm::dvec3(0.0), rules.data(), static_cast<int>(rules.size()), 1u,
                            level.world_background_spacing);
    if (!CookLand(land, level.world, hash, cooked_folder, error)) {
        return false;
    }
    cooked = true;
    return true;
}

struct LandFromFiles::Impl {
    const AssetLibrary* library = nullptr;
    std::string name;
    LandInfo info;
    bool open = false;
    std::string error;
    KeptPatches kept;
};

LandFromFiles::LandFromFiles(const AssetLibrary& library, const std::string& name, const glm::dvec3& world_origin, int kept_patches)
    : impl_(std::make_unique<Impl>()) {
    Impl& impl = *impl_;
    impl.library = &library;
    impl.name = name;
    impl.kept.capacity = std::max(kept_patches, 1);
    std::vector<std::uint8_t> bytes;
    std::uint64_t hash = 0;
    if (!library.ReadFile(name + ".pworld", bytes)) {
        impl.error = "no cooked world \"" + name + "\"";
    } else if (!ReadInfo(bytes, impl.info, hash)) {
        impl.error = name + ".pworld: not a cooked world, or cut short";
    } else {
        impl.info.world_origin = world_origin;
        impl.open = true;
    }
}

LandFromFiles::~LandFromFiles() = default;

bool LandFromFiles::IsOpen() const {
    return impl_->open;
}

const std::string& LandFromFiles::GetError() const {
    return impl_->error;
}

const LandInfo& LandFromFiles::GetInfo() const {
    return impl_->info;
}

std::shared_ptr<const LandPatch> LandFromFiles::FindPatch(PatchCoord patch) const {
    return impl_->kept.Find(patch);
}

std::shared_ptr<const LandPatch> LandFromFiles::GetPatch(PatchCoord patch) {
    if (std::shared_ptr<const LandPatch> found = impl_->kept.Find(patch)) {
        return found;
    }
    std::vector<std::uint8_t> bytes;
    auto read = std::make_shared<LandPatch>();
    if (!impl_->library->ReadFile(PatchFileName(impl_->name, patch), bytes) || !ReadPatch(bytes, *read)) {
        FatalError(("LandFromFiles: cannot read " + PatchFileName(impl_->name, patch)).c_str());
    }
    return impl_->kept.Keep(read);
}

} // namespace poligon

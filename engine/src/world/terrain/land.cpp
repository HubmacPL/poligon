#include "poligon/world/land.h"

#include <algorithm>
#include <cmath>
#include <list>
#include <utility>

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

struct LandFromMaps::Impl {
    const TerrainMaps* maps = nullptr;
    std::vector<TerrainLayerRule> rules;
    std::uint32_t weight_seed = 0;
    LandInfo info;
    int kept_patches = 32;
    // The patches cut last, newest first.
    mutable Mutex mutex;
    mutable std::list<std::shared_ptr<const LandPatch>> kept;
};

LandFromMaps::LandFromMaps(const TerrainMaps& maps, double patch_size, const glm::dvec3& world_origin, const TerrainLayerRule* rules,
                           int rule_count, std::uint32_t weight_seed, float background_spacing, int kept_patches)
    : impl_(std::make_unique<Impl>()) {
    const HeightMap& height = maps.height;
    const double map_points = patch_size / static_cast<double>(maps.map_spacing);
    if (height.IsEmpty() || patch_size <= 0.0 || map_points != std::floor(map_points) ||
        patch_size / static_cast<double>(height.spacing) != std::floor(patch_size / static_cast<double>(height.spacing)) ||
        rule_count < 0 || rule_count > kMaxTerrainLayers || (rule_count > 0 && rules == nullptr)) {
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
    impl.kept_patches = std::max(kept_patches, 1);
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
    // The patch asked for moves to the front: patches in use stay, the oldest unused go first.
    MutexLock lock(impl_->mutex);
    for (auto it = impl_->kept.begin(); it != impl_->kept.end(); ++it) {
        if ((*it)->patch == patch) {
            impl_->kept.splice(impl_->kept.begin(), impl_->kept, it);
            return impl_->kept.front();
        }
    }
    return nullptr;
}

std::shared_ptr<const LandPatch> LandFromMaps::GetPatch(PatchCoord patch) {
    if (std::shared_ptr<const LandPatch> found = FindPatch(patch)) {
        return found;
    }
    // Cut outside the lock: another thread may cut the same patch meanwhile; both results are
    // equal, and the first one kept wins.
    std::shared_ptr<const LandPatch> cut = std::make_shared<const LandPatch>(CutPatch(patch));
    MutexLock lock(impl_->mutex);
    for (const std::shared_ptr<const LandPatch>& kept : impl_->kept) {
        if (kept->patch == patch) {
            return kept;
        }
    }
    impl_->kept.push_front(cut);
    while (static_cast<int>(impl_->kept.size()) > impl_->kept_patches) {
        impl_->kept.pop_back();
    }
    return cut;
}

} // namespace poligon

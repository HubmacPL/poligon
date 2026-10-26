#include "poligon/world/sky_occlusion.h"

#include <algorithm>
#include <cmath>
#include <map>

#include "poligon/core/fatal_error.h"
#include "poligon/world/land.h"
#include "poligon/world/terrain.h"

namespace poligon {

namespace {

// The land's part of sky occlusion at `resolution` x `resolution` points from `origin`,
// `spacing` apart, for land whose height at a world place `height_at` gives.
void FillLandOcclusion(SkyOcclusionMap& map, float reach, const std::function<double(double, double)>& height_at) {
    const float spacing = map.spacing;
    // Steps outwards along each direction: closer together near the point, wider far out.
    std::vector<float> distances;
    for (float distance = spacing; distance <= reach; distance *= 1.3f) {
        distances.push_back(distance);
    }
    const glm::vec2 directions[8] = {{1.0f, 0.0f},  {0.7071f, 0.7071f},  {0.0f, 1.0f},  {-0.7071f, 0.7071f},
                                     {-1.0f, 0.0f}, {-0.7071f, -0.7071f}, {0.0f, -1.0f}, {0.7071f, -0.7071f}};
    for (int z = 0; z < map.resolution; ++z) {
        for (int x = 0; x < map.resolution; ++x) {
            const glm::dvec2 place = map.origin + glm::dvec2(static_cast<double>(x), static_cast<double>(z)) * static_cast<double>(spacing);
            const double height = height_at(place.x, place.y) + 1.0;  // seen from a meter above the ground
            // The open sky above a horizon at elevation angle a, for even sky light: 1 - sin(a)
            // per direction, averaged.
            float open = 0.0f;
            for (const glm::vec2& direction : directions) {
                float steepest = 0.0f;
                for (const float distance : distances) {
                    const glm::dvec2 point = place + glm::dvec2(direction * distance);
                    steepest = std::max(steepest, static_cast<float>(height_at(point.x, point.y) - height) / distance);
                }
                open += 1.0f - steepest / std::sqrt(1.0f + steepest * steepest);
            }
            map.values[static_cast<size_t>(z) * static_cast<size_t>(map.resolution) + static_cast<size_t>(x)] =
                static_cast<std::uint8_t>(std::lround(std::clamp(open / 8.0f, 0.0f, 1.0f) * 255.0f));
        }
    }
    ++map.version;
}

} // namespace

GroundMap SkyOcclusionMap::GetGroundMap() const {
    GroundMap map;
    if (resolution < 2 || values.size() < static_cast<size_t>(resolution) * static_cast<size_t>(resolution)) {
        return map;
    }
    map.values = values.data();
    map.resolution = resolution;
    map.corner = origin - glm::dvec2(static_cast<double>(spacing) * 0.5);
    map.size = static_cast<float>(resolution) * spacing;
    map.version = version;
    return map;
}

float SkyOcclusionMap::Get(glm::dvec2 position) const {
    if (resolution < 2 || values.empty()) {
        return 1.0f;
    }
    const glm::vec2 local((position - origin) / static_cast<double>(spacing));
    if (local.x < 0.0f || local.y < 0.0f || local.x > static_cast<float>(resolution - 1) || local.y > static_cast<float>(resolution - 1)) {
        return 1.0f;
    }
    const int x = std::min(static_cast<int>(local.x), resolution - 2);
    const int z = std::min(static_cast<int>(local.y), resolution - 2);
    const float fx = local.x - static_cast<float>(x);
    const float fz = local.y - static_cast<float>(z);
    const auto at = [this](int px, int pz) {
        return static_cast<float>(values[static_cast<size_t>(pz) * static_cast<size_t>(resolution) + static_cast<size_t>(px)]) / 255.0f;
    };
    return glm::mix(glm::mix(at(x, z), at(x + 1, z), fx), glm::mix(at(x, z + 1), at(x + 1, z + 1), fx), fz);
}

SkyOcclusionMap MakeTerrainSkyOcclusion(const Terrain& terrain, glm::dvec2 center, float size, float spacing, float reach) {
    if (size <= 0.0f || spacing <= 0.0f || reach <= 0.0f) {
        FatalError("MakeTerrainSkyOcclusion: size, spacing and reach must be above zero");
    }
    SkyOcclusionMap map;
    map.spacing = spacing;
    map.resolution = static_cast<int>(std::ceil(size / spacing)) + 1;
    map.origin = center - glm::dvec2(static_cast<double>(size) * 0.5);
    map.values.resize(static_cast<size_t>(map.resolution) * static_cast<size_t>(map.resolution));
    FillLandOcclusion(map, reach, [&terrain](double x, double z) { return terrain.GetHeight(x, z); });
    return map;
}

void AddCanopy(SkyOcclusionMap& map, glm::dvec2 position, float radius, float strength) {
    if (radius <= 0.0f || strength <= 0.0f || map.resolution < 2) {
        return;
    }
    const glm::vec2 local((position - map.origin) / static_cast<double>(map.spacing));
    const float reach = radius / map.spacing;
    const int first_x = std::max(static_cast<int>(std::floor(local.x - reach)), 0);
    const int last_x = std::min(static_cast<int>(std::ceil(local.x + reach)), map.resolution - 1);
    const int first_z = std::max(static_cast<int>(std::floor(local.y - reach)), 0);
    const int last_z = std::min(static_cast<int>(std::ceil(local.y + reach)), map.resolution - 1);
    for (int z = first_z; z <= last_z; ++z) {
        for (int x = first_x; x <= last_x; ++x) {
            const float distance = glm::length(glm::vec2(static_cast<float>(x), static_cast<float>(z)) - local) / reach;
            if (distance >= 1.0f) {
                continue;
            }
            const float share = strength * (1.0f - distance * distance);
            std::uint8_t& value = map.values[static_cast<size_t>(z) * static_cast<size_t>(map.resolution) + static_cast<size_t>(x)];
            value = static_cast<std::uint8_t>(std::lround(static_cast<float>(value) * (1.0f - share)));
        }
    }
    ++map.version;
}

namespace {

struct SkyTile : PatchData {
    SkyOcclusionMap map;  // the patch's points: from its corner, `points` x `points`
};

} // namespace

struct SkyOcclusionPatchSource::Impl {
    LandSource* land = nullptr;
    int window_radius = 4;
    float spacing = 4.0f;
    float reach = 160.0f;
    int points = 0;  // points along a patch's side
    Planter planter;
    float min_height = 6.0f;
    float strength = 0.55f;
    float radius_share = 0.9f;
    std::map<PatchCoord, SkyOcclusionMap> tiles;
    // The tiles joined (built when asked for after a change).
    mutable SkyOcclusionMap joined;
    mutable bool dirty = true;
    mutable std::uint32_t version = 0;

    glm::dvec2 PatchCorner(PatchCoord patch) const {
        const LandInfo& info = land->GetInfo();
        return glm::dvec2(info.world_origin.x, info.world_origin.z) +
               glm::dvec2(static_cast<double>(patch.x), static_cast<double>(patch.z)) * info.grid.patch_size;
    }

    void Join() const {
        if (!dirty) {
            return;
        }
        dirty = false;
        joined.values.clear();
        joined.resolution = 0;
        joined.version = ++version;
        if (tiles.empty()) {
            return;
        }
        // A square of patches around the held ones, at most the window's size.
        PatchCoord low = tiles.begin()->first;
        PatchCoord high = low;
        for (const auto& [patch, tile] : tiles) {
            low = {std::min(low.x, patch.x), std::min(low.z, patch.z)};
            high = {std::max(high.x, patch.x), std::max(high.z, patch.z)};
        }
        const int count = std::min(std::max(high.x - low.x, high.z - low.z) + 1, 2 * window_radius + 1);
        joined.spacing = spacing;
        joined.resolution = count * points;
        joined.origin = PatchCorner(low);
        joined.values.assign(static_cast<size_t>(joined.resolution) * static_cast<size_t>(joined.resolution), 255);
        for (const auto& [patch, tile] : tiles) {
            const int column = patch.x - low.x;
            const int row = patch.z - low.z;
            if (column >= count || row >= count) {
                continue;
            }
            for (int z = 0; z < points; ++z) {
                std::copy(tile.values.begin() + static_cast<std::ptrdiff_t>(z * points),
                          tile.values.begin() + static_cast<std::ptrdiff_t>((z + 1) * points),
                          joined.values.begin() + static_cast<std::ptrdiff_t>((row * points + z) * joined.resolution + column * points));
            }
        }
    }
};

SkyOcclusionPatchSource::SkyOcclusionPatchSource(LandSource& land, int window_radius, float spacing, float reach)
    : impl_(std::make_unique<Impl>()) {
    const double points = land.GetInfo().grid.patch_size / static_cast<double>(spacing);
    if (spacing <= 0.0f || reach <= 0.0f || window_radius < 0 || points != std::floor(points) ||
        static_cast<double>(reach) > land.GetInfo().grid.patch_size) {
        FatalError("SkyOcclusionPatchSource: the patch size must be a whole number of points apart and at least the reach");
    }
    impl_->land = &land;
    impl_->window_radius = window_radius;
    impl_->spacing = spacing;
    impl_->reach = reach;
    impl_->points = static_cast<int>(points);
}

SkyOcclusionPatchSource::~SkyOcclusionPatchSource() = default;

void SkyOcclusionPatchSource::SetCanopy(Planter planter, float min_height, float strength, float radius_share) {
    impl_->planter = std::move(planter);
    impl_->min_height = min_height;
    impl_->strength = strength;
    impl_->radius_share = radius_share;
}

std::unique_ptr<PatchData> SkyOcclusionPatchSource::Load(PatchCoord patch) {
    const Impl& impl = *impl_;
    LandSource& land = *impl.land;
    const LandInfo& info = land.GetInfo();
    // The patch and its neighbors hold every height within `reach` (shorter than a patch).
    std::map<PatchCoord, std::shared_ptr<const LandPatch>> near;
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
            const PatchCoord neighbor{patch.x + dx, patch.z + dz};
            if (neighbor.x >= 0 && neighbor.z >= 0 && neighbor.x < info.grid.patch_count_x && neighbor.z < info.grid.patch_count_z) {
                near[neighbor] = land.GetPatch(neighbor);
            }
        }
    }
    const double size = info.grid.patch_size;
    const auto height_at = [&](double x, double z) {
        // Map coordinates, kept on the land: beyond its edge the height at the edge counts, as
        // for a terrain of one map.
        const double map_x = std::clamp(x - info.world_origin.x, 0.0, static_cast<double>(info.size_x));
        const double map_z = std::clamp(z - info.world_origin.z, 0.0, static_cast<double>(info.size_z));
        PatchCoord at{std::min(static_cast<int>(map_x / size), info.grid.patch_count_x - 1),
                      std::min(static_cast<int>(map_z / size), info.grid.patch_count_z - 1)};
        at.x = std::clamp(at.x, patch.x - 1, patch.x + 1);
        at.z = std::clamp(at.z, patch.z - 1, patch.z + 1);
        const auto found = near.find(at);
        const HeightMap& heights = found != near.end() ? found->second->maps.height : near.at(patch)->maps.height;
        return info.world_origin.y + static_cast<double>(heights.GetHeight(static_cast<float>(map_x), static_cast<float>(map_z)));
    };
    auto tile = std::make_unique<SkyTile>();
    tile->map.spacing = impl.spacing;
    tile->map.resolution = impl.points;
    tile->map.origin = impl.PatchCorner(patch);
    tile->map.values.resize(static_cast<size_t>(impl.points) * static_cast<size_t>(impl.points));
    FillLandOcclusion(tile->map, impl.reach, height_at);
    if (impl.planter) {
        // Crowns on the patch and within a crown's reach around it, each from its own patch.
        const float crown_reach = 15.0f;
        const glm::vec2 low(static_cast<float>(patch.x * size) - crown_reach, static_cast<float>(patch.z * size) - crown_reach);
        const glm::vec2 high(static_cast<float>((patch.x + 1) * size) + crown_reach, static_cast<float>((patch.z + 1) * size) + crown_reach);
        std::vector<PlacedPlant> plants;
        for (const auto& [neighbor, neighbor_land] : near) {
            const glm::vec2 neighbor_low(static_cast<float>(neighbor.x * size), static_cast<float>(neighbor.z * size));
            const glm::vec2 neighbor_high(std::min(neighbor_low.x + static_cast<float>(size), info.size_x),
                                          std::min(neighbor_low.y + static_cast<float>(size), info.size_z));
            const glm::vec2 part_low = glm::max(low, neighbor_low);
            const glm::vec2 part_high = glm::min(high, neighbor_high);
            if (part_low.x < part_high.x && part_low.y < part_high.y) {
                impl.planter(*neighbor_land, part_low, part_high, plants);
            }
        }
        for (const PlacedPlant& plant : plants) {
            const PlantVariant& variant = plant.kind->variants[static_cast<size_t>(plant.variant)];
            if (variant.height * plant.scale > impl.min_height) {
                const float radius = (variant.half_width.x + variant.half_width.y) * 0.5f * plant.scale * impl.radius_share;
                AddCanopy(tile->map, glm::dvec2(plant.foot.x, plant.foot.z), radius, impl.strength);
            }
        }
    }
    return tile;
}

void SkyOcclusionPatchSource::Attach(PatchCoord patch, std::unique_ptr<PatchData> data) {
    impl_->tiles[patch] = std::move(static_cast<SkyTile&>(*data).map);
    impl_->dirty = true;
}

void SkyOcclusionPatchSource::Detach(PatchCoord patch) {
    impl_->tiles.erase(patch);
    impl_->dirty = true;
}

GroundMap SkyOcclusionPatchSource::GetGroundMap() const {
    impl_->Join();
    return impl_->joined.GetGroundMap();
}

float SkyOcclusionPatchSource::Get(glm::dvec2 position) const {
    impl_->Join();
    return impl_->joined.Get(position);
}

} // namespace poligon

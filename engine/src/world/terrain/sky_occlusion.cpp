#include "poligon/world/sky_occlusion.h"

#include <algorithm>
#include <cmath>

#include "poligon/core/fatal_error.h"
#include "poligon/world/terrain.h"

namespace poligon {

GroundMap SkyOcclusionMap::GetGroundMap() const {
    GroundMap map;
    if (resolution < 2 || values.size() < static_cast<size_t>(resolution) * static_cast<size_t>(resolution)) {
        return map;
    }
    map.values = values.data();
    map.resolution = resolution;
    map.corner = origin - glm::vec2(spacing * 0.5f);
    map.size = static_cast<float>(resolution) * spacing;
    map.version = version;
    return map;
}

float SkyOcclusionMap::Get(glm::vec2 position) const {
    if (resolution < 2 || values.empty()) {
        return 1.0f;
    }
    const glm::vec2 local = (position - origin) / spacing;
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

SkyOcclusionMap MakeTerrainSkyOcclusion(const Terrain& terrain, glm::vec2 center, float size, float spacing, float reach) {
    if (size <= 0.0f || spacing <= 0.0f || reach <= 0.0f) {
        FatalError("MakeTerrainSkyOcclusion: size, spacing and reach must be above zero");
    }
    SkyOcclusionMap map;
    map.spacing = spacing;
    map.resolution = static_cast<int>(std::ceil(size / spacing)) + 1;
    map.origin = center - glm::vec2(size * 0.5f);
    map.values.resize(static_cast<size_t>(map.resolution) * static_cast<size_t>(map.resolution));
    // Steps outwards along each direction: closer together near the point, wider far out.
    std::vector<float> distances;
    for (float distance = spacing; distance <= reach; distance *= 1.3f) {
        distances.push_back(distance);
    }
    const glm::vec2 directions[8] = {{1.0f, 0.0f},  {0.7071f, 0.7071f},  {0.0f, 1.0f},  {-0.7071f, 0.7071f},
                                     {-1.0f, 0.0f}, {-0.7071f, -0.7071f}, {0.0f, -1.0f}, {0.7071f, -0.7071f}};
    for (int z = 0; z < map.resolution; ++z) {
        for (int x = 0; x < map.resolution; ++x) {
            const glm::vec2 place = map.origin + glm::vec2(static_cast<float>(x), static_cast<float>(z)) * spacing;
            const float height = terrain.GetHeight(place.x, place.y) + 1.0f;  // seen from a meter above the ground
            // The open sky above a horizon at elevation angle a, for even sky light: 1 - sin(a)
            // per direction, averaged.
            float open = 0.0f;
            for (const glm::vec2& direction : directions) {
                float steepest = 0.0f;
                for (const float distance : distances) {
                    const glm::vec2 point = place + direction * distance;
                    steepest = std::max(steepest, (terrain.GetHeight(point.x, point.y) - height) / distance);
                }
                open += 1.0f - steepest / std::sqrt(1.0f + steepest * steepest);
            }
            map.values[static_cast<size_t>(z) * static_cast<size_t>(map.resolution) + static_cast<size_t>(x)] =
                static_cast<std::uint8_t>(std::lround(std::clamp(open / 8.0f, 0.0f, 1.0f) * 255.0f));
        }
    }
    ++map.version;
    return map;
}

void AddCanopy(SkyOcclusionMap& map, glm::vec2 position, float radius, float strength) {
    if (radius <= 0.0f || strength <= 0.0f || map.resolution < 2) {
        return;
    }
    const glm::vec2 local = (position - map.origin) / map.spacing;
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

} // namespace poligon

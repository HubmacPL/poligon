#include "poligon/terrain.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "poligon/fatal_error.h"

namespace poligon {

namespace {

// Cells along one edge of the smallest patch; must match the renderer's grid mesh.
constexpr int kPatchCells = 64;
constexpr int kMaxLevels = 16;

// Lowest and highest land inside the squares of one size.
struct LevelRanges {
    int columns = 0;
    int rows = 0;
    std::vector<float> lowest;
    std::vector<float> highest;
};

} // namespace

struct Terrain::Impl {
    Renderer* renderer = nullptr;
    HeightMap map;
    TerrainConfig config;
    TerrainHandle handle;
    // Index 0: squares of the smallest size; each further level has squares twice as large.
    std::vector<LevelRanges> levels;
    std::vector<TerrainPatch> patches;
    TerrainStats stats;

    float GetSquareSize(int level) const { return static_cast<float>(kPatchCells << level) * map.spacing; }

    // Adds the square (column, row) of the given level, or its four quarters if the camera is close.
    void Select(int level, int column, int row, const glm::vec3& eye) {
        const LevelRanges& ranges = levels[static_cast<size_t>(level)];
        if (column >= ranges.columns || row >= ranges.rows) {
            return;  // wholly beyond the map's edge
        }
        const size_t index = static_cast<size_t>(row) * static_cast<size_t>(ranges.columns) + static_cast<size_t>(column);
        const float size = GetSquareSize(level);
        TerrainPatch patch;
        patch.x = config.origin.x + static_cast<float>(column) * size;
        patch.z = config.origin.z + static_cast<float>(row) * size;
        patch.size = size;
        patch.min_height = config.origin.y + ranges.lowest[index];
        patch.max_height = config.origin.y + ranges.highest[index];

        if (level > 0) {
            // Distance from the camera to the nearest point of the box around the square's land.
            const glm::vec3 nearest = glm::clamp(eye, glm::vec3(patch.x, patch.min_height, patch.z),
                                                 glm::vec3(patch.x + size, patch.max_height, patch.z + size));
            if (glm::distance(eye, nearest) < size * config.detail) {
                for (int quarter = 0; quarter < 4; ++quarter) {
                    Select(level - 1, column * 2 + (quarter & 1), row * 2 + (quarter >> 1), eye);
                }
                return;
            }
        }
        patches.push_back(patch);
        ++stats.patches;
        ++stats.patches_per_level[level];
    }
};

Terrain::Terrain(Renderer& renderer, const HeightMap& map, const TerrainConfig& config) : impl_(std::make_unique<Impl>()) {
    if (map.IsEmpty()) {
        FatalError("Terrain: the height map is empty");
    }
    Impl& impl = *impl_;
    impl.renderer = &renderer;
    impl.map = map;
    impl.config = config;
    impl.handle = renderer.CreateTerrain(map.heights.data(), map.width, map.depth, map.spacing, config.origin);

    // The smallest squares: 64 cells, that is 65 points, along each edge; neighbors share their rim.
    LevelRanges finest;
    finest.columns = (map.width - 2) / kPatchCells + 1;
    finest.rows = (map.depth - 2) / kPatchCells + 1;
    finest.lowest.assign(static_cast<size_t>(finest.columns) * static_cast<size_t>(finest.rows), 1.0e30f);
    finest.highest.assign(finest.lowest.size(), -1.0e30f);
    for (int row = 0; row < finest.rows; ++row) {
        for (int column = 0; column < finest.columns; ++column) {
            float lowest = 1.0e30f;
            float highest = -1.0e30f;
            const int last_z = glm::min((row + 1) * kPatchCells, map.depth - 1);
            const int last_x = glm::min((column + 1) * kPatchCells, map.width - 1);
            for (int iz = row * kPatchCells; iz <= last_z; ++iz) {
                const float* heights = &map.heights[static_cast<size_t>(iz) * static_cast<size_t>(map.width)];
                for (int ix = column * kPatchCells; ix <= last_x; ++ix) {
                    lowest = glm::min(lowest, heights[ix]);
                    highest = glm::max(highest, heights[ix]);
                }
            }
            const size_t index = static_cast<size_t>(row) * static_cast<size_t>(finest.columns) + static_cast<size_t>(column);
            finest.lowest[index] = lowest;
            finest.highest[index] = highest;
        }
    }
    impl.levels.push_back(std::move(finest));

    // Larger squares: each covers four of the level below.
    while ((impl.levels.back().columns > 1 || impl.levels.back().rows > 1) && static_cast<int>(impl.levels.size()) < kMaxLevels) {
        const LevelRanges& below = impl.levels.back();
        LevelRanges above;
        above.columns = (below.columns + 1) / 2;
        above.rows = (below.rows + 1) / 2;
        above.lowest.assign(static_cast<size_t>(above.columns) * static_cast<size_t>(above.rows), 1.0e30f);
        above.highest.assign(above.lowest.size(), -1.0e30f);
        for (int row = 0; row < below.rows; ++row) {
            for (int column = 0; column < below.columns; ++column) {
                const size_t from = static_cast<size_t>(row) * static_cast<size_t>(below.columns) + static_cast<size_t>(column);
                const size_t to = static_cast<size_t>(row / 2) * static_cast<size_t>(above.columns) + static_cast<size_t>(column / 2);
                above.lowest[to] = glm::min(above.lowest[to], below.lowest[from]);
                above.highest[to] = glm::max(above.highest[to], below.highest[from]);
            }
        }
        impl.levels.push_back(std::move(above));
    }
}

Terrain::~Terrain() {
    impl_->renderer->DestroyTerrain(impl_->handle);
}

void Terrain::Draw(const Camera& camera) {
    Impl& impl = *impl_;
    impl.patches.clear();
    impl.stats = TerrainStats{};
    const int top = static_cast<int>(impl.levels.size()) - 1;
    const LevelRanges& roots = impl.levels[static_cast<size_t>(top)];
    for (int row = 0; row < roots.rows; ++row) {
        for (int column = 0; column < roots.columns; ++column) {
            impl.Select(top, column, row, camera.position);
        }
    }
    impl.renderer->DrawTerrain(impl.handle, impl.config.material, impl.patches.data(), static_cast<int>(impl.patches.size()));
}

float Terrain::GetHeight(float x, float z) const {
    return impl_->config.origin.y + impl_->map.GetHeight(x - impl_->config.origin.x, z - impl_->config.origin.z);
}

glm::vec3 Terrain::GetNormal(float x, float z) const {
    return impl_->map.GetNormal(x - impl_->config.origin.x, z - impl_->config.origin.z);
}

float Terrain::GetSlopeDegrees(float x, float z) const {
    return impl_->map.GetSlopeDegrees(x - impl_->config.origin.x, z - impl_->config.origin.z);
}

glm::vec2 Terrain::GetMin() const {
    return {impl_->config.origin.x, impl_->config.origin.z};
}

glm::vec2 Terrain::GetMax() const {
    return {impl_->config.origin.x + impl_->map.GetSizeX(), impl_->config.origin.z + impl_->map.GetSizeZ()};
}

const HeightMap& Terrain::GetHeightMap() const {
    return impl_->map;
}

void Terrain::SetMaterial(const TerrainMaterial& material) {
    impl_->config.material = material;
}

void Terrain::SetDetail(float detail) {
    if (detail <= 0.0f) {
        FatalError("Terrain::SetDetail: the detail factor must be above zero");
    }
    impl_->config.detail = detail;
}

const TerrainStats& Terrain::GetStats() const {
    return impl_->stats;
}

namespace {

float CornerValue(std::int32_t x, std::int32_t z, std::uint32_t seed) {
    std::uint32_t h = seed ^ (static_cast<std::uint32_t>(x) * 0x8DA6B343u) ^ (static_cast<std::uint32_t>(z) * 0xD8163841u);
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    h *= 0x297A2D39u;
    h ^= h >> 15;
    return static_cast<float>(h >> 8) / 16777216.0f;
}

// Smooth value noise in 0..1, two layers.
float PatchNoise(float x, float z, std::uint32_t seed) {
    float sum = 0.0f;
    float amplitude = 0.65f;
    for (int octave = 0; octave < 2; ++octave) {
        const float fx = std::floor(x);
        const float fz = std::floor(z);
        const std::int32_t ix = static_cast<std::int32_t>(fx);
        const std::int32_t iz = static_cast<std::int32_t>(fz);
        float tx = x - fx;
        float tz = z - fz;
        tx = tx * tx * (3.0f - 2.0f * tx);
        tz = tz * tz * (3.0f - 2.0f * tz);
        const float a = CornerValue(ix, iz, seed);
        const float b = CornerValue(ix + 1, iz, seed);
        const float c = CornerValue(ix, iz + 1, seed);
        const float d = CornerValue(ix + 1, iz + 1, seed);
        sum += amplitude * ((a + (b - a) * tx) + ((c + (d - c) * tx) - (a + (b - a) * tx)) * tz);
        amplitude = 0.35f;
        x = x * 2.03f + 17.0f;
        z = z * 2.03f + 31.0f;
        seed += 101u;
    }
    return sum;
}

} // namespace

void MakeTerrainLayerWeights(const TerrainMaps& maps, const TerrainLayerRule* rules, int count, std::uint32_t seed,
                             Image weights[2]) {
    if (count < 1 || count > kMaxTerrainLayers || rules == nullptr || maps.map_width < 2 || maps.map_depth < 2) {
        FatalError("MakeTerrainLayerWeights: needs maps and between 1 and 8 rules");
    }
    for (int image = 0; image < 2; ++image) {
        weights[image] = MakeSolidImage(maps.map_width, maps.map_depth, 0, 0, 0, 0);
    }
    float values[kMaxTerrainLayers] = {};
    for (int z = 0; z < maps.map_depth; ++z) {
        for (int x = 0; x < maps.map_width; ++x) {
            const size_t i = static_cast<size_t>(z) * static_cast<size_t>(maps.map_width) + static_cast<size_t>(x);
            const float px = static_cast<float>(x) * maps.map_spacing;
            const float pz = static_cast<float>(z) * maps.map_spacing;
            float total = 0.0f;
            for (int layer = 0; layer < count; ++layer) {
                const TerrainLayerRule& rule = rules[layer];
                float weight = rule.strength * rule.conditions.Weight(maps, glm::vec2(px, pz));
                if (weight > 0.0f && rule.patch_size > 0.0f) {
                    const float noise = PatchNoise(px / rule.patch_size, pz / rule.patch_size, seed + static_cast<std::uint32_t>(layer) * 7919u);
                    // Cut the noise so that about `patch_cover` of the land is inside a patch.
                    const float edge = 1.0f - rule.patch_cover;
                    const float t = std::clamp((noise - (edge - 0.08f)) / 0.16f, 0.0f, 1.0f);
                    weight *= t * t * (3.0f - 2.0f * t);
                }
                values[layer] = std::max(weight, 0.0f);
                total += values[layer];
            }
            if (total <= 1e-6f) {
                values[0] = 1.0f;
                total = 1.0f;
                for (int layer = 1; layer < count; ++layer) {
                    values[layer] = 0.0f;
                }
            }
            for (int layer = 0; layer < count; ++layer) {
                weights[layer / 4].pixels[i * 4 + static_cast<size_t>(layer % 4)] =
                    static_cast<std::uint8_t>(std::lround(values[layer] / total * 255.0f));
            }
        }
    }
}

} // namespace poligon

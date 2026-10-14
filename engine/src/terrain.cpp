#include "poligon/terrain.h"

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

} // namespace poligon

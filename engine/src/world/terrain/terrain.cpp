#include "poligon/world/terrain.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "poligon/core/fatal_error.h"
#include "poligon/physics/physics.h"
#include "poligon/world/land.h"

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

// Lowest and highest land of the smallest squares of a map (64 cells, 65 points along each
// edge; neighbors share their rim), from point (first_x, first_z) on, `columns` x `rows` of
// them, and the larger levels above them, up to one square or `max_levels` levels.
std::vector<LevelRanges> BuildLevels(const HeightMap& map, int first_x, int first_z, int last_x, int last_z, int max_levels) {
    std::vector<LevelRanges> levels;
    LevelRanges finest;
    finest.columns = (last_x - first_x - 1) / kPatchCells + 1;
    finest.rows = (last_z - first_z - 1) / kPatchCells + 1;
    finest.lowest.assign(static_cast<size_t>(finest.columns) * static_cast<size_t>(finest.rows), 1.0e30f);
    finest.highest.assign(finest.lowest.size(), -1.0e30f);
    for (int row = 0; row < finest.rows; ++row) {
        for (int column = 0; column < finest.columns; ++column) {
            float lowest = 1.0e30f;
            float highest = -1.0e30f;
            const int end_z = glm::min(first_z + (row + 1) * kPatchCells, last_z);
            const int end_x = glm::min(first_x + (column + 1) * kPatchCells, last_x);
            for (int iz = first_z + row * kPatchCells; iz <= end_z; ++iz) {
                const float* heights = &map.heights[static_cast<size_t>(iz) * static_cast<size_t>(map.width)];
                for (int ix = first_x + column * kPatchCells; ix <= end_x; ++ix) {
                    lowest = glm::min(lowest, heights[ix]);
                    highest = glm::max(highest, heights[ix]);
                }
            }
            const size_t index = static_cast<size_t>(row) * static_cast<size_t>(finest.columns) + static_cast<size_t>(column);
            finest.lowest[index] = lowest;
            finest.highest[index] = highest;
        }
    }
    levels.push_back(std::move(finest));
    // Larger squares: each covers four of the level below.
    while ((levels.back().columns > 1 || levels.back().rows > 1) && static_cast<int>(levels.size()) < max_levels) {
        const LevelRanges& below = levels.back();
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
        levels.push_back(std::move(above));
    }
    return levels;
}

// One patch of a streamed terrain.
struct HeldPatch {
    int page = 0;
    HeightMap heights;  // window of the whole map with a rim of one point, map coordinates
    std::vector<LevelRanges> levels;
};

} // namespace

struct Terrain::Impl {
    Renderer* renderer = nullptr;
    HeightMap map;  // the whole map, or for a streamed terrain the background
    TerrainConfig config;
    TerrainHandle handle;
    // Index 0: squares of the smallest size; each further level has squares twice as large.
    std::vector<LevelRanges> levels;
    std::vector<TerrainPatch> patches;
    TerrainStats stats;
    const TerrainOcclusion* occlusion = nullptr;
    std::vector<TerrainPatch> hidden;  // scratch: the squares a hill hides

    // Draws the squares, those a hill hides as shadow casters only.
    void DrawSquares(TerrainHandle terrain, const TerrainMaterial& material, std::vector<TerrainPatch>& squares) {
        hidden.clear();
        if (occlusion != nullptr) {
            size_t kept = 0;
            for (const TerrainPatch& square : squares) {
                if (occlusion->IsHidden(glm::dvec3(square.x, square.min_height, square.z),
                                        glm::dvec3(square.x + square.size, square.max_height, square.z + square.size))) {
                    hidden.push_back(square);
                } else {
                    squares[kept++] = square;
                }
            }
            squares.resize(kept);
            stats.patches_hidden += static_cast<int>(hidden.size());
        }
        if (!squares.empty()) {
            renderer->DrawTerrain(terrain, material, squares.data(), static_cast<int>(squares.size()));
        }
        if (!hidden.empty()) {
            InstanceOptions shadow_only;
            shadow_only.draw_in_view = false;
            renderer->DrawTerrain(terrain, material, hidden.data(), static_cast<int>(hidden.size()), shadow_only);
        }
    }

    // --- Streamed ---
    bool streamed = false;
    double patch_size = 0.0;
    int patch_points = 0;  // height points along a patch's edge, without the shared last one
    int patch_level = 0;   // level of a whole patch: patch_points = 64 << patch_level
    int page_points = 0;   // patch_points + 1 + a rim of one point on both sides
    float spacing = 1.0f;  // meters between height points
    glm::vec2 land_size{0.0f};
    std::map<PatchCoord, HeldPatch> held;
    std::vector<int> free_pages;
    // The far background: the coarse heights brought to a point every patch_size / 64
    // meters, so that its smallest square is one patch, on a terrain of its own.
    TerrainHandle background_handle;
    HeightMap background_drawn;
    std::vector<LevelRanges> background_levels;
    TerrainMaterial background_material;
    std::vector<TerrainPatch> background_patches;
    bool background_visible = true;

    // Squares of the background where no patch is held: a square with held patches inside
    // is split, down to single patches, which are left out.
    void SelectBackground(int level, int column, int row, const glm::dvec3& eye) {
        const LevelRanges& ranges = background_levels[static_cast<size_t>(level)];
        if (column >= ranges.columns || row >= ranges.rows) {
            return;
        }
        const int first_x = column << level;
        const int first_z = row << level;
        const int end_x = (column + 1) << level;
        const int end_z = (row + 1) << level;
        bool holds = false;
        for (const auto& [coord, patch] : held) {
            if (coord.x >= first_x && coord.x < end_x && coord.z >= first_z && coord.z < end_z) {
                holds = true;
                break;
            }
        }
        const float size = static_cast<float>(patch_size) * static_cast<float>(1 << level);
        const size_t index = static_cast<size_t>(row) * static_cast<size_t>(ranges.columns) + static_cast<size_t>(column);
        TerrainPatch square;
        square.x = config.origin.x + static_cast<double>(column) * static_cast<double>(size);
        square.z = config.origin.z + static_cast<double>(row) * static_cast<double>(size);
        square.size = size;
        square.min_height = static_cast<float>(config.origin.y + static_cast<double>(ranges.lowest[index]));
        square.max_height = static_cast<float>(config.origin.y + static_cast<double>(ranges.highest[index]));
        bool split = holds;
        if (!split && level > 0) {
            const glm::dvec3 nearest = glm::clamp(eye, glm::dvec3(square.x, square.min_height, square.z),
                                                  glm::dvec3(square.x + size, square.max_height, square.z + size));
            split = glm::distance(eye, nearest) < static_cast<double>(size * config.detail);
        }
        if (split) {
            if (level == 0) {
                return;  // a held patch: drawn from its page
            }
            for (int quarter = 0; quarter < 4; ++quarter) {
                SelectBackground(level - 1, column * 2 + (quarter & 1), row * 2 + (quarter >> 1), eye);
            }
            return;
        }
        background_patches.push_back(square);
        ++stats.background_patches;
    }

    float GetSquareSize(int level, float point_spacing) const { return static_cast<float>(kPatchCells << level) * point_spacing; }

    // Adds the square (column, row) of the given level, or its four quarters if the camera is
    // close. `corner` is the world position of the levels' first point, `page` the page drawn.
    void Select(const std::vector<LevelRanges>& ranges_by_level, const glm::dvec3& corner, float point_spacing, int page, int level,
                int column, int row, const glm::dvec3& eye) {
        const LevelRanges& ranges = ranges_by_level[static_cast<size_t>(level)];
        if (column >= ranges.columns || row >= ranges.rows) {
            return;  // wholly beyond the map's edge
        }
        const size_t index = static_cast<size_t>(row) * static_cast<size_t>(ranges.columns) + static_cast<size_t>(column);
        const float size = GetSquareSize(level, point_spacing);
        TerrainPatch patch;
        patch.x = corner.x + static_cast<double>(column) * static_cast<double>(size);
        patch.z = corner.z + static_cast<double>(row) * static_cast<double>(size);
        patch.size = size;
        patch.min_height = static_cast<float>(corner.y + static_cast<double>(ranges.lowest[index]));
        patch.max_height = static_cast<float>(corner.y + static_cast<double>(ranges.highest[index]));
        patch.page = page;

        if (level > 0) {
            // Distance from the camera to the nearest point of the box around the square's land.
            const glm::dvec3 nearest = glm::clamp(eye, glm::dvec3(patch.x, patch.min_height, patch.z),
                                                  glm::dvec3(patch.x + size, patch.max_height, patch.z + size));
            if (glm::distance(eye, nearest) < static_cast<double>(size * config.detail)) {
                for (int quarter = 0; quarter < 4; ++quarter) {
                    Select(ranges_by_level, corner, point_spacing, page, level - 1, column * 2 + (quarter & 1), row * 2 + (quarter >> 1), eye);
                }
                return;
            }
        }
        patches.push_back(patch);
        ++stats.patches;
        ++stats.patches_per_level[level];
    }

    // The held patch whose land holds the map position, or nullptr.
    const HeldPatch* FindHeld(float map_x, float map_z) const {
        const PatchCoord patch{static_cast<int>(std::floor(static_cast<double>(map_x) / patch_size)),
                               static_cast<int>(std::floor(static_cast<double>(map_z) / patch_size))};
        const auto found = held.find(patch);
        return found != held.end() ? &found->second : nullptr;
    }

    // The heights to read at a map position: its held patch, or else the whole map (the
    // background for a streamed terrain).
    const HeightMap& HeightsAt(float map_x, float map_z) const {
        if (streamed) {
            if (const HeldPatch* patch = FindHeld(map_x, map_z)) {
                return patch->heights;
            }
        }
        return map;
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
    impl.levels = BuildLevels(map, 0, 0, map.width - 1, map.depth - 1, kMaxLevels);
}

Terrain::Terrain(Renderer& renderer, const LandInfo& land, const TerrainConfig& config, int pages) : impl_(std::make_unique<Impl>()) {
    Impl& impl = *impl_;
    const double points = land.grid.patch_size / static_cast<double>(land.spacing);
    impl.patch_points = static_cast<int>(std::lround(points));
    impl.patch_level = 0;
    while ((kPatchCells << impl.patch_level) < impl.patch_points) {
        ++impl.patch_level;
    }
    if (std::fabs(points - static_cast<double>(impl.patch_points)) > 1e-6 || (kPatchCells << impl.patch_level) != impl.patch_points ||
        pages < 1 || land.background.IsEmpty()) {
        FatalError("Terrain: a streamed terrain needs patches of 64, 128, 256... height points, at least one page and background heights");
    }
    impl.renderer = &renderer;
    impl.streamed = true;
    impl.config = config;
    impl.config.origin = land.world_origin;
    impl.map = land.background;
    impl.patch_size = land.grid.patch_size;
    impl.page_points = impl.patch_points + 3;
    impl.land_size = {land.size_x, land.size_z};
    impl.spacing = land.spacing;
    const int weight_points = static_cast<int>(std::lround(land.grid.patch_size / static_cast<double>(land.map_spacing))) + 1;
    impl.handle = renderer.CreateTerrainPages(impl.page_points, pages, land.spacing, weight_points, 1);
    for (int page = pages - 1; page >= 0; --page) {
        impl.free_pages.push_back(page);
    }

    // The background, brought to 64 cells per patch, blended from the coarse points.
    const HeightMap& coarse = land.background;
    const float drawn_spacing = static_cast<float>(land.grid.patch_size / static_cast<double>(kPatchCells));
    const int ratio = std::max(static_cast<int>(std::lround(coarse.spacing / drawn_spacing)), 1);
    HeightMap& drawn = impl.background_drawn;
    drawn.spacing = coarse.spacing / static_cast<float>(ratio);
    drawn.width = (coarse.width - 1) * ratio + 1;
    drawn.depth = (coarse.depth - 1) * ratio + 1;
    drawn.heights.resize(static_cast<size_t>(drawn.width) * static_cast<size_t>(drawn.depth));
    for (int z = 0; z < drawn.depth; ++z) {
        for (int x = 0; x < drawn.width; ++x) {
            drawn.heights[static_cast<size_t>(z) * static_cast<size_t>(drawn.width) + static_cast<size_t>(x)] =
                coarse.GetHeight(static_cast<float>(x) * drawn.spacing, static_cast<float>(z) * drawn.spacing);
        }
    }
    impl.background_handle = renderer.CreateTerrain(drawn.heights.data(), drawn.width, drawn.depth, drawn.spacing, impl.config.origin);
    impl.background_levels = BuildLevels(drawn, 0, 0, drawn.width - 1, drawn.depth - 1, kMaxLevels);
    impl.background_material = impl.config.material;
    if (!land.background_weights[0].pixels.empty()) {
        TextureOptions plain;
        plain.is_color = false;
        plain.generate_mipmaps = false;
        impl.background_material.weights[0] = renderer.CreateTexture(land.background_weights[0], plain);
        impl.background_material.weights[1] = renderer.CreateTexture(land.background_weights[1], plain);
    }
}

Terrain::~Terrain() {
    impl_->renderer->DestroyTerrain(impl_->handle);
    if (impl_->background_handle.id != 0) {
        impl_->renderer->DestroyTerrain(impl_->background_handle);
    }
}

void Terrain::SetBackgroundVisible(bool visible) {
    impl_->background_visible = visible;
}

void Terrain::SetPatch(PatchCoord patch, const HeightMap& heights, const Image* weights) {
    Impl& impl = *impl_;
    if (!impl.streamed) {
        FatalError("Terrain::SetPatch: only a streamed terrain takes patches");
    }
    if (impl.held.count(patch) != 0) {
        RemovePatch(patch);
    }
    if (impl.free_pages.empty()) {
        FatalError("Terrain::SetPatch: every page is in use; make the terrain with more pages");
    }
    // The page: the patch's points and a rim of one around them, in the whole map's points;
    // beyond the window (the land's edge) the nearest point repeats, as the whole map does.
    HeldPatch held;
    held.page = impl.free_pages.back();
    held.heights = heights;
    const int first_x = patch.x * impl.patch_points - 1;
    const int first_z = patch.z * impl.patch_points - 1;
    const int size = impl.page_points;
    std::vector<float> page(static_cast<size_t>(size) * static_cast<size_t>(size));
    for (int z = 0; z < size; ++z) {
        const int from_z = glm::clamp(first_z + z, heights.first_z, heights.first_z + heights.depth - 1) - heights.first_z;
        for (int x = 0; x < size; ++x) {
            const int from_x = glm::clamp(first_x + x, heights.first_x, heights.first_x + heights.width - 1) - heights.first_x;
            page[static_cast<size_t>(z) * static_cast<size_t>(size) + static_cast<size_t>(x)] =
                heights.heights[static_cast<size_t>(from_z) * static_cast<size_t>(heights.width) + static_cast<size_t>(from_x)];
        }
    }
    // Points of the patch that hold land (fewer at the land's far edges), and the squares over them.
    const float spacing = heights.spacing;
    const int land_x = glm::min(impl.patch_points, static_cast<int>(std::lround(impl.land_size.x / spacing)) - patch.x * impl.patch_points);
    const int land_z = glm::min(impl.patch_points, static_cast<int>(std::lround(impl.land_size.y / spacing)) - patch.z * impl.patch_points);
    if (land_x < 1 || land_z < 1) {
        FatalError("Terrain::SetPatch: the patch lies outside the land");
    }
    HeightMap page_map;
    page_map.width = size;
    page_map.depth = size;
    page_map.spacing = spacing;
    page_map.heights = page;
    held.levels = BuildLevels(page_map, 1, 1, 1 + land_x, 1 + land_z, impl.patch_level + 1);
    const glm::dvec3 page_origin = impl.config.origin + glm::dvec3(static_cast<double>(first_x) * spacing, 0.0,
                                                                 static_cast<double>(first_z) * spacing);
    impl.renderer->SetTerrainPage(impl.handle, held.page, page.data(), page_origin, weights, land_x + 2, land_z + 2);
    impl.free_pages.pop_back();
    impl.held[patch] = std::move(held);
}

void Terrain::RemovePatch(PatchCoord patch) {
    Impl& impl = *impl_;
    const auto found = impl.held.find(patch);
    if (found == impl.held.end()) {
        return;
    }
    impl.free_pages.push_back(found->second.page);
    impl.held.erase(found);
}

bool Terrain::HasPatch(PatchCoord patch) const {
    return impl_->held.count(patch) != 0;
}

int Terrain::GetPatchCount() const {
    return static_cast<int>(impl_->held.size());
}

void Terrain::Draw(const Camera& camera) {
    Impl& impl = *impl_;
    impl.patches.clear();
    impl.stats = TerrainStats{};
    if (impl.streamed) {
        // Each held patch is a tree of squares of its own, in patch order (the same every frame).
        for (const auto& [coord, held] : impl.held) {
            // The levels start at the page's point 1, the patch's first point.
            const glm::dvec3 corner = impl.config.origin + glm::dvec3(static_cast<double>(coord.x) * impl.patch_size, 0.0,
                                                                      static_cast<double>(coord.z) * impl.patch_size);
            impl.Select(held.levels, corner, impl.spacing, held.page, static_cast<int>(held.levels.size()) - 1, 0, 0, camera.position);
        }
        impl.background_patches.clear();
        if (impl.background_visible) {
            const int top = static_cast<int>(impl.background_levels.size()) - 1;
            const LevelRanges& roots = impl.background_levels[static_cast<size_t>(top)];
            for (int row = 0; row < roots.rows; ++row) {
                for (int column = 0; column < roots.columns; ++column) {
                    impl.SelectBackground(top, column, row, camera.position);
                }
            }
        }
        if (!impl.background_patches.empty()) {
            // Keep the ground layer textures of the material in step with the patches'.
            TerrainMaterial look = impl.config.material;
            look.weights[0] = impl.background_material.weights[0];
            look.weights[1] = impl.background_material.weights[1];
            impl.DrawSquares(impl.background_handle, look, impl.background_patches);
        }
    } else {
        const int top = static_cast<int>(impl.levels.size()) - 1;
        const LevelRanges& roots = impl.levels[static_cast<size_t>(top)];
        for (int row = 0; row < roots.rows; ++row) {
            for (int column = 0; column < roots.columns; ++column) {
                impl.Select(impl.levels, impl.config.origin, impl.map.spacing, 0, top, column, row, camera.position);
            }
        }
    }
    impl.DrawSquares(impl.handle, impl.config.material, impl.patches);
}

void Terrain::SetOcclusion(const TerrainOcclusion* occlusion) {
    impl_->occlusion = occlusion;
}

// The height map counts in float from its own first point; world positions are moved there in double.
double Terrain::GetHeight(double x, double z) const {
    const float map_x = static_cast<float>(x - impl_->config.origin.x);
    const float map_z = static_cast<float>(z - impl_->config.origin.z);
    return impl_->config.origin.y + static_cast<double>(impl_->HeightsAt(map_x, map_z).GetHeight(map_x, map_z));
}

glm::vec3 Terrain::GetNormal(double x, double z) const {
    const float map_x = static_cast<float>(x - impl_->config.origin.x);
    const float map_z = static_cast<float>(z - impl_->config.origin.z);
    return impl_->HeightsAt(map_x, map_z).GetNormal(map_x, map_z);
}

float Terrain::GetSlopeDegrees(double x, double z) const {
    const float map_x = static_cast<float>(x - impl_->config.origin.x);
    const float map_z = static_cast<float>(z - impl_->config.origin.z);
    return impl_->HeightsAt(map_x, map_z).GetSlopeDegrees(map_x, map_z);
}

glm::dvec2 Terrain::GetMin() const {
    return {impl_->config.origin.x, impl_->config.origin.z};
}

glm::dvec2 Terrain::GetMax() const {
    if (impl_->streamed) {
        return {impl_->config.origin.x + impl_->land_size.x, impl_->config.origin.z + impl_->land_size.y};
    }
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
            // Positions of the whole maps, so that a window of them gives the same weights.
            const float px = static_cast<float>(maps.map_first_x + x) * maps.map_spacing;
            const float pz = static_cast<float>(maps.map_first_z + z) * maps.map_spacing;
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

const std::vector<std::string>& GetGroundLayerWords() {
    static const std::vector<std::string> words = {"ground", "tile", "strength", "patch", "slope", "height", "moisture", "sediment",
                                                   "rock", "flow", "water_distance", "water_depth"};
    return words;
}

bool ParseGroundLayers(const std::vector<std::pair<int, std::string>>& lines, std::vector<GroundLayer>& layers, std::string& error) {
    std::vector<GroundLayer> parsed;
    bool in_plant_rule = false;  // a "plant" or "cover" block of the same file (poligon/world/plant_rules.h)
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
            in_plant_rule = true;
            continue;
        }
        if (key == "ground") {
            in_plant_rule = false;
            if (values.size() != 1) {
                return fail("\"ground\" takes the name of the layer's textures");
            }
            if (parsed.size() >= static_cast<size_t>(kMaxTerrainLayers)) {
                return fail("at most 8 ground layers");
            }
            GroundLayer layer;
            layer.texture = values[0];
            layer.line = line_number;
            parsed.push_back(layer);
            continue;
        }
        if (in_plant_rule) {
            continue;
        }
        if (parsed.empty()) {
            return fail("\"" + key + "\" before any ground layer");
        }
        GroundLayer& layer = parsed.back();
        char* end = nullptr;
        const auto number = [&](size_t index, float& out) {
            out = std::strtof(values[index].c_str(), &end);
            return *end == '\0';
        };
        if (TerrainRange* range = FindMapCondition(layer.rule.conditions, key)) {
            if (!ReadTerrainRange(values, *range)) {
                return fail("\"" + key + "\" takes low, high and fade (low not above high)");
            }
        } else if (key == "tile" || key == "strength") {
            float value = 0.0f;
            if (values.size() != 1 || !number(0, value) || value <= 0.0f) {
                return fail("\"" + key + "\" takes one number above zero");
            }
            (key == "tile" ? layer.tile_size : layer.rule.strength) = value;
        } else if (key == "patch") {
            if (values.size() != 2 || !number(0, layer.rule.patch_size) || !number(1, layer.rule.patch_cover) ||
                layer.rule.patch_cover < 0.0f || layer.rule.patch_cover > 1.0f) {
                return fail("\"patch\" takes a size in meters and the share of land covered (0 to 1)");
            }
        } else {
            return fail("unknown word \"" + key + "\" in a ground layer");
        }
    }
    layers = std::move(parsed);
    return true;
}

namespace {

// What TerrainPatchSource and TerrainPhysicsSource load: the patch's land, kept by the LandSource too.
struct LandPatchData : PatchData {
    std::shared_ptr<const LandPatch> land;
};

} // namespace

TerrainPatchSource::TerrainPatchSource(Terrain& terrain, LandSource& land) : terrain_(terrain), land_(land) {}

std::unique_ptr<PatchData> TerrainPatchSource::Load(PatchCoord patch) {
    auto data = std::make_unique<LandPatchData>();
    data->land = land_.GetPatch(patch);
    return data;
}

void TerrainPatchSource::Attach(PatchCoord patch, std::unique_ptr<PatchData> data) {
    const LandPatch& land = *static_cast<LandPatchData&>(*data).land;
    terrain_.SetPatch(patch, land.maps.height, land.weights[0].pixels.empty() ? nullptr : land.weights);
}

void TerrainPatchSource::Detach(PatchCoord patch) {
    terrain_.RemovePatch(patch);
}

struct TerrainPhysicsSource::Impl {
    PhysicsWorld* physics = nullptr;
    LandSource* land = nullptr;
    float friction = 0.5f;
    std::map<PatchCoord, BodyHandle> bodies;
};

TerrainPhysicsSource::TerrainPhysicsSource(PhysicsWorld& physics, LandSource& land, float friction) : impl_(std::make_shared<Impl>()) {
    impl_->physics = &physics;
    impl_->land = &land;
    impl_->friction = friction;
}

std::unique_ptr<PatchData> TerrainPhysicsSource::Load(PatchCoord patch) {
    auto data = std::make_unique<LandPatchData>();
    data->land = impl_->land->GetPatch(patch);
    return data;
}

void TerrainPhysicsSource::Attach(PatchCoord patch, std::unique_ptr<PatchData> data) {
    const LandInfo& info = impl_->land->GetInfo();
    const HeightMap& window = static_cast<LandPatchData&>(*data).land->maps.height;
    // The patch's points and the shared last row and column; beyond the land's far edges the
    // edge repeats (a height field is square).
    const int points = static_cast<int>(std::lround(info.grid.patch_size / static_cast<double>(info.spacing))) + 1;
    const int first_x = patch.x * (points - 1);
    const int first_z = patch.z * (points - 1);
    std::vector<float> heights(static_cast<size_t>(points) * static_cast<size_t>(points));
    for (int z = 0; z < points; ++z) {
        const int from_z = glm::clamp(first_z + z, window.first_z, window.first_z + window.depth - 1) - window.first_z;
        for (int x = 0; x < points; ++x) {
            const int from_x = glm::clamp(first_x + x, window.first_x, window.first_x + window.width - 1) - window.first_x;
            heights[static_cast<size_t>(z) * static_cast<size_t>(points) + static_cast<size_t>(x)] =
                window.heights[static_cast<size_t>(from_z) * static_cast<size_t>(window.width) + static_cast<size_t>(from_x)];
        }
    }
    const glm::dvec3 corner = info.world_origin + glm::dvec3(static_cast<double>(first_x) * info.spacing, 0.0,
                                                             static_cast<double>(first_z) * info.spacing);
    impl_->bodies[patch] = impl_->physics->AddHeightField(heights.data(), points, info.spacing, corner, impl_->friction);
}

void TerrainPhysicsSource::Detach(PatchCoord patch) {
    const auto found = impl_->bodies.find(patch);
    if (found != impl_->bodies.end()) {
        impl_->physics->RemoveBody(found->second);
        impl_->bodies.erase(found);
    }
}

int TerrainPhysicsSource::GetBodyCount() const {
    return static_cast<int>(impl_->bodies.size());
}

} // namespace poligon

#include "poligon/ground_cover.h"

#include <cmath>
#include <cstdint>
#include <map>
#include <utility>

#include "poligon/fatal_error.h"

namespace poligon {

namespace {

// A small, fast random number generator (PCG), seeded per cell and layer so that a cell's
// plants do not depend on the order in which cells were built.
class Random {
public:
    explicit Random(std::uint32_t seed) : state_(seed * 747796405u + 2891336453u) { Next(); }

    float Next() {
        state_ = state_ * 747796405u + 2891336453u;
        std::uint32_t word = ((state_ >> ((state_ >> 28u) + 4u)) ^ state_) * 277803737u;
        word = (word >> 22u) ^ word;
        return static_cast<float>(word >> 8) / 16777216.0f;
    }

    float Between(float low, float high) { return low + (high - low) * Next(); }

private:
    std::uint32_t state_;
};

std::uint32_t Hash(std::int32_t a, std::int32_t b, std::int32_t c) {
    std::uint32_t h = static_cast<std::uint32_t>(a) * 73856093u ^ static_cast<std::uint32_t>(b) * 19349663u ^
                      static_cast<std::uint32_t>(c) * 83492791u;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    return h;
}

// Smooth noise in 0..1 with features about one unit across: random values at whole
// coordinates, blended in between. The same function as in terrain.frag.hlsl in spirit;
// it decides where a layer's patches lie.
float ValueNoise(float x, float y) {
    const float cell_x = std::floor(x);
    const float cell_y = std::floor(y);
    float fx = x - cell_x;
    float fy = y - cell_y;
    fx = fx * fx * (3.0f - 2.0f * fx);
    fy = fy * fy * (3.0f - 2.0f * fy);
    const auto value = [](float cx, float cy) {
        const std::uint32_t h = Hash(static_cast<std::int32_t>(cx), static_cast<std::int32_t>(cy), 7);
        return static_cast<float>(h >> 8) / 16777216.0f;
    };
    const float top = glm::mix(value(cell_x, cell_y), value(cell_x + 1.0f, cell_y), fx);
    const float bottom = glm::mix(value(cell_x, cell_y + 1.0f), value(cell_x + 1.0f, cell_y + 1.0f), fx);
    // Blending pulls values towards the middle; spreading them out again gives patches
    // with clear insides and clear gaps instead of a uniform haze.
    return glm::smoothstep(0.25f, 0.75f, glm::mix(top, bottom, fy));
}

struct Cell {
    // Transforms per layer.
    std::vector<std::vector<glm::mat4>> layers;
    int instance_count = 0;
};

} // namespace

struct GroundCover::Impl {
    const Terrain* terrain = nullptr;
    float radius = 80.0f;
    float cell_size = 16.0f;
    float density_scale = 1.0f;
    const TerrainMaps* maps = nullptr;
    glm::vec2 world_to_map{0.0f};
    std::vector<GroundCoverLayer> layers;
    std::vector<int> species_ids;
    Vegetation vegetation{32.0f};
    std::map<std::pair<int, int>, Cell> cells;
    GroundCoverStats stats;
    bool has_camera = false;
    std::pair<int, int> camera_cell{0, 0};

    Cell Build(int column, int row) const {
        Cell cell;
        cell.layers.resize(layers.size());
        const glm::vec2 corner(static_cast<float>(column) * cell_size, static_cast<float>(row) * cell_size);
        for (size_t layer_index = 0; layer_index < layers.size(); ++layer_index) {
            const GroundCoverLayer& layer = layers[layer_index];
            Random random(Hash(column, row, static_cast<int>(layer_index) * 977 + layer.seed));
            // The expected count, rounded at random so that fractions average out.
            const float expected = layer.density * density_scale * cell_size * cell_size;
            int count = static_cast<int>(expected);
            if (random.Next() < expected - static_cast<float>(count)) {
                ++count;
            }
            std::vector<glm::mat4>& placed = cell.layers[layer_index];
            for (int i = 0; i < count; ++i) {
                const float x = corner.x + random.Between(0.0f, cell_size);
                const float z = corner.y + random.Between(0.0f, cell_size);
                const float turn = random.Between(0.0f, 6.2831853f);
                const float scale = random.Between(layer.scale_min, layer.scale_max);
                const float patch_roll = random.Next();
                // Drawn only with maps, so that cover without them is placed as before.
                const float map_roll = maps != nullptr ? random.Next() : 0.0f;
                // Every random number is drawn before any rule, so that a plant rejected by a
                // rule does not shift the plants after it.
                if (layer.patch_threshold > 0.0f) {
                    const float noise = ValueNoise(x / layer.patch_size + static_cast<float>(layer.seed) * 3.7f,
                                                   z / layer.patch_size - static_cast<float>(layer.seed) * 1.3f);
                    const float share = glm::clamp((noise - layer.patch_threshold) / glm::max(layer.patch_softness, 0.001f) + 1.0f,
                                                   0.0f, 1.0f);
                    if (patch_roll >= share) {
                        continue;
                    }
                }
                if (terrain->GetSlopeDegrees(x, z) > layer.max_slope_degrees) {
                    continue;
                }
                if (maps != nullptr && map_roll >= layer.conditions.Weight(*maps, glm::vec2(x, z) + world_to_map)) {
                    continue;
                }
                const float height = terrain->GetHeight(x, z);
                if (height < layer.min_height || height > layer.max_height) {
                    continue;
                }
                glm::mat4 transform = glm::translate(glm::mat4(1.0f), {x, height - layer.sink_meters, z});
                if (layer.align_to_ground) {
                    const glm::vec3 normal = terrain->GetNormal(x, z);
                    const glm::vec3 up(0.0f, 1.0f, 0.0f);
                    const float cosine = glm::clamp(glm::dot(up, normal), -1.0f, 1.0f);
                    if (cosine < 0.9999f) {
                        transform = transform * glm::rotate(glm::mat4(1.0f), std::acos(cosine), glm::normalize(glm::cross(up, normal)));
                    }
                }
                transform = transform * glm::rotate(glm::mat4(1.0f), turn, {0.0f, 1.0f, 0.0f}) *
                            glm::scale(glm::mat4(1.0f), glm::vec3(scale));
                placed.push_back(transform);
            }
            cell.instance_count += static_cast<int>(placed.size());
        }
        return cell;
    }

    // Hands every living cell's plants to the vegetation system again.
    void Refill() {
        vegetation.ClearInstances();
        stats.instances_alive = 0;
        for (const auto& [key, cell] : cells) {
            for (size_t layer_index = 0; layer_index < cell.layers.size(); ++layer_index) {
                for (const glm::mat4& transform : cell.layers[layer_index]) {
                    vegetation.AddInstance(species_ids[layer_index], transform);
                }
            }
            stats.instances_alive += cell.instance_count;
        }
        stats.cells_alive = static_cast<int>(cells.size());
    }
};

GroundCover::GroundCover(const Terrain& terrain, float radius, float cell_size) : impl_(std::make_unique<Impl>()) {
    if (radius <= 0.0f || cell_size <= 0.0f) {
        FatalError("GroundCover: radius and cell size must be above zero");
    }
    impl_->terrain = &terrain;
    impl_->radius = radius;
    impl_->cell_size = cell_size;
}

GroundCover::~GroundCover() = default;

int GroundCover::AddLayer(const GroundCoverLayer& layer) {
    if (!impl_->cells.empty()) {
        FatalError("GroundCover::AddLayer: add layers before the first Update");
    }
    if (layer.density < 0.0f || layer.scale_min <= 0.0f || layer.scale_max < layer.scale_min) {
        FatalError("GroundCover::AddLayer: density must not be negative and scales must be above zero, min at most max");
    }
    impl_->layers.push_back(layer);
    impl_->species_ids.push_back(impl_->vegetation.AddSpecies(layer.species));
    return static_cast<int>(impl_->layers.size()) - 1;
}

void GroundCover::Update(const glm::vec3& camera_position) {
    Impl& impl = *impl_;
    impl.stats.cells_built = 0;
    const std::pair<int, int> camera_cell{static_cast<int>(std::floor(camera_position.x / impl.cell_size)),
                                          static_cast<int>(std::floor(camera_position.z / impl.cell_size))};
    if (impl.has_camera && camera_cell == impl.camera_cell) {
        return;
    }
    impl.has_camera = true;
    impl.camera_cell = camera_cell;

    // The cells whose nearest point lies within the radius of the camera.
    const glm::vec2 eye(camera_position.x, camera_position.z);
    const int reach = static_cast<int>(std::ceil(impl.radius / impl.cell_size));
    bool changed = false;
    std::map<std::pair<int, int>, Cell> kept;
    for (int row = camera_cell.second - reach; row <= camera_cell.second + reach; ++row) {
        for (int column = camera_cell.first - reach; column <= camera_cell.first + reach; ++column) {
            const glm::vec2 low(static_cast<float>(column) * impl.cell_size, static_cast<float>(row) * impl.cell_size);
            const glm::vec2 nearest = glm::clamp(eye, low, low + impl.cell_size);
            if (glm::distance(eye, nearest) > impl.radius) {
                continue;
            }
            const std::pair<int, int> key{column, row};
            auto found = impl.cells.find(key);
            if (found != impl.cells.end()) {
                kept.insert(impl.cells.extract(found));
            } else {
                kept.emplace(key, impl.Build(column, row));
                ++impl.stats.cells_built;
                changed = true;
            }
        }
    }
    changed = changed || !impl.cells.empty();  // anything left behind was dropped
    impl.cells = std::move(kept);
    if (changed) {
        impl.Refill();
    }
}

void GroundCover::Draw(Renderer& renderer, const Camera& camera) {
    impl_->vegetation.Draw(renderer, camera);
}

void GroundCover::SetRadius(float radius) {
    if (radius <= 0.0f) {
        FatalError("GroundCover::SetRadius: the radius must be above zero");
    }
    impl_->radius = radius;
    impl_->has_camera = false;  // the next Update rebuilds the set of cells
}

void GroundCover::SetDensityScale(float scale) {
    if (scale < 0.0f) {
        FatalError("GroundCover::SetDensityScale: the scale must not be negative");
    }
    impl_->density_scale = scale;
    impl_->cells.clear();
    impl_->has_camera = false;  // the next Update builds every cell again
}

void GroundCover::SetMaps(const TerrainMaps* maps, const glm::vec2& world_to_map) {
    impl_->maps = maps;
    impl_->world_to_map = world_to_map;
    impl_->cells.clear();
    impl_->has_camera = false;
}

float GroundCover::GetRadius() const {
    return impl_->radius;
}

void GroundCover::CollectInstances(int layer, std::vector<glm::mat4>& out) const {
    if (layer < 0 || layer >= static_cast<int>(impl_->layers.size())) {
        FatalError("GroundCover::CollectInstances: no such layer");
    }
    for (const auto& [key, cell] : impl_->cells) {
        const std::vector<glm::mat4>& placed = cell.layers[static_cast<size_t>(layer)];
        out.insert(out.end(), placed.begin(), placed.end());
    }
}

const GroundCoverStats& GroundCover::GetStats() const {
    return impl_->stats;
}

const Vegetation& GroundCover::GetVegetation() const {
    return impl_->vegetation;
}

Vegetation& GroundCover::GetVegetation() {
    return impl_->vegetation;
}

} // namespace poligon

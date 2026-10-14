#include "poligon/vegetation.h"

#include <cmath>
#include <map>
#include <utility>

#include "poligon/fatal_error.h"

namespace poligon {

namespace {

constexpr int kMaxLevels = 8;

// The plants of one species in one cell.
struct CellSpecies {
    int species;
    std::vector<glm::mat4> transforms;
};

struct Cell {
    // Box around the feet of the cell's plants; only x and z are used.
    glm::vec2 min{1.0e30f};
    glm::vec2 max{-1.0e30f};
    std::vector<CellSpecies> species;
};

// The transforms collected during one Draw for one species and level, split by whether
// the plants are within shadow range.
struct LevelLists {
    std::vector<glm::mat4> with_shadow;
    std::vector<glm::mat4> without_shadow;
};

struct SpeciesData {
    VegetationSpecies species;
    // Level distances and shadow distance after SetDistanceScale.
    float level_distance[kMaxLevels] = {};
    float shadow_distance = 0.0f;
    LevelLists lists[kMaxLevels];
};

} // namespace

struct Vegetation::Impl {
    float cell_size = 32.0f;
    std::vector<SpeciesData> species;
    // Ordered by cell coordinates, so that cells are always visited in the same order.
    std::map<std::pair<int, int>, Cell> cells;
    int instance_count = 0;
    float detail_scale = 1.0f;
    float shadow_scale = 1.0f;
    VegetationStats stats;
    // Scratch for parts whose local transform is not the identity.
    std::vector<glm::mat4> part_transforms;

    void ApplyScales(SpeciesData& data) const {
        for (size_t i = 0; i < data.species.levels.size(); ++i) {
            data.level_distance[i] = data.species.levels[i].max_distance * detail_scale;
        }
        data.shadow_distance = data.species.shadow_distance * shadow_scale;
    }
};

Vegetation::Vegetation(float cell_size) : impl_(std::make_unique<Impl>()) {
    if (cell_size <= 0.0f) {
        FatalError("Vegetation: the cell size must be above zero");
    }
    impl_->cell_size = cell_size;
}

Vegetation::~Vegetation() = default;

int Vegetation::AddSpecies(const VegetationSpecies& species) {
    if (species.levels.empty() || species.levels.size() > kMaxLevels) {
        FatalError("Vegetation::AddSpecies: a species needs between 1 and 8 levels");
    }
    float previous = 0.0f;
    for (const VegetationLevel& level : species.levels) {
        if (level.max_distance <= previous) {
            FatalError("Vegetation::AddSpecies: every level must reach further than the one before it");
        }
        previous = level.max_distance;
    }
    SpeciesData data;
    data.species = species;
    impl_->ApplyScales(data);
    impl_->species.push_back(std::move(data));
    return static_cast<int>(impl_->species.size()) - 1;
}

void Vegetation::AddInstance(int species, const glm::mat4& transform) {
    if (species < 0 || species >= static_cast<int>(impl_->species.size())) {
        FatalError("Vegetation::AddInstance: no such species");
    }
    const glm::vec2 foot{transform[3].x, transform[3].z};
    const std::pair<int, int> key{static_cast<int>(std::floor(foot.x / impl_->cell_size)),
                                  static_cast<int>(std::floor(foot.y / impl_->cell_size))};
    Cell& cell = impl_->cells[key];
    cell.min = glm::min(cell.min, foot);
    cell.max = glm::max(cell.max, foot);
    for (CellSpecies& entry : cell.species) {
        if (entry.species == species) {
            entry.transforms.push_back(transform);
            ++impl_->instance_count;
            return;
        }
    }
    CellSpecies entry;
    entry.species = species;
    entry.transforms.push_back(transform);
    cell.species.push_back(std::move(entry));
    ++impl_->instance_count;
}

void Vegetation::ClearInstances() {
    impl_->cells.clear();
    impl_->instance_count = 0;
}

int Vegetation::GetInstanceCount() const {
    return impl_->instance_count;
}

void Vegetation::SetDistanceScale(float detail_scale, float shadow_scale) {
    if (detail_scale <= 0.0f || shadow_scale < 0.0f) {
        FatalError("Vegetation::SetDistanceScale: the detail scale must be above zero, the shadow scale not negative");
    }
    impl_->detail_scale = detail_scale;
    impl_->shadow_scale = shadow_scale;
    for (SpeciesData& data : impl_->species) {
        impl_->ApplyScales(data);
    }
}

const VegetationStats& Vegetation::GetStats() const {
    return impl_->stats;
}

void Vegetation::Draw(Renderer& renderer, const Camera& camera) {
    Impl& impl = *impl_;
    impl.stats = VegetationStats{};
    impl.stats.instances_total = impl.instance_count;
    for (SpeciesData& data : impl.species) {
        for (LevelLists& lists : data.lists) {
            lists.with_shadow.clear();
            lists.without_shadow.clear();
        }
    }

    // Distances are measured along the ground: a plant seen from a hill top is as detailed
    // as one seen from the same distance on the flat.
    const glm::vec2 eye{camera.position.x, camera.position.z};
    for (const auto& [key, cell] : impl.cells) {
        ++impl.stats.cells_total;
        // Nearest and farthest the cell's plants can be from the camera.
        const glm::vec2 nearest_point = glm::clamp(eye, cell.min, cell.max);
        const float nearest = glm::distance(eye, nearest_point);
        const glm::vec2 farthest_corner{std::fabs(eye.x - cell.min.x) > std::fabs(eye.x - cell.max.x) ? cell.min.x : cell.max.x,
                                        std::fabs(eye.y - cell.min.y) > std::fabs(eye.y - cell.max.y) ? cell.min.y : cell.max.y};
        const float farthest = glm::distance(eye, farthest_corner);

        bool cell_used = false;
        for (const CellSpecies& entry : cell.species) {
            SpeciesData& data = impl.species[static_cast<size_t>(entry.species)];
            const int level_count = static_cast<int>(data.species.levels.size());
            if (nearest > data.level_distance[level_count - 1]) {
                continue;  // every plant of this species in the cell is out of range
            }
            cell_used = true;
            const auto level_at = [&data, level_count](float distance) {
                int level = 0;
                while (level < level_count && distance > data.level_distance[level]) {
                    ++level;
                }
                return level;  // level_count means "not drawn"
            };
            const int nearest_level = level_at(nearest);
            const bool one_level = nearest_level == level_at(farthest);
            const bool one_shadow_answer = (nearest <= data.shadow_distance) == (farthest <= data.shadow_distance);
            if (one_level && one_shadow_answer) {
                // The whole cell falls into one level: hand its plants over in one piece.
                LevelLists& lists = data.lists[nearest_level];
                std::vector<glm::mat4>& list = nearest <= data.shadow_distance ? lists.with_shadow : lists.without_shadow;
                list.insert(list.end(), entry.transforms.begin(), entry.transforms.end());
                continue;
            }
            for (const glm::mat4& transform : entry.transforms) {
                const float distance = glm::distance(eye, glm::vec2(transform[3].x, transform[3].z));
                const int level = level_at(distance);
                if (level == level_count) {
                    continue;
                }
                LevelLists& lists = data.lists[level];
                (distance <= data.shadow_distance ? lists.with_shadow : lists.without_shadow).push_back(transform);
            }
        }
        if (!cell_used) {
            ++impl.stats.cells_skipped;
        }
    }

    // Hands a list to the renderer once per part of the level's meshes.
    const auto submit = [&impl, &renderer](const VegetationLevel& level, const std::vector<glm::mat4>& transforms,
                                           const InstanceOptions& options) {
        if (transforms.empty()) {
            return;
        }
        for (const VegetationPart& part : level.parts) {
            const glm::mat4* data = transforms.data();
            if (part.local != glm::mat4(1.0f)) {
                impl.part_transforms.resize(transforms.size());
                for (size_t i = 0; i < transforms.size(); ++i) {
                    impl.part_transforms[i] = transforms[i] * part.local;
                }
                data = impl.part_transforms.data();
            }
            renderer.DrawMeshInstances(part.mesh, part.material, data, static_cast<int>(transforms.size()), options);
            ++impl.stats.batches;
        }
    };

    InstanceOptions view_only;
    view_only.shadow_cascades = 0;
    InstanceOptions shadow_only;
    shadow_only.draw_in_view = false;
    const InstanceOptions view_and_shadow;

    for (SpeciesData& data : impl.species) {
        const int level_count = static_cast<int>(data.species.levels.size());
        for (int level = 0; level < level_count; ++level) {
            const LevelLists& lists = data.lists[level];
            impl.stats.instances_per_level[level] += static_cast<int>(lists.with_shadow.size() + lists.without_shadow.size());
            const VegetationLevel& look = data.species.levels[static_cast<size_t>(level)];
            submit(look, lists.without_shadow, view_only);
            const int shadow_level = glm::min(level + glm::max(data.species.shadow_level_bias, 0), level_count - 1);
            if (shadow_level == level) {
                submit(look, lists.with_shadow, view_and_shadow);
            } else {
                // The picture gets this level's meshes, the shadow image a simpler level's.
                submit(look, lists.with_shadow, view_only);
                submit(data.species.levels[static_cast<size_t>(shadow_level)], lists.with_shadow, shadow_only);
            }
        }
    }
}

} // namespace poligon

#include "poligon/world/vegetation.h"

#include <cmath>
#include <map>
#include <utility>

#include "poligon/core/fatal_error.h"

namespace poligon {

namespace {

constexpr int kMaxLevels = 8;

// The plants of one species in one cell, relative to the cell's corner.
struct CellSpecies {
    int species;
    std::vector<glm::mat4> transforms;
};

struct Cell {
    // The cell's corner in the world (y = 0); everything below is relative to it.
    glm::dvec3 corner{0.0};
    // Box around the feet of the cell's plants.
    glm::vec2 min{1.0e30f};
    glm::vec2 max{-1.0e30f};
    float min_y = 1.0e30f;
    float max_y = -1.0e30f;
    std::vector<CellSpecies> species;
};

// Plants are at most this tall and wide beyond their foot; the box around a cell's feet
// grows by this much before it is tested against the camera's view.
constexpr float kPlantReach = 30.0f;

// The six sides of what a camera sees, as planes (normal, distance) with the inside positive.
struct ViewVolume {
    glm::vec4 planes[6];

    // The view seen from `origin`: positions given to Sees are relative to it.
    static ViewVolume FromCamera(const Camera& camera, const glm::dvec3& origin, float aspect_ratio) {
        const glm::mat4 view = glm::lookAt(glm::vec3(camera.position - origin), glm::vec3(camera.target - origin), camera.up);
        const glm::mat4 projection =
            glm::perspective(glm::radians(camera.vertical_fov_degrees), aspect_ratio, camera.near_plane, camera.far_plane);
        const glm::mat4 clip = projection * view;
        const auto row = [&clip](int i) { return glm::vec4(clip[0][i], clip[1][i], clip[2][i], clip[3][i]); };
        ViewVolume volume;
        volume.planes[0] = row(3) + row(0);
        volume.planes[1] = row(3) - row(0);
        volume.planes[2] = row(3) + row(1);
        volume.planes[3] = row(3) - row(1);
        volume.planes[4] = row(3) - row(2);
        volume.planes[5] = row(2);
        for (glm::vec4& plane : volume.planes) {
            plane /= glm::length(glm::vec3(plane));
        }
        return volume;
    }

    bool Sees(const glm::vec3& center, float radius) const {
        for (const glm::vec4& plane : planes) {
            if (glm::dot(glm::vec3(plane), center) + plane.w < -radius) {
                return false;
            }
        }
        return true;
    }
};

// The transforms collected during one Draw for one species and level, split by whether
// the plants are within shadow range. Plants whose shadow is fading out are in
// without_shadow for the picture and again in shadow_fading, with the fade, for the shadow.
// Plants in cells outside the camera's view cast shadows only (shadow_only).
struct LevelLists {
    std::vector<glm::mat4> with_shadow;
    std::vector<glm::mat4> without_shadow;
    std::vector<glm::mat4> shadow_fading;
    std::vector<glm::mat4> shadow_only;
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
    // Scratch for parts whose local transform is not the identity, and for the plants of a
    // level as the picture draws them (shadowed and unshadowed together).
    std::vector<glm::mat4> part_transforms;
    std::vector<glm::mat4> view_list;
    std::vector<glm::mat4> caster_list;

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
    if (species.fade_width < 0.0f || species.fade_width >= 1.0f) {
        FatalError("Vegetation::AddSpecies: fade_width must be at least 0 and below 1");
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

void Vegetation::AddInstance(int species, const Transform& transform) {
    AddInstance(species, transform.position, transform.GetLocalMatrix());
}

void Vegetation::AddInstance(int species, const glm::dvec3& origin, const glm::mat4& local) {
    if (species < 0 || species >= static_cast<int>(impl_->species.size())) {
        FatalError("Vegetation::AddInstance: no such species");
    }
    const glm::dvec3 world_foot = origin + glm::dvec3(local[3]);
    const double cell_size = static_cast<double>(impl_->cell_size);
    const std::pair<int, int> key{static_cast<int>(std::floor(world_foot.x / cell_size)),
                                  static_cast<int>(std::floor(world_foot.z / cell_size))};
    Cell& cell = impl_->cells[key];
    cell.corner = glm::dvec3(static_cast<double>(key.first) * cell_size, 0.0, static_cast<double>(key.second) * cell_size);
    const glm::mat4 transform = RelativeTo(origin, local, cell.corner);
    const glm::vec2 foot{transform[3].x, transform[3].z};
    cell.min = glm::min(cell.min, foot);
    cell.max = glm::max(cell.max, foot);
    cell.min_y = glm::min(cell.min_y, transform[3].y);
    cell.max_y = glm::max(cell.max_y, transform[3].y);
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
            lists.shadow_fading.clear();
            lists.shadow_only.clear();
        }
    }

    // The lists of this frame are relative to a point near the camera (rounded to 4096 m,
    // so that it seldom changes, and 0 near the origin); each cell's plants are moved there by
    // its corner's offset.
    const glm::dvec3 origin(std::round(camera.position.x / 4096.0) * 4096.0, 0.0, std::round(camera.position.z / 4096.0) * 4096.0);
    // Distances are measured along the ground: a plant seen from a hill top is as detailed
    // as one seen from the same distance on the flat.
    const glm::vec2 eye(glm::dvec2(camera.position.x, camera.position.z) - glm::dvec2(origin.x, origin.z));
    // Cells wholly outside the camera's view only cast shadows: their plants skip the
    // picture lists, which saves the renderer most of its per-plant culling.
    const ViewVolume view = ViewVolume::FromCamera(
        camera, origin, static_cast<float>(renderer.GetWidth()) / static_cast<float>(glm::max(renderer.GetHeight(), 1)));
    for (const auto& [key, stored_cell] : impl.cells) {
        ++impl.stats.cells_total;
        // The cell's box, relative to this frame's origin.
        const glm::vec3 shift(stored_cell.corner - origin);
        const glm::vec4 shift4(shift, 0.0f);
        Cell cell;
        cell.min = stored_cell.min + glm::vec2(shift.x, shift.z);
        cell.max = stored_cell.max + glm::vec2(shift.x, shift.z);
        cell.min_y = stored_cell.min_y;
        cell.max_y = stored_cell.max_y;
        const glm::vec3 cell_center((cell.min.x + cell.max.x) * 0.5f, (cell.min_y + cell.max_y) * 0.5f + kPlantReach * 0.5f,
                                    (cell.min.y + cell.max.y) * 0.5f);
        const float cell_radius =
            glm::length(glm::vec3(cell.max.x - cell.min.x, cell.max_y - cell.min_y + kPlantReach, cell.max.y - cell.min.y)) *
                0.5f +
            kPlantReach;
        const bool cell_visible = view.Sees(cell_center, cell_radius);
        if (!cell_visible) {
            ++impl.stats.cells_out_of_view;
        }
        // Nearest and farthest the cell's plants can be from the camera.
        const glm::vec2 nearest_point = glm::clamp(eye, cell.min, cell.max);
        const float nearest = glm::distance(eye, nearest_point);
        const glm::vec2 farthest_corner{std::fabs(eye.x - cell.min.x) > std::fabs(eye.x - cell.max.x) ? cell.min.x : cell.max.x,
                                        std::fabs(eye.y - cell.min.y) > std::fabs(eye.y - cell.max.y) ? cell.min.y : cell.max.y};
        const float farthest = glm::distance(eye, farthest_corner);

        bool cell_used = false;
        for (const CellSpecies& entry : stored_cell.species) {
            SpeciesData& data = impl.species[static_cast<size_t>(entry.species)];
            const int level_count = static_cast<int>(data.species.levels.size());
            if (nearest > data.level_distance[level_count - 1] || (!cell_visible && nearest > data.shadow_distance)) {
                continue;  // every plant of this species in the cell is out of range (or out of view and shadow range)
            }
            cell_used = true;
            // The level a plant at this distance shows, and how far it is into the fade
            // towards the next level: 0 = not fading, up to 1 at the level's end.
            // Level level_count means "not drawn".
            const float fade_width = data.species.fade_width;
            const auto choose = [&data, level_count, fade_width](float distance, float& fade) {
                fade = 0.0f;
                for (int level = 0; level < level_count; ++level) {
                    const float end = data.level_distance[level];
                    const float start = end * (1.0f - fade_width);
                    if (distance <= start) {
                        return level;
                    }
                    if (distance <= end) {
                        fade = (distance - start) / (end - start);
                        return level;
                    }
                }
                return level_count;
            };
            float nearest_fade = 0.0f;
            float farthest_fade = 0.0f;
            const int nearest_level = choose(nearest, nearest_fade);
            const bool one_level = nearest_level == choose(farthest, farthest_fade) && nearest_fade == 0.0f &&
                                   farthest_fade == 0.0f;
            // Shadows fade out between shadow_fade_start and shadow_distance.
            const float shadow_fade_start = data.shadow_distance * (1.0f - fade_width);
            const bool one_shadow_answer = farthest <= shadow_fade_start || nearest > data.shadow_distance;
            if (one_level && one_shadow_answer) {
                // The whole cell falls into one level, with nothing fading: hand its plants
                // over in one piece.
                LevelLists& lists = data.lists[nearest_level];
                std::vector<glm::mat4>& list = !cell_visible                      ? lists.shadow_only
                                               : nearest <= data.shadow_distance ? lists.with_shadow
                                                                                 : lists.without_shadow;
                for (glm::mat4 transform : entry.transforms) {
                    transform[3] += shift4;
                    list.push_back(transform);
                }
                continue;
            }
            for (glm::mat4 transform : entry.transforms) {
                transform[3] += shift4;
                const float distance = glm::distance(eye, glm::vec2(transform[3].x, transform[3].z));
                float fade = 0.0f;
                const int level = choose(distance, fade);
                if (level == level_count) {
                    continue;
                }
                const bool with_shadow = distance <= data.shadow_distance;
                const auto add = [&data, with_shadow, cell_visible](int to_level, const glm::mat4& placed) {
                    LevelLists& lists = data.lists[to_level];
                    if (!cell_visible) {
                        if (with_shadow) {
                            lists.shadow_only.push_back(placed);
                        }
                        return;
                    }
                    (with_shadow ? lists.with_shadow : lists.without_shadow).push_back(placed);
                };
                if (fade == 0.0f) {
                    if (with_shadow && distance > shadow_fade_start) {
                        // Drawn whole, with a shadow that dissolves with distance.
                        if (cell_visible) {
                            data.lists[level].without_shadow.push_back(transform);
                        }
                        glm::mat4 shadow = transform;
                        shadow[3][3] = -(distance - shadow_fade_start) / (data.shadow_distance - shadow_fade_start);
                        data.lists[level].shadow_fading.push_back(shadow);
                        continue;
                    }
                    add(level, transform);
                    continue;
                }
                // The last value of the matrix carries the fade to the shaders (see
                // Renderer::DrawMeshInstances): this level keeps the pixels the next one leaves.
                ++impl.stats.instances_fading;
                glm::mat4 fading_out = transform;
                fading_out[3][3] = -fade;
                add(level, fading_out);
                if (level + 1 < level_count) {
                    glm::mat4 fading_in = transform;
                    fading_in[3][3] = fade;
                    add(level + 1, fading_in);
                }
            }
        }
        if (!cell_used) {
            ++impl.stats.cells_skipped;
        }
    }

    // Hands a list to the renderer once per part of the level's meshes, or once as an impostor.
    const auto submit = [&impl, &renderer, &origin](const VegetationLevel& level, const std::vector<glm::mat4>& transforms,
                                           const InstanceOptions& options) {
        if (transforms.empty()) {
            return;
        }
        if (level.impostor.id != 0) {
            renderer.DrawImpostorInstances(level.impostor, origin, transforms.data(), static_cast<int>(transforms.size()), options);
            ++impl.stats.batches;
            return;
        }
        for (const VegetationPart& part : level.parts) {
            const glm::mat4* data = transforms.data();
            if (part.local != glm::mat4(1.0f)) {
                // The plant's matrix times the part's; the fade in [3][3] carries over, since
                // part matrices keep 1 there.
                impl.part_transforms.resize(transforms.size());
                for (size_t i = 0; i < transforms.size(); ++i) {
                    impl.part_transforms[i] = transforms[i] * part.local;
                }
                data = impl.part_transforms.data();
            }
            renderer.DrawMeshInstances(part.mesh, part.material, origin, data, static_cast<int>(transforms.size()), options);
            ++impl.stats.batches;
        }
    };

    InstanceOptions view_only;
    view_only.shadow_cascades = 0;
    InstanceOptions shadow_only;
    shadow_only.draw_in_view = false;

    for (SpeciesData& data : impl.species) {
        const std::vector<VegetationLevel>& levels = data.species.levels;
        const int level_count = static_cast<int>(levels.size());
        for (int level = 0; level < level_count; ++level) {
            const LevelLists& lists = data.lists[level];
            impl.stats.instances_per_level[level] += static_cast<int>(lists.with_shadow.size() + lists.without_shadow.size());
            const VegetationLevel& look = levels[static_cast<size_t>(level)];
            // One batch draws the level in the picture, shadowed and unshadowed plants
            // together (one draw call per part instead of two); the shadows come from
            // separate caster batches, which the picture passes skip.
            impl.view_list.clear();
            impl.view_list.insert(impl.view_list.end(), lists.without_shadow.begin(), lists.without_shadow.end());
            impl.view_list.insert(impl.view_list.end(), lists.with_shadow.begin(), lists.with_shadow.end());
            submit(look, impl.view_list, view_only);
            if (look.impostor.id != 0) {
                continue;  // impostors cast no shadows
            }
            // Shadows come from a simpler mesh level, never from an impostor.
            int shadow_level = glm::min(level + glm::max(data.species.shadow_level_bias, 0), level_count - 1);
            while (shadow_level > level && levels[static_cast<size_t>(shadow_level)].impostor.id != 0) {
                --shadow_level;
            }
            const VegetationLevel& caster = levels[static_cast<size_t>(shadow_level)];
            impl.caster_list.clear();
            impl.caster_list.insert(impl.caster_list.end(), lists.with_shadow.begin(), lists.with_shadow.end());
            impl.caster_list.insert(impl.caster_list.end(), lists.shadow_only.begin(), lists.shadow_only.end());
            submit(caster, impl.caster_list, shadow_only);
            submit(caster, lists.shadow_fading, shadow_only);
        }
    }
}

} // namespace poligon

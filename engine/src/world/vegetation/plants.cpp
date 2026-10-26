#include "poligon/world/plants.h"

#include <algorithm>
#include <numeric>
#include <string>

namespace poligon {

namespace {

struct Box {
    glm::vec3 min{1.0e30f};
    glm::vec3 max{-1.0e30f};
};

// The model's materials with the kind's wind response and lighting settings.
std::vector<Material> WindyMaterials(const LoadedModel& model, const PlantSettings& settings) {
    std::vector<Material> materials = model.render.materials;
    for (Material& material : materials) {
        material.wind_bend = settings.wind_bend;
        material.wind_flutter = material.alpha_cutoff > 0.0f ? settings.wind_flutter : 0.0f;
        material.normal_up = std::max(material.normal_up, settings.normal_up);
        material.casts_ambient_occlusion = material.casts_ambient_occlusion && settings.casts_ambient_occlusion;
    }
    return materials;
}

} // namespace

PlantKind MakePlantKind(const LoadedModel& loaded, const PlantSettings& settings, Vegetation& vegetation) {
    PlantKind kind;
    kind.settings = settings;
    const Model& model = loaded.model;
    const std::vector<Material> materials = WindyMaterials(loaded, settings);
    const size_t count = model.instances.size();

    // The box around each instance.
    std::vector<Box> boxes(count);
    for (size_t i = 0; i < count; ++i) {
        const ModelInstance& instance = model.instances[i];
        for (const MeshVertex& vertex : model.meshes[static_cast<size_t>(instance.mesh_index)].vertices) {
            const glm::vec3 position = glm::vec3(instance.transform * glm::vec4(vertex.position, 1.0f));
            boxes[i].min = glm::min(boxes[i].min, position);
            boxes[i].max = glm::max(boxes[i].max, position);
        }
    }
    // Groups of instances whose boxes overlap seen from above (or everything, unsplit).
    std::vector<size_t> group(count);
    std::iota(group.begin(), group.end(), size_t{0});
    const auto find_group = [&group](size_t i) {
        while (group[i] != i) {
            i = group[i];
        }
        return i;
    };
    for (size_t a = 0; a < count; ++a) {
        for (size_t b = a + 1; b < count; ++b) {
            const bool overlap = boxes[a].min.x < boxes[b].max.x && boxes[b].min.x < boxes[a].max.x &&
                                 boxes[a].min.z < boxes[b].max.z && boxes[b].min.z < boxes[a].max.z;
            if (!settings.split_variants || overlap) {
                group[find_group(b)] = find_group(a);
            }
        }
    }

    for (size_t root = 0; root < count; ++root) {
        if (find_group(root) != root) {
            continue;
        }
        Box box;
        std::vector<std::string> node_names;
        for (size_t i = 0; i < count; ++i) {
            if (find_group(i) == root) {
                box.min = glm::min(box.min, boxes[i].min);
                box.max = glm::max(box.max, boxes[i].max);
                node_names.push_back(model.instances[i].node_name);
            }
        }
        const glm::mat4 to_foot = glm::translate(
            glm::mat4(1.0f), {settings.keep_origin ? 0.0f : -(box.min.x + box.max.x) * 0.5f, -box.min.y,
                              settings.keep_origin ? 0.0f : -(box.min.z + box.max.z) * 0.5f});
        PlantVariant variant;
        variant.height = box.max.y - box.min.y;
        variant.half_width = {(box.max.x - box.min.x) * 0.5f, (box.max.z - box.min.z) * 0.5f};

        // Level 0: the full model's meshes of this group.
        std::vector<VegetationLevel> levels;
        VegetationLevel full;
        for (size_t i = 0; i < count; ++i) {
            if (find_group(i) != root) {
                continue;
            }
            const size_t mesh_index = static_cast<size_t>(model.instances[i].mesh_index);
            full.parts.push_back({loaded.render.meshes[mesh_index],
                                  materials[static_cast<size_t>(loaded.render.mesh_material_index[mesh_index])],
                                  to_foot * model.instances[i].transform});
        }
        levels.push_back(full);
        // Simpler levels: nodes named like this group's, materials matched by name.
        for (size_t lod = 0; lod < loaded.lod_models.size(); ++lod) {
            const Model& simpler = loaded.lod_models[lod];
            VegetationLevel level;
            for (const ModelInstance& instance : simpler.instances) {
                if (std::find(node_names.begin(), node_names.end(), instance.node_name) == node_names.end()) {
                    continue;
                }
                const ModelMesh& mesh = simpler.meshes[static_cast<size_t>(instance.mesh_index)];
                const std::string& material_name = simpler.materials[static_cast<size_t>(mesh.material_index)].name;
                size_t material_index = 0;
                for (size_t m = 0; m < model.materials.size(); ++m) {
                    if (model.materials[m].name == material_name) {
                        material_index = m;
                        break;
                    }
                }
                level.parts.push_back({loaded.lod_meshes[lod][static_cast<size_t>(instance.mesh_index)], materials[material_index],
                                       to_foot * instance.transform});
            }
            if (!level.parts.empty()) {
                levels.push_back(level);
            }
        }
        // Distances: one per level in order, the impostor taking the last.
        const std::vector<float>& distances = settings.level_distances;
        const auto distance_at = [&distances](size_t index) {
            return distances.empty() ? 150.0f : distances[std::min(index, distances.size() - 1)];
        };
        const size_t mesh_levels = levels.size();
        for (size_t i = 0; i < mesh_levels; ++i) {
            levels[i].max_distance = distance_at(i);
        }
        // Missing simpler levels: the last mesh level reaches as far as they would have.
        const size_t wanted_mesh_levels = 1 + loaded.lod_models.size();
        if (mesh_levels < wanted_mesh_levels) {
            levels.back().max_distance = distance_at(wanted_mesh_levels - 1);
        }
        if (loaded.impostor.id != 0 && distances.size() > wanted_mesh_levels) {
            VegetationLevel far;
            far.impostor = loaded.impostor;
            far.max_distance = distance_at(wanted_mesh_levels);
            levels.push_back(far);
        } else if (settings.distance_without_impostor > 0.0f) {
            levels.back().max_distance = settings.distance_without_impostor;
        }
        variant.species.levels = std::move(levels);
        variant.species.shadow_distance = settings.shadow_distance;
        variant.species.shadow_level_bias = settings.shadow_level_bias;
        variant.species_id = vegetation.AddSpecies(variant.species);
        kind.variants.push_back(std::move(variant));
    }
    return kind;
}

BodyHandle AddPlantCollider(PhysicsWorld& physics, const PlantKind& kind, const PlantVariant& variant, const glm::dvec3& foot,
                            float turn, float scale) {
    BodySettings body;
    body.rotation = glm::angleAxis(turn, glm::vec3(0.0f, 1.0f, 0.0f));
    if (kind.settings.collider == PlantCollider::Trunk) {
        const float half_height = std::min(variant.height * scale * 0.5f, 4.0f);
        body.position = foot + glm::dvec3(0.0f, half_height - 0.5f, 0.0f);
        return physics.AddCylinder(half_height, kind.settings.trunk_radius * scale, body);
    } else if (kind.settings.collider == PlantCollider::Block) {
        // Somewhat smaller than the box around the model, whose corners are mostly air.
        const glm::vec3 half_size{variant.half_width.x * scale * 0.8f, variant.height * scale * 0.45f,
                                  variant.half_width.y * scale * 0.8f};
        body.position = foot + glm::dvec3(0.0f, variant.height * scale * 0.5f, 0.0f);
        return physics.AddBox(half_size, body);
    }
    return BodyHandle{};
}

namespace {

struct PlantPatchData : PatchData {
    std::vector<PlacedPlant> plants;
};

// The patch's plants, from the planter, over the patch's rectangle of the land.
std::unique_ptr<PatchData> PlantPatch(LandSource& land, const Planter& planter, PatchCoord patch) {
    const LandInfo& info = land.GetInfo();
    const std::shared_ptr<const LandPatch> patch_land = land.GetPatch(patch);
    const float size = static_cast<float>(info.grid.patch_size);
    const glm::vec2 low(static_cast<float>(patch.x) * size, static_cast<float>(patch.z) * size);
    const glm::vec2 high(std::min(low.x + size, info.size_x), std::min(low.y + size, info.size_z));
    auto data = std::make_unique<PlantPatchData>();
    planter(*patch_land, low, high, data->plants);
    return data;
}

} // namespace

PlantPatchSource::PlantPatchSource(Vegetation& vegetation, LandSource& land, Planter planter)
    : vegetation_(vegetation), land_(land), planter_(std::move(planter)) {}

std::unique_ptr<PatchData> PlantPatchSource::Load(PatchCoord patch) {
    return PlantPatch(land_, planter_, patch);
}

void PlantPatchSource::Attach(PatchCoord, std::unique_ptr<PatchData> data) {
    for (const PlacedPlant& plant : static_cast<PlantPatchData&>(*data).plants) {
        const PlantVariant& variant = plant.kind->variants[static_cast<size_t>(plant.variant)];
        vegetation_.AddInstance(variant.species_id, Transform(plant.foot, glm::angleAxis(plant.turn, glm::vec3(0.0f, 1.0f, 0.0f)),
                                                              glm::vec3(plant.scale)));
    }
}

void PlantPatchSource::Detach(PatchCoord patch) {
    const LandInfo& info = land_.GetInfo();
    const glm::dvec2 low = glm::dvec2(info.world_origin.x, info.world_origin.z) +
                           glm::dvec2(static_cast<double>(patch.x), static_cast<double>(patch.z)) * info.grid.patch_size;
    vegetation_.RemoveInstancesInRectangle(low, low + glm::dvec2(info.grid.patch_size));
}

PlantColliderSource::PlantColliderSource(PhysicsWorld& physics, LandSource& land, Planter planter)
    : physics_(physics), land_(land), planter_(std::move(planter)) {}

std::unique_ptr<PatchData> PlantColliderSource::Load(PatchCoord patch) {
    return PlantPatch(land_, planter_, patch);
}

void PlantColliderSource::Attach(PatchCoord patch, std::unique_ptr<PatchData> data) {
    std::vector<BodyHandle>& bodies = bodies_[patch];
    for (const PlacedPlant& plant : static_cast<PlantPatchData&>(*data).plants) {
        const PlantVariant& variant = plant.kind->variants[static_cast<size_t>(plant.variant)];
        const BodyHandle body = AddPlantCollider(physics_, *plant.kind, variant, plant.foot, plant.turn, plant.scale);
        if (body.id != 0) {
            bodies.push_back(body);
        }
    }
}

void PlantColliderSource::Detach(PatchCoord patch) {
    const auto found = bodies_.find(patch);
    if (found == bodies_.end()) {
        return;
    }
    for (const BodyHandle body : found->second) {
        physics_.RemoveBody(body);
    }
    bodies_.erase(found);
}

int PlantColliderSource::GetBodyCount() const {
    int count = 0;
    for (const auto& [patch, bodies] : bodies_) {
        count += static_cast<int>(bodies.size());
    }
    return count;
}

} // namespace poligon

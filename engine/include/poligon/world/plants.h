#pragma once

#include <vector>

#include "poligon/assets/assets.h"
#include "poligon/physics/physics.h"
#include "poligon/world/vegetation.h"

namespace poligon {

// Plants, rocks, stumps and fallen trunks made from model assets (poligon/assets/assets.h), ready
// for the vegetation system and the physics world.
//
// A model file can hold one plant or a row of different ones; with split_variants each
// group of meshes whose boxes overlap seen from above (trunk and leaves of one tree, not
// neighbors in a row) becomes its own variant, moved so that the middle of its foot is at
// the origin. Each variant's levels of detail come from the asset: the full model, its
// simpler levels (nodes matched by name) and its impostor as the farthest level.

// What blocks the player and thrown things.
enum class PlantCollider {
    None,   // walked through (grass, ferns, shrubs)
    Trunk,  // an upright cylinder where the trunk is; the crown does not block
    Block,  // a box somewhat smaller than the whole thing (rocks, stumps, fallen trunks)
};

struct PlantSettings {
    // The file holds several plants side by side; each becomes a variant.
    bool split_variants = false;
    // Keep the file's origin sideways instead of the middle of the foot (a tree's trunk
    // stands at the origin while its crown may lean to one side).
    bool keep_origin = false;
    // How far each level is used (meters): the full model, each simpler level, then the
    // impostor. A level the asset lacks is skipped and the one before it reaches its
    // distance. When the asset has no impostor, the last mesh level reaches
    // distance_without_impostor instead (0 = its own distance).
    std::vector<float> level_distances{150.0f};
    float distance_without_impostor = 0.0f;
    // See VegetationSpecies.
    float shadow_distance = 0.0f;
    int shadow_level_bias = 0;
    // See Material::wind_bend and wind_flutter; flutter goes to cut-out (leaf) materials only.
    float wind_bend = 0.0f;
    float wind_flutter = 0.0f;
    // Given to all the model's materials: see Material::normal_up and
    // casts_ambient_occlusion. Grass: 1 and false; ferns and other low plants: about 0.5
    // and false (screen-space ambient occlusion makes thin leaves and the ground under
    // them far too dark).
    float normal_up = 0.0f;
    bool casts_ambient_occlusion = true;
    PlantCollider collider = PlantCollider::None;
    // For PlantCollider::Trunk: the trunk's radius before scaling, meters.
    float trunk_radius = 0.3f;
};

struct PlantVariant {
    VegetationSpecies species;
    // Its number in the Vegetation it was added to.
    int species_id = -1;
    // Height, and half of its width along x and z, before scaling.
    float height = 0.0f;
    glm::vec2 half_width{0.0f};
};

struct PlantKind {
    std::vector<PlantVariant> variants;
    PlantSettings settings;
};

// Makes the variants of a kind from a loaded model and adds each to `vegetation`.
PlantKind MakePlantKind(const LoadedModel& model, const PlantSettings& settings, Vegetation& vegetation);

// Adds the body that blocks movement for one placed plant whose foot is at `foot`, turned
// `turn` radians around the vertical and scaled by `scale`. Nothing for PlantCollider::None.
void AddPlantCollider(PhysicsWorld& physics, const PlantKind& kind, const PlantVariant& variant, const glm::dvec3& foot,
                      float turn, float scale);

} // namespace poligon

#pragma once

#include <memory>
#include <vector>

#include "poligon/core/math.h"
#include "poligon/world/terrain.h"
#include "poligon/world/vegetation.h"

namespace poligon {

// Ground cover: grass, ferns, litter, small plants and pebbles, made on the fly around the
// camera. There are far too many of them to store for a whole map, so the land is cut into
// square cells, and a cell within `radius` of the camera gets its plants computed from rules
// (slope, height, patchiness) and a hash of its position: the same cell always comes out the
// same, whenever the camera returns. Cells beyond the radius are dropped. What exists is
// drawn by the vegetation system, so levels of detail, fades, wind and shadows apply.
//
// Rules of thumb: keep the species' farthest level distance below the radius, so that
// plants fade out before their cell is dropped; keep the meshes cheap (grass cards, a
// pebble of a few dozen triangles), because a radius of 80 m holds tens of thousands.

struct GroundCoverLayer {
    // What to draw and how far (VegetationSpecies: parts per level, shadow distance).
    VegetationSpecies species;
    // Plants per square meter where the layer is allowed in full.
    float density = 1.0f;
    float scale_min = 0.8f;
    float scale_max = 1.2f;
    // Nothing of this layer grows on steeper ground, or outside this range of world heights.
    float max_slope_degrees = 35.0f;
    float min_height = -1.0e30f;
    float max_height = 1.0e30f;
    // Patchiness: the layer grows where a smooth noise with features of about `patch_size`
    // meters is above `patch_threshold` (0..1), thinning out over `patch_softness` below it.
    // threshold 0 = everywhere evenly.
    float patch_size = 20.0f;
    float patch_threshold = 0.0f;
    float patch_softness = 0.2f;
    // Tilt the plant with the slope (grass, litter) instead of standing it upright.
    bool align_to_ground = false;
    // Push the plant this many meters into the ground (pebbles, half-buried things).
    float sink_meters = 0.0f;
    // Different seeds give different placements for layers that would otherwise coincide.
    int seed = 0;
    // Conditions on the generator's maps (only when GroundCover::SetMaps was given maps):
    // a plant is kept with the probability of their weight. The default takes everything.
    MapConditions conditions;
};

struct GroundCoverStats {
    int cells_alive = 0;
    int instances_alive = 0;
    // Cells computed in the last Update (0 when the camera stayed within its cells).
    int cells_built = 0;
};

class GroundCover {
public:
    // The terrain gives heights and slopes; it must outlive the ground cover.
    // radius: how far around the camera plants exist. cell_size: edge of a cell in meters.
    GroundCover(const Terrain& terrain, float radius = 80.0f, float cell_size = 16.0f);
    ~GroundCover();
    GroundCover(const GroundCover&) = delete;
    GroundCover& operator=(const GroundCover&) = delete;

    // Returns the layer's number. Layers are added before the first Update.
    int AddLayer(const GroundCoverLayer& layer);

    // Builds the cells that came within the radius since the last call and drops those
    // that left it. Cheap when the camera stayed inside the same cell. Call once per frame
    // before Draw.
    void Update(const glm::vec3& camera_position);
    // Call between Renderer::BeginFrame and EndFrame, with the camera of that frame.
    void Draw(Renderer& renderer, const Camera& camera);

    void SetRadius(float radius);
    float GetRadius() const;
    // Multiplies every layer's density (graphics quality); all cells are rebuilt.
    void SetDensityScale(float scale);
    // The generator's maps of the land, for the layers' conditions; a map position is the
    // world position (x, z) plus `world_to_map`. nullptr turns the conditions off. Cells
    // are built again. The maps must outlive the ground cover.
    void SetMaps(const TerrainMaps* maps, const glm::vec2& world_to_map);
    // Appends the transforms of all living plants of a layer (for tests and tools).
    void CollectInstances(int layer, std::vector<glm::mat4>& out) const;
    const GroundCoverStats& GetStats() const;
    // The vegetation system underneath, for its statistics and distance scaling.
    const Vegetation& GetVegetation() const;
    Vegetation& GetVegetation();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace poligon

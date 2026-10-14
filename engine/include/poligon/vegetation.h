#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "poligon/math.h"
#include "poligon/renderer.h"

namespace poligon {

// Draws large numbers of plants: trees, bushes, ferns, grass, also rocks and other things
// that stand around in great numbers and do not move.
//
// How it stays fast:
// - Every kind of plant (a "species") has several levels of detail: the full mesh for up
//   close, simpler ones for further away. Which one a plant is drawn with depends on its
//   distance from the camera.
// - All plants of one species and level are drawn in one call (Renderer::DrawMeshInstances).
// - The world is cut into square cells. A cell that is wholly beyond a species' range is
//   skipped without looking at its plants, and a cell that lies wholly within one level's
//   range is handed over in one piece.
// - Shadows can use a simpler level than the picture, and stop at a shorter distance.
//
// Plants are placed once (AddInstance) and stay; Draw is called every frame.

// One mesh of a plant with its material, placed relative to the plant's foot.
struct VegetationPart {
    MeshHandle mesh;
    Material material;
    glm::mat4 local{1.0f};
};

// How a plant looks within a range of distances.
struct VegetationLevel {
    std::vector<VegetationPart> parts;
    // This level is used up to this distance from the camera (meters). Levels are listed
    // nearest first; beyond the last level's distance the plant is not drawn at all.
    float max_distance = 0.0f;
};

struct VegetationSpecies {
    std::vector<VegetationLevel> levels;
    // Plants further away than this cast no shadow. 0 = the species never casts shadows.
    float shadow_distance = 0.0f;
    // Shadows are drawn with a level this many steps simpler than the picture (0 = the
    // same). Shadows hide most detail, so 1 is usually invisible and saves a lot.
    int shadow_level_bias = 0;
};

// What the last Draw did.
struct VegetationStats {
    int instances_total = 0;
    // Plants handed to the renderer, per level (index 0 = most detailed, up to 8 levels).
    int instances_per_level[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    // Cells looked at, and cells skipped whole because nothing in them was within range.
    int cells_total = 0;
    int cells_skipped = 0;
    // Renderer::DrawMeshInstances calls made.
    int batches = 0;
};

class Vegetation {
public:
    // cell_size: edge of the square cells the world is cut into, in meters.
    explicit Vegetation(float cell_size = 32.0f);
    ~Vegetation();
    Vegetation(const Vegetation&) = delete;
    Vegetation& operator=(const Vegetation&) = delete;

    // Returns the species' number, for AddInstance. Meshes and textures stay owned by the caller.
    int AddSpecies(const VegetationSpecies& species);
    // Puts one plant into the world. `transform` places its foot.
    void AddInstance(int species, const glm::mat4& transform);
    // Removes all plants; species stay.
    void ClearInstances();
    int GetInstanceCount() const;

    // Changes a species' distances after the fact (for tuning with sliders): multiplies
    // every level's max_distance and the shadow distance of every species by these factors,
    // relative to what was given to AddSpecies.
    void SetDistanceScale(float detail_scale, float shadow_scale);

    // Call between Renderer::BeginFrame and EndFrame, with the camera of that frame.
    void Draw(Renderer& renderer, const Camera& camera);

    const VegetationStats& GetStats() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace poligon

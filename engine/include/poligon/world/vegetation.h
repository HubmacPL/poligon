#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "poligon/core/math.h"
#include "poligon/render/renderer.h"

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
// - The farthest level can be an impostor: one card showing baked pictures of the plant
//   (Renderer::BakeImpostor), so that a forest reaches the horizon.
// - A plant does not jump from one level to the next: over the last stretch of a level's
//   range both levels are drawn, one dissolving pixel by pixel as the other appears
//   ("dithered fade", see Renderer::DrawMeshInstances). After the last level it dissolves away.
//
// Plants are placed once (AddInstance) and stay; Draw is called every frame.

// One mesh of a plant with its material, placed relative to the plant's foot.
struct VegetationPart {
    MeshHandle mesh;
    Material material;
    glm::mat4 local{1.0f};
};

// How a plant looks within a range of distances: its meshes, or an impostor.
struct VegetationLevel {
    std::vector<VegetationPart> parts;
    // When set, the level is drawn as this impostor instead of `parts`. Impostors cast no
    // shadows; a species whose shadow level would be an impostor uses its last mesh level
    // for shadows instead.
    ImpostorHandle impostor;
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
    // Share of each level's distance over which the plant fades into the next level (into
    // nothing after the last one), and of shadow_distance over which its shadow fades out.
    // 0.1 with a level ending at 70 m: from 63 m to 70 m. 0 = the level changes at once.
    float fade_width = 0.1f;
};

// What the last Draw did.
struct VegetationStats {
    int instances_total = 0;
    // Plants handed to the renderer, per level (index 0 = most detailed, up to 8 levels).
    // A plant fading between two levels counts in both.
    int instances_per_level[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    // Plants that were fading between levels (or out after the last one).
    int instances_fading = 0;
    // Cells looked at, cells skipped whole because nothing in them was within range, and
    // cells outside the camera's view (their plants are handed over as shadow casters only).
    int cells_total = 0;
    int cells_skipped = 0;
    int cells_out_of_view = 0;
    // Renderer::DrawMeshInstances and DrawImpostorInstances calls made.
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
    // Puts one plant into the world. `transform` places its foot; or `origin` (double) with
    // `local` (float, relative to it) for placements a Transform cannot say. Plants are kept
    // relative to the corner of their cell, so float stays precise anywhere in a large world.
    void AddInstance(int species, const Transform& transform);
    void AddInstance(int species, const glm::dvec3& origin, const glm::mat4& local);
    // Removes all plants; species stay.
    void ClearInstances();
    // Where the cell grid starts (world x and z of a cell corner); by default 0. Set it before
    // the first plant. A streamed world sets its land's origin, so that its patches are made
    // of whole cells.
    void SetCellOrigin(const glm::dvec2& origin);
    // Removes the plants of every cell whose corner lies in [low, high) (world x and z): for a
    // patch of a streamed world, exactly its plants.
    void RemoveInstancesInRectangle(const glm::dvec2& low, const glm::dvec2& high);
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

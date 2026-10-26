#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "poligon/core/image.h"
#include "poligon/world/streaming.h"
#include "poligon/world/terrain.h"
#include "poligon/world/terrain_generator.h"

namespace poligon {

// The generated land of a streamed world, patch by patch (poligon/world/streaming.h).
//
// The terrain generator simulates the whole land at once, so the land is generated whole
// (when it is cooked) and cut into patches. A LandSource hands out one patch at a time: the
// generator's maps over the patch with a margin (a window: CutTerrainMaps) and the ground
// layer weights over the patch. Every source of a streamed world (terrain, its physics,
// water, plants, sky occlusion, ground cover) reads its patches through one LandSource, which
// keeps the last few patches so that they are cut or read once.
//
// Map coordinates: meters from the whole land's first point; patch (0, 0) starts there.

// Meters of maps kept around a patch: enough for plant rules, the ground's direction and
// the ground cover near the patch's edge.
constexpr float kLandPatchMargin = 8.0f;

struct LandPatch {
    PatchCoord patch;
    // Window of the generator's maps over the patch, widened by kLandPatchMargin.
    TerrainMaps maps;
    // Ground layer weights over exactly the patch's map points (TerrainMaterial::weights'
    // layout), or empty images when the land has no layer rules.
    Image weights[2];
};

// What a streamed land is, for those who build on it.
struct LandInfo {
    // Patches over the land, in map coordinates (origin 0) or, with world_origin added, in
    // the world.
    StreamingGridSettings grid;
    glm::dvec3 world_origin{0.0};  // world position of the land's first point (TerrainConfig::origin)
    float spacing = 1.0f;          // meters between height points
    float map_spacing = 2.0f;      // meters between map points
    float size_x = 0.0f;           // meters of land along x and z
    float size_z = 0.0f;
    std::vector<Lake> lakes;
    // The whole land's heights, a point every `background_spacing` meters, for drawing it far
    // away where no patch is loaded and for heights outside the loaded patches.
    HeightMap background;
};

// Hands out patches of land. GetPatch may be called from several threads at once.
class LandSource {
public:
    virtual ~LandSource() = default;
    virtual const LandInfo& GetInfo() const = 0;
    // The patch's land, read or cut now if it is not kept already. Never nullptr for a patch
    // inside the land.
    virtual std::shared_ptr<const LandPatch> GetPatch(PatchCoord patch) = 0;
    // The patch only if it is kept already (does not wait); nullptr otherwise.
    virtual std::shared_ptr<const LandPatch> FindPatch(PatchCoord patch) const = 0;
};

// Land cut from whole maps held in memory: each patch is cut when first asked for (the maps
// must outlive the source). Weights come from `rules` (MakeTerrainLayerWeights over the
// patch's window, which gives the whole land's weights to the bit); no rules, no weights.
// `background_spacing` must be a whole multiple of the maps' spacing.
class LandFromMaps : public LandSource {
public:
    LandFromMaps(const TerrainMaps& maps, double patch_size, const glm::dvec3& world_origin, const TerrainLayerRule* rules,
                 int rule_count, std::uint32_t weight_seed, float background_spacing = 64.0f, int kept_patches = 32);
    ~LandFromMaps() override;
    const LandInfo& GetInfo() const override;
    std::shared_ptr<const LandPatch> GetPatch(PatchCoord patch) override;
    std::shared_ptr<const LandPatch> FindPatch(PatchCoord patch) const override;

    // Cuts one patch (what GetPatch keeps); for tools that write patches to files.
    LandPatch CutPatch(PatchCoord patch) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// A coarser copy of a height map: every `step`-th point (the corners stay).
HeightMap MakeCoarseHeightMap(const HeightMap& map, int step);

} // namespace poligon

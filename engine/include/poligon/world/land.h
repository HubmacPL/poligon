#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "poligon/assets/assets.h"
#include "poligon/core/image.h"
#include "poligon/world/streaming.h"
#include "poligon/world/terrain.h"
#include "poligon/world/level.h"
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
    // At the same points, the lowest height of the land within one background spacing of
    // each (so over the four squares around it): read between its points it never rises
    // above the land, which makes a horizon from it never too high (TerrainOcclusion).
    HeightMap occlusion_floor;
    // Its ground layer weights at the same points (TerrainMaterial::weights' layout), or
    // empty images when the land has no layer rules.
    Image background_weights[2];
    // A place to look at the largest lake from (FindLakeViewpoint at an eye height of
    // kLandViewpointEyeHeight), found when the land is made, since a game holds only patches.
    bool has_lake_viewpoint = false;
    LakeViewpoint lake_viewpoint;
};

constexpr float kLandViewpointEyeHeight = 1.7f;

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

// --- Cooked land ---
//
// A land cooked into files (CookLand), read patch by patch from a cooked folder or an
// archive (LandFromFiles, through an AssetLibrary):
//
//     NAME.pworld        "poligon-world 1", then what LandInfo holds (without world_origin)
//                        and a number naming the settings it was made from
//     NAME/X_Z.ppatch    "poligon-patch 1", then one LandPatch (its maps window and weights)
//
// The bytes are those of the cut patches, so a patch read from a file equals the patch cut
// from the whole maps to the bit.

// Writes `land`'s description and every patch into `cooked_folder`. `settings_hash` is kept
// in the description (CookLevelWorld uses it to know when to cook again).
bool CookLand(const LandFromMaps& land, const std::string& name, std::uint64_t settings_hash, const char* cooked_folder,
              std::string& error);

// Cooks the streamed world a level names (Level::world) when its files are missing or were
// made from other settings (the terrain generator's, the level's ground layers, the patch
// size): generates the land (kept in `terrain_cache`, as LoadOrGenerateTerrain does), makes
// the ground layer weights from `layers` and cuts it into patches. `cooked` says whether it
// cooked; false and `error` on failure.
bool CookLevelWorld(const Level& level, const std::vector<GroundLayer>& layers, const char* cooked_folder, const char* terrain_cache,
                    bool& cooked, std::string& error);

// Land read from cooked files. The description is read when it is made (IsOpen and
// GetError say how it went); patches when asked for, from any thread. `world_origin` puts
// the land into the world. The library must outlive the source.
class LandFromFiles : public LandSource {
public:
    LandFromFiles(const AssetLibrary& library, const std::string& name, const glm::dvec3& world_origin, int kept_patches = 32);
    ~LandFromFiles() override;
    bool IsOpen() const;
    const std::string& GetError() const;
    const LandInfo& GetInfo() const override;
    // A patch that cannot be read ends the program with a message (the cooked files are broken).
    std::shared_ptr<const LandPatch> GetPatch(PatchCoord patch) override;
    std::shared_ptr<const LandPatch> FindPatch(PatchCoord patch) const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// A coarser copy of a height map: every `step`-th point (the corners stay).
HeightMap MakeCoarseHeightMap(const HeightMap& map, int step);
// The same points as MakeCoarseHeightMap, each the lowest height of `map` within `step`
// points of it (LandInfo::occlusion_floor).
HeightMap MakeOcclusionFloor(const HeightMap& map, int step);

} // namespace poligon

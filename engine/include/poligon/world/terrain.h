#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "poligon/world/height_map.h"
#include "poligon/world/terrain_occlusion.h"
#include "poligon/core/image.h"
#include "poligon/render/renderer.h"
#include "poligon/world/streaming.h"
#include "poligon/world/terrain_generator.h"

namespace poligon {

struct LandInfo;   // poligon/world/land.h
class LandSource;  // poligon/world/land.h
class PhysicsWorld;

// Draws a height map as land, kilometers of it at once.
//
// How it stays fast: the land is drawn as square patches from a tree of squares. The
// whole map is one square; a square close to the camera is split into four smaller ones,
// and those again, down to the smallest size. So the land under the camera's feet is fine
// (one vertex per height map point) and a hillside two kilometers away is a handful of
// large coarse squares. Every patch is the same grid mesh, lifted to the land's height on
// the graphics card (Renderer::DrawTerrain), so no mesh is ever built from the map.

struct TerrainConfig {
    // World position of the height map's first point. Its y is added to every height.
    glm::dvec3 origin{0.0};
    TerrainMaterial material;
    // A square is split while the camera is nearer to it than its size times this.
    // Larger = finer land further out, and more to draw.
    float detail = 2.0f;
};

// What the last Draw did.
struct TerrainStats {
    int patches = 0;
    int background_patches = 0;  // of a streamed terrain: squares drawn from the background
    // Of those, squares wholly below the land's horizon (SetOcclusion): shadow casters only.
    int patches_hidden = 0;
    // Patches by size: index 0 = smallest (finest). Up to 16 sizes.
    int patches_per_level[16] = {};
};

class Terrain {
public:
    // The height map is copied; the caller may free it.
    Terrain(Renderer& renderer, const HeightMap& map, const TerrainConfig& config);
    // A streamed terrain (poligon/world/streaming.h, poligon/world/land.h): it has no land
    // until patches are given (by a TerrainPatchSource, or SetPatch). `land` says how the
    // land is cut and where it lies (config.origin is not used: LandInfo::world_origin is);
    // `pages`: how many patches it can hold at once ((2 radius + 2)^2 for a streamer's window
    // of `radius`, which keeps patches half a patch beyond it). Each patch is its own tree of squares;
    // all of them are drawn by one draw call per pass. The patch size must be 64, 128, 256,
    // 512... height points. Heights where no patch is held come from LandInfo::background,
    // and there the land is drawn from it too (the far background: squares of a patch's size
    // or larger, 64 x 64 cells each, one more draw call per pass).
    Terrain(Renderer& renderer, const LandInfo& land, const TerrainConfig& config, int pages);
    ~Terrain();
    Terrain(const Terrain&) = delete;
    Terrain& operator=(const Terrain&) = delete;

    // Call between Renderer::BeginFrame and EndFrame, with the camera of that frame.
    void Draw(const Camera& camera);

    // The land at a world position (x, z): height, facing direction, steepness in degrees.
    // Outside the map the nearest edge counts.
    // World positions are double; the height is one too.
    double GetHeight(double x, double z) const;
    glm::vec3 GetNormal(double x, double z) const;
    float GetSlopeDegrees(double x, double z) const;
    // The map's extent in world coordinates.
    glm::dvec2 GetMin() const;
    glm::dvec2 GetMax() const;
    // The height map (of a streamed terrain: the background, coarse heights).
    const HeightMap& GetHeightMap() const;

    // --- Streamed terrain ---
    // Gives a patch its land: `heights` is a window of the whole height map (CutHeightMap,
    // map coordinates) covering the patch and one point around it where the land has one;
    // `weights` two images over the patch's map points (TerrainMaterial::weights' layout),
    // or nullptr. Outside BeginFrame and EndFrame.
    void SetPatch(PatchCoord patch, const HeightMap& heights, const Image* weights);
    void RemovePatch(PatchCoord patch);
    bool HasPatch(PatchCoord patch) const;
    int GetPatchCount() const;
    // Whether the background is drawn where no patch is held (on by default).
    void SetBackgroundVisible(bool visible);

    // Squares a hill hides from the camera (TerrainOcclusion::IsHidden, updated by the caller
    // for this frame's camera) only cast shadows. nullptr: none are hidden (the default).
    void SetOcclusion(const TerrainOcclusion* occlusion);

    void SetMaterial(const TerrainMaterial& material);
    void SetDetail(float detail);
    const TerrainStats& GetStats() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// The land of a streamed terrain, patch by patch: a Background source for WorldStreamer.
// Load reads the patch from `land` (on the loading thread); Attach gives it to the terrain.
class TerrainPatchSource : public PatchSource {
public:
    TerrainPatchSource(Terrain& terrain, LandSource& land);
    std::unique_ptr<PatchData> Load(PatchCoord patch) override;
    void Attach(PatchCoord patch, std::unique_ptr<PatchData> data) override;
    void Detach(PatchCoord patch) override;

private:
    Terrain& terrain_;
    LandSource& land_;
};

// The land's collision shapes, patch by patch: an Immediate source (it is what the
// simulation stands on). Each patch becomes one height field in `physics`, exactly the
// whole map's heights; neighbors share their border points, so nothing falls through.
class TerrainPhysicsSource : public PatchSource {
public:
    TerrainPhysicsSource(PhysicsWorld& physics, LandSource& land, float friction = 0.5f);
    std::unique_ptr<PatchData> Load(PatchCoord patch) override;
    void Attach(PatchCoord patch, std::unique_ptr<PatchData> data) override;
    void Detach(PatchCoord patch) override;
    int GetBodyCount() const;

private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};

// --- Ground layers from the generator's maps ---

// Where one ground layer covers the land, judged from the generator's maps
// (MapConditions, poligon/world/terrain_generator.h).
struct TerrainLayerRule {
    MapConditions conditions;
    // Irregular patches: with patch_size above 0 the weight is also multiplied by noise of
    // about that size (meters), cut so that `patch_cover` of the land (0 to 1) is covered.
    float patch_size = 0.0f;
    float patch_cover = 0.5f;
    // Multiplies the result.
    float strength = 1.0f;
};

// The weights of up to eight layers at every point of the maps' grid, for
// TerrainMaterial::weights: two images of map_width x map_depth pixels, red, green, blue
// and alpha of the first holding layers 0 to 3, of the second 4 to 7. At each point the
// weights are scaled to add up to 1; where no rule holds at all, layer 0 covers. The same
// maps, rules and seed give the same images.
void MakeTerrainLayerWeights(const TerrainMaps& maps, const TerrainLayerRule* rules, int count, std::uint32_t seed,
                             Image weights[2]);

// A ground layer as a level file (poligon/world/level.h) says it, a block of lines:
//
//     ground ground/forest_floor     # its textures: the cooked images NAME_color and NAME_normal
//     tile 3                         # meters after which the texture repeats
//     strength 0.35                  # multiplies the weight
//     moisture 0 0.45 0.15           # conditions on the maps, as in plant rules
//     patch 45 0.45                  # irregular patches: size in meters, share of land covered
//
// Layers are numbered in the order of the file (at most eight). Blocks of plant rules in
// the same lines are passed over, and plant rules pass over ground blocks.
struct GroundLayer {
    std::string texture;
    float tile_size = 3.0f;
    TerrainLayerRule rule;
    int line = 0;
};

// The words that begin ground layer lines, for LoadLevel's other_line_prefixes.
const std::vector<std::string>& GetGroundLayerWords();
bool ParseGroundLayers(const std::vector<std::pair<int, std::string>>& lines, std::vector<GroundLayer>& layers, std::string& error);

} // namespace poligon

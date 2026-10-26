#pragma once

#include <memory>
#include <vector>

#include "poligon/render/renderer.h"
#include "poligon/world/streaming.h"
#include "poligon/world/terrain_generator.h"

namespace poligon {

// Water surfaces for the lakes of generated land (poligon/world/terrain_generator.h).
//
// Each lake becomes one flat mesh at its level, made of squares of the map grid that hold
// water or touch it. The terrain, drawn first, hides the parts of a square that lie above
// the water, so the shore line follows the land's own heights (1 m), not the squares.
// How water looks is the renderer's part: Renderer::DrawWater and WaterMaterial.
//
//     std::vector<WaterSurface> lakes = CreateLakeSurfaces(renderer, maps, terrain_origin);
//     // every frame, between BeginFrame and EndFrame:
//     for (const WaterSurface& lake : lakes) renderer.DrawWater(lake.mesh, lake.origin, glm::mat4(1.0f), material);

struct WaterSurface {
    MeshHandle mesh;
    // Where the mesh lies in the world: the land's origin, at the lake's level.
    glm::dvec3 origin{0.0};
    // World height of the water surface.
    double level = 0.0;
};

// One surface per lake of `maps`. `terrain_origin` is TerrainConfig::origin of the terrain
// drawn from the same maps: the world position of the map's first point. `square_points`:
// how many map points along each side of a square (8 = 16 m with 2 m maps).
std::vector<WaterSurface> CreateLakeSurfaces(Renderer& renderer, const TerrainMaps& maps, const glm::dvec3& terrain_origin,
                                             int square_points = 8);

class LandSource;  // poligon/world/land.h

// The lakes of a streamed land (poligon/world/land.h), patch by patch: a Background source
// for WorldStreamer. Each patch's water is a mesh per lake made of the patch's squares that
// hold water, the same squares CreateLakeSurfaces makes for the whole land.
class WaterPatchSource : public PatchSource {
public:
    WaterPatchSource(Renderer& renderer, LandSource& land, int square_points = 8);
    ~WaterPatchSource() override;
    std::unique_ptr<PatchData> Load(PatchCoord patch) override;
    void Attach(PatchCoord patch, std::unique_ptr<PatchData> data) override;
    void Detach(PatchCoord patch) override;

    // Draws the water of every held patch (between BeginFrame and EndFrame).
    void Draw(Renderer& renderer, const WaterMaterial& material) const;
    int GetSurfaceCount() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace poligon

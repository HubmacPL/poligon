#pragma once

#include <vector>

#include "poligon/renderer.h"
#include "poligon/terrain_generator.h"

namespace poligon {

// Water surfaces for the lakes of generated land (poligon/terrain_generator.h).
//
// Each lake becomes one flat mesh at its level, made of squares of the map grid that hold
// water or touch it. The terrain, drawn first, hides the parts of a square that lie above
// the water, so the shore line follows the land's own heights (1 m), not the squares.
// How water looks is the renderer's part: Renderer::DrawWater and WaterMaterial.
//
//     std::vector<WaterSurface> lakes = CreateLakeSurfaces(renderer, maps, terrain_origin);
//     // every frame, between BeginFrame and EndFrame:
//     for (const WaterSurface& lake : lakes) renderer.DrawWater(lake.mesh, lake.transform, material);

struct WaterSurface {
    MeshHandle mesh;
    // Places the mesh in the world: at the lake's level, with the land's origin applied.
    glm::mat4 transform{1.0f};
    // World height of the water surface.
    float level = 0.0f;
};

// One surface per lake of `maps`. `terrain_origin` is TerrainConfig::origin of the terrain
// drawn from the same maps: the world position of the map's first point. `square_points`:
// how many map points along each side of a square (8 = 16 m with 2 m maps).
std::vector<WaterSurface> CreateLakeSurfaces(Renderer& renderer, const TerrainMaps& maps, const glm::vec3& terrain_origin,
                                             int square_points = 8);

} // namespace poligon

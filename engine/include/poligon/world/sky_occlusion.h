#pragma once

#include <cstdint>
#include <vector>

#include "poligon/core/math.h"
#include "poligon/render/renderer.h"

namespace poligon {

class Terrain;

// How much of the sky each place on the land sees: 1 = open sky, 0 = none. Valley floors
// and gullies see less of it (the land around rises above them), and so does the ground
// under tree crowns. The renderer darkens sky light by it (Renderer::SetSkyOcclusion):
// "large-scale occlusion", where screen-space ambient occlusion only sees what is on screen
// and within a meter or two.
//
// The map is a grid over a square of the world (x, z), one byte per point.
struct SkyOcclusionMap : public GroundMapSource {
    int resolution = 0;        // points along each side
    glm::dvec2 origin{0.0};    // world x and z of the first point
    float spacing = 4.0f;      // meters between points
    std::vector<std::uint8_t> values;
    // Raised by every change, so the renderer uploads only when needed.
    std::uint32_t version = 0;

    float Get(glm::dvec2 position) const;  // 1 outside the map
    // The map as the renderer reads it (Renderer::SetSkyOcclusion): texel centers lie on the
    // map's points, so the grid reaches half a spacing beyond the first and last point.
    GroundMap GetGroundMap() const override;
};

// The land's part: at every point, the share of the sky above the horizon the land around
// leaves open, from the horizon's height in eight directions out to `reach` meters. The
// square has `size` meters per side around `center` (world x, z).
SkyOcclusionMap MakeTerrainSkyOcclusion(const Terrain& terrain, glm::dvec2 center, float size, float spacing = 4.0f,
                                        float reach = 160.0f);

// A tree's part: its crown, `radius` meters around `position`, takes `strength` (0..1) of
// the sky from the ground under it, softly less towards the crown's edge. Overlapping
// crowns multiply.
void AddCanopy(SkyOcclusionMap& map, glm::dvec2 position, float radius, float strength);

} // namespace poligon

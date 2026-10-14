#pragma once

#include <memory>

#include "poligon/height_map.h"
#include "poligon/renderer.h"

namespace poligon {

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
    glm::vec3 origin{0.0f};
    TerrainMaterial material;
    // A square is split while the camera is nearer to it than its size times this.
    // Larger = finer land further out, and more to draw.
    float detail = 2.0f;
};

// What the last Draw did.
struct TerrainStats {
    int patches = 0;
    // Patches by size: index 0 = smallest (finest). Up to 16 sizes.
    int patches_per_level[16] = {};
};

class Terrain {
public:
    // The height map is copied; the caller may free it.
    Terrain(Renderer& renderer, const HeightMap& map, const TerrainConfig& config);
    ~Terrain();
    Terrain(const Terrain&) = delete;
    Terrain& operator=(const Terrain&) = delete;

    // Call between Renderer::BeginFrame and EndFrame, with the camera of that frame.
    void Draw(const Camera& camera);

    // The land at a world position (x, z): height, facing direction, steepness in degrees.
    // Outside the map the nearest edge counts.
    float GetHeight(float x, float z) const;
    glm::vec3 GetNormal(float x, float z) const;
    float GetSlopeDegrees(float x, float z) const;
    // The map's extent in world coordinates.
    glm::vec2 GetMin() const;
    glm::vec2 GetMax() const;
    const HeightMap& GetHeightMap() const;

    void SetMaterial(const TerrainMaterial& material);
    void SetDetail(float detail);
    const TerrainStats& GetStats() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace poligon

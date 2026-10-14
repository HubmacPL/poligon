#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "poligon/math.h"

namespace poligon {

// The shape of a piece of land as a grid of heights: one number per grid point, with the
// same distance between neighboring points in both directions. Terrain is drawn from it,
// physics collides with it, and plants are placed by it.
//
// Coordinates: x runs along a row, z from row to row, both in meters from the map's first
// point; heights are meters. A point (ix, iz) is at index iz * width + ix.
struct HeightMap {
    int width = 0;         // points along x
    int depth = 0;         // points along z
    float spacing = 1.0f;  // meters between neighboring points
    std::vector<float> heights;

    bool IsEmpty() const { return width < 2 || depth < 2 || heights.empty(); }
    // Size of the covered land in meters.
    float GetSizeX() const { return static_cast<float>(width - 1) * spacing; }
    float GetSizeZ() const { return static_cast<float>(depth - 1) * spacing; }

    // Height at any place on the map, blended from the four points around it. Places
    // outside the map get the height of the nearest edge.
    float GetHeight(float x, float z) const;
    // Direction the ground faces there (unit length, pointing up and away from the slope).
    glm::vec3 GetNormal(float x, float z) const;
    // Steepness there: 0 = level, 45 = as steep as it is far, 90 = a wall.
    float GetSlopeDegrees(float x, float z) const;
    void GetHeightRange(float& lowest, float& highest) const;
};

// Rolling hills from layered noise; the same arguments always give the same map.
// hill_size: distance from one hill to the next, roughly (meters). hill_height: from the
// lowest valley to the highest top, roughly (meters).
HeightMap MakeNoiseHeightMap(int width, int depth, float spacing, std::uint32_t seed, float hill_size, float hill_height);

// Reads a TIFF file that stores one 32-bit floating-point height per pixel, without
// compression, in strips. That is exactly what Poland's Geoportal elevation service (WCS)
// returns; it is not a general TIFF reader. `spacing` is not in such a file's basic part
// and must be given. Pixels the file marks as "no data" (0 or below -1000) are counted in
// `missing_count`; they are left as they are.
bool LoadHeightMapFromTiff(const char* path, float spacing, HeightMap& map, int& missing_count, std::string& error);

// Gives every point without data (0, below -1000, or not a number) the average of its
// neighbors that have data, working inwards from the rim of each hole. Elevation services
// leave such pinholes. Returns how many points were filled.
int FillMissingHeights(HeightMap& map);

// Poligon's own height map file: a line of text ("poligon-heightmap 1 WIDTH DEPTH SPACING")
// followed by the heights as 32-bit floats. Loads much faster than joining tiles again.
bool SaveHeightMap(const char* path, const HeightMap& map);
bool LoadHeightMap(const char* path, HeightMap& map, std::string& error);

// Copies `tile` into `map` with the tile's first point at (ix, iz). Parts that fall outside are dropped.
void PasteHeightMap(HeightMap& map, const HeightMap& tile, int ix, int iz);

// Changes real land into game land: `horizontal_scale` 0.5 halves all distances (a 12 km
// square becomes 6 km, slopes get twice as steep), `vertical_scale` multiplies heights,
// and the lowest point is moved to height 0. The number of points stays the same.
void RescaleHeightMap(HeightMap& map, float horizontal_scale, float vertical_scale);

} // namespace poligon

// Example: find out whether a box behind a hill can be left out of drawing.
// Vegetation and Terrain take a TerrainOcclusion with SetOcclusion and do this per cell.
#include <cstdio>

#include "poligon/world/terrain_occlusion.h"

int main() {
    // Flat land 2 km x 2 km, 8 m between points, with a 60 m high ridge across x = 1000 m.
    poligon::HeightMap floor;
    floor.width = 251;
    floor.depth = 251;
    floor.spacing = 8.0f;
    floor.heights.resize(static_cast<size_t>(floor.width) * floor.depth, 0.0f);
    for (int iz = 0; iz < floor.depth; ++iz) {
        for (int ix = 120; ix <= 130; ++ix) {
            floor.heights[static_cast<size_t>(iz) * floor.width + ix] = 60.0f;
        }
    }

    poligon::TerrainOcclusion occlusion;
    occlusion.SetFloor(&floor, glm::dvec3(0.0));
    occlusion.Update(glm::dvec3(500.0, 2.0, 1000.0));  // the camera, 2 m above the ground

    // A 20 m tree cell behind the ridge, and one on the camera's side of it.
    const bool behind = occlusion.IsHidden(glm::dvec3(1500.0, 0.0, 990.0), glm::dvec3(1520.0, 20.0, 1010.0));
    const bool in_front = occlusion.IsHidden(glm::dvec3(700.0, 0.0, 990.0), glm::dvec3(720.0, 20.0, 1010.0));
    std::printf("cell behind the ridge hidden: %d\n", behind);
    std::printf("cell in front of the ridge hidden: %d\n", in_front);
    return (behind && !in_front) ? 0 : 1;
}

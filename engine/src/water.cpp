#include "poligon/water.h"

#include <cmath>
#include <cstdint>

#include "poligon/fatal_error.h"

namespace poligon {

std::vector<WaterSurface> CreateLakeSurfaces(Renderer& renderer, const TerrainMaps& maps, const glm::vec3& terrain_origin,
                                             int square_points) {
    if (square_points < 1) {
        FatalError("CreateLakeSurfaces: square_points must be at least 1");
    }
    std::vector<WaterSurface> surfaces;
    if (maps.water_level.empty()) {
        return surfaces;
    }
    const int columns = (maps.map_width - 1 + square_points - 1) / square_points;
    const int rows = (maps.map_depth - 1 + square_points - 1) / square_points;
    const float square_size = static_cast<float>(square_points) * maps.map_spacing;
    for (const Lake& lake : maps.lakes) {
        // The squares that hold this lake's water (its level, give or take a millimeter).
        std::vector<MeshVertex> vertices;
        std::vector<std::uint32_t> indices;
        for (int row = 0; row < rows; ++row) {
            for (int column = 0; column < columns; ++column) {
                bool wet = false;
                for (int z = row * square_points; z <= (row + 1) * square_points && z < maps.map_depth && !wet; ++z) {
                    for (int x = column * square_points; x <= (column + 1) * square_points && x < maps.map_width && !wet; ++x) {
                        const float level =
                            maps.water_level[static_cast<size_t>(z) * static_cast<size_t>(maps.map_width) + static_cast<size_t>(x)];
                        wet = level != TerrainMaps::kNoWater && std::fabs(level - lake.level) < 0.001f;
                    }
                }
                if (!wet) {
                    continue;
                }
                const float x0 = static_cast<float>(column) * square_size;
                const float z0 = static_cast<float>(row) * square_size;
                const std::uint32_t first = static_cast<std::uint32_t>(vertices.size());
                for (int corner = 0; corner < 4; ++corner) {
                    MeshVertex vertex;
                    vertex.position = {x0 + (corner & 1 ? square_size : 0.0f), 0.0f, z0 + (corner & 2 ? square_size : 0.0f)};
                    vertex.normal = {0.0f, 1.0f, 0.0f};
                    vertex.uv = {vertex.position.x, vertex.position.z};
                    vertices.push_back(vertex);
                }
                // Counter-clockwise seen from above (+Y).
                for (const std::uint32_t corner : {0u, 2u, 1u, 1u, 2u, 3u}) {
                    indices.push_back(first + corner);
                }
            }
        }
        if (vertices.empty()) {
            continue;
        }
        WaterSurface surface;
        surface.mesh = renderer.CreateMesh(vertices.data(), static_cast<int>(vertices.size()), indices.data(),
                                           static_cast<int>(indices.size()));
        surface.level = lake.level + terrain_origin.y;
        surface.transform = glm::translate(glm::mat4(1.0f), glm::vec3(terrain_origin.x, surface.level, terrain_origin.z));
        surfaces.push_back(surface);
    }
    return surfaces;
}

} // namespace poligon

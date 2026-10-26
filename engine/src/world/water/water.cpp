#include "poligon/world/water.h"

#include <cmath>
#include <cstdint>
#include <map>

#include "poligon/core/fatal_error.h"
#include "poligon/world/land.h"

namespace poligon {

namespace {

// One lake's squares of water in a range of the map grid, as a mesh relative to a corner.
struct LakeMesh {
    int lake = 0;
    std::vector<MeshVertex> vertices;
    std::vector<std::uint32_t> indices;
};

// The squares of water (`square_points` map points on a side) with indices [first_column,
// end_column) x [first_row, end_row) of the whole map's squares, one mesh per lake. Vertices
// are relative to map point (corner_x, corner_z). `maps` may be a window that covers the squares.
std::vector<LakeMesh> BuildLakeMeshes(const TerrainMaps& maps, int first_column, int first_row, int end_column, int end_row,
                                      int square_points, int corner_x, int corner_z) {
    std::vector<LakeMesh> meshes;
    const float square_size = static_cast<float>(square_points) * maps.map_spacing;
    const int last_x = maps.map_first_x + maps.map_width - 1;
    const int last_z = maps.map_first_z + maps.map_depth - 1;
    for (size_t lake_index = 0; lake_index < maps.lakes.size(); ++lake_index) {
        const Lake& lake = maps.lakes[lake_index];
        LakeMesh mesh;
        mesh.lake = static_cast<int>(lake_index);
        for (int row = first_row; row < end_row; ++row) {
            for (int column = first_column; column < end_column; ++column) {
                // The square holds this lake's water (its level, give or take a millimeter).
                bool wet = false;
                for (int z = row * square_points; z <= (row + 1) * square_points && z <= last_z && !wet; ++z) {
                    for (int x = column * square_points; x <= (column + 1) * square_points && x <= last_x && !wet; ++x) {
                        if (x < maps.map_first_x || z < maps.map_first_z) {
                            continue;
                        }
                        const float level = maps.water_level[static_cast<size_t>(z - maps.map_first_z) * static_cast<size_t>(maps.map_width) +
                                                             static_cast<size_t>(x - maps.map_first_x)];
                        wet = level != TerrainMaps::kNoWater && std::fabs(level - lake.level) < 0.001f;
                    }
                }
                if (!wet) {
                    continue;
                }
                const float x0 = static_cast<float>(column * square_points - corner_x) * maps.map_spacing;
                const float z0 = static_cast<float>(row * square_points - corner_z) * maps.map_spacing;
                const std::uint32_t first = static_cast<std::uint32_t>(mesh.vertices.size());
                for (int corner = 0; corner < 4; ++corner) {
                    MeshVertex vertex;
                    vertex.position = {x0 + (corner & 1 ? square_size : 0.0f), 0.0f, z0 + (corner & 2 ? square_size : 0.0f)};
                    vertex.normal = {0.0f, 1.0f, 0.0f};
                    vertex.uv = {vertex.position.x, vertex.position.z};
                    mesh.vertices.push_back(vertex);
                }
                // Counter-clockwise seen from above (+Y).
                for (const std::uint32_t corner : {0u, 2u, 1u, 1u, 2u, 3u}) {
                    mesh.indices.push_back(first + corner);
                }
            }
        }
        if (!mesh.vertices.empty()) {
            meshes.push_back(std::move(mesh));
        }
    }
    return meshes;
}

WaterSurface UploadLakeMesh(Renderer& renderer, const LakeMesh& mesh, double level, const glm::dvec3& corner) {
    WaterSurface surface;
    surface.mesh = renderer.CreateMesh(mesh.vertices.data(), static_cast<int>(mesh.vertices.size()), mesh.indices.data(),
                                       static_cast<int>(mesh.indices.size()));
    surface.level = level;
    surface.origin = glm::dvec3(corner.x, level, corner.z);
    return surface;
}

} // namespace

std::vector<WaterSurface> CreateLakeSurfaces(Renderer& renderer, const TerrainMaps& maps, const glm::dvec3& terrain_origin,
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
    for (const LakeMesh& mesh : BuildLakeMeshes(maps, 0, 0, columns, rows, square_points, 0, 0)) {
        const double level = static_cast<double>(maps.lakes[static_cast<size_t>(mesh.lake)].level) + terrain_origin.y;
        surfaces.push_back(UploadLakeMesh(renderer, mesh, level, terrain_origin));
    }
    return surfaces;
}

namespace {

struct WaterPatchData : PatchData {
    std::vector<LakeMesh> meshes;
};

} // namespace

struct WaterPatchSource::Impl {
    Renderer* renderer = nullptr;
    LandSource* land = nullptr;
    int square_points = 8;
    std::map<PatchCoord, std::vector<WaterSurface>> held;
};

WaterPatchSource::WaterPatchSource(Renderer& renderer, LandSource& land, int square_points) : impl_(std::make_unique<Impl>()) {
    const LandInfo& info = land.GetInfo();
    const double patch_map_points = info.grid.patch_size / static_cast<double>(info.map_spacing);
    if (square_points < 1 || std::fmod(patch_map_points, static_cast<double>(square_points)) != 0.0) {
        FatalError("WaterPatchSource: a patch must hold a whole number of squares");
    }
    impl_->renderer = &renderer;
    impl_->land = &land;
    impl_->square_points = square_points;
}

WaterPatchSource::~WaterPatchSource() {
    for (auto& [patch, surfaces] : impl_->held) {
        for (const WaterSurface& surface : surfaces) {
            impl_->renderer->DestroyMesh(surface.mesh);
        }
    }
}

std::unique_ptr<PatchData> WaterPatchSource::Load(PatchCoord patch) {
    const LandInfo& info = impl_->land->GetInfo();
    const std::shared_ptr<const LandPatch> land = impl_->land->GetPatch(patch);
    auto data = std::make_unique<WaterPatchData>();
    if (land->maps.water_level.empty()) {
        return data;
    }
    const int squares = static_cast<int>(std::lround(info.grid.patch_size / static_cast<double>(info.map_spacing))) / impl_->square_points;
    const int land_columns = (static_cast<int>(std::lround(info.size_x / info.map_spacing)) + impl_->square_points - 1) / impl_->square_points;
    const int land_rows = (static_cast<int>(std::lround(info.size_z / info.map_spacing)) + impl_->square_points - 1) / impl_->square_points;
    const int first_column = patch.x * squares;
    const int first_row = patch.z * squares;
    data->meshes = BuildLakeMeshes(land->maps, first_column, first_row, std::min(first_column + squares, land_columns),
                                   std::min(first_row + squares, land_rows), impl_->square_points, first_column * impl_->square_points,
                                   first_row * impl_->square_points);
    return data;
}

void WaterPatchSource::Attach(PatchCoord patch, std::unique_ptr<PatchData> data) {
    const LandInfo& info = impl_->land->GetInfo();
    const glm::dvec3 corner = info.world_origin + glm::dvec3(static_cast<double>(patch.x) * info.grid.patch_size, 0.0,
                                                             static_cast<double>(patch.z) * info.grid.patch_size);
    std::vector<WaterSurface>& surfaces = impl_->held[patch];
    for (const LakeMesh& mesh : static_cast<WaterPatchData&>(*data).meshes) {
        const double level = static_cast<double>(info.lakes[static_cast<size_t>(mesh.lake)].level) + info.world_origin.y;
        surfaces.push_back(UploadLakeMesh(*impl_->renderer, mesh, level, corner));
    }
}

void WaterPatchSource::Detach(PatchCoord patch) {
    const auto found = impl_->held.find(patch);
    if (found == impl_->held.end()) {
        return;
    }
    for (const WaterSurface& surface : found->second) {
        impl_->renderer->DestroyMesh(surface.mesh);
    }
    impl_->held.erase(found);
}

void WaterPatchSource::Draw(Renderer& renderer, const WaterMaterial& material) const {
    for (const auto& [patch, surfaces] : impl_->held) {
        for (const WaterSurface& surface : surfaces) {
            renderer.DrawWater(surface.mesh, surface.origin, glm::mat4(1.0f), material);
        }
    }
}

int WaterPatchSource::GetSurfaceCount() const {
    int count = 0;
    for (const auto& [patch, surfaces] : impl_->held) {
        count += static_cast<int>(surfaces.size());
    }
    return count;
}

} // namespace poligon

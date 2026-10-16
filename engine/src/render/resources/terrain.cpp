// Terrain height images and the shared grid mesh their patches are drawn with.

#include "../renderer/renderer_internal.h"

namespace poligon {

TerrainHandle Renderer::CreateTerrain(const float* heights, int width, int depth, float spacing, const glm::vec3& origin) {
    if (heights == nullptr || width < 2 || depth < 2 || width > 16384 || depth > 16384 || spacing <= 0.0f) {
        FatalError("Renderer::CreateTerrain: needs between 2 and 16384 points each way and a spacing above zero");
    }
    if (impl_->in_frame) {
        FatalError("Renderer::CreateTerrain: do not create terrains between BeginFrame and EndFrame");
    }
    const size_t point_count = static_cast<size_t>(width) * static_cast<size_t>(depth);
    float lowest = heights[0];
    float highest = heights[0];
    for (size_t i = 1; i < point_count; ++i) {
        lowest = glm::min(lowest, heights[i]);
        highest = glm::max(highest, heights[i]);
    }
    TerrainData terrain;
    terrain.width = width;
    terrain.depth = depth;
    terrain.spacing = spacing;
    terrain.origin = {origin.x, origin.y + lowest, origin.z};
    terrain.height_range = glm::max(highest - lowest, 0.001f);

    SDL_GPUTextureCreateInfo info{};
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = kHeightFormat;
    info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    info.width = static_cast<Uint32>(width);
    info.height = static_cast<Uint32>(depth);
    info.layer_count_or_depth = 1;
    info.num_levels = 1;
    info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    terrain.height_texture = SDL_CreateGPUTexture(impl_->device, &info);
    if (terrain.height_texture == nullptr) {
        FatalSdlError("cannot create terrain height texture");
    }

    std::vector<std::uint16_t> values(point_count);
    for (size_t i = 0; i < point_count; ++i) {
        values[i] = static_cast<std::uint16_t>((heights[i] - lowest) / terrain.height_range * 65535.0f + 0.5f);
    }
    SDL_GPUTransferBuffer* transfer =
        impl_->CreateFilledTransferBuffer(values.data(), static_cast<Uint32>(point_count * sizeof(std::uint16_t)));
    SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(impl_->device);
    if (commands == nullptr) {
        FatalSdlError("cannot acquire command buffer");
    }
    SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(commands);
    SDL_GPUTextureTransferInfo source{};
    source.transfer_buffer = transfer;
    source.pixels_per_row = static_cast<Uint32>(width);
    source.rows_per_layer = static_cast<Uint32>(depth);
    SDL_GPUTextureRegion destination{};
    destination.texture = terrain.height_texture;
    destination.w = static_cast<Uint32>(width);
    destination.h = static_cast<Uint32>(depth);
    destination.d = 1;
    SDL_UploadToGPUTexture(copy_pass, &source, &destination, false);
    SDL_EndGPUCopyPass(copy_pass);
    if (!SDL_SubmitGPUCommandBuffer(commands)) {
        FatalSdlError("cannot submit terrain upload");
    }
    SDL_ReleaseGPUTransferBuffer(impl_->device, transfer);

    // The grid mesh all patches share: a flat square from (0, 0) to (1, 1) with a rim of
    // "skirt" vertices (y = -1) that the vertex shader pushes down.
    if (impl_->terrain_grid.id == 0) {
        const int cells = kTerrainGridCells;
        const int row = cells + 1;
        std::vector<MeshVertex> vertices;
        std::vector<std::uint32_t> indices;
        for (int j = 0; j < row; ++j) {
            for (int i = 0; i < row; ++i) {
                const float x = static_cast<float>(i) / static_cast<float>(cells);
                const float z = static_cast<float>(j) / static_cast<float>(cells);
                vertices.push_back({{x, 0.0f, z}, {0.0f, 1.0f, 0.0f}, {x, z}});
            }
        }
        for (int j = 0; j < cells; ++j) {
            for (int i = 0; i < cells; ++i) {
                const std::uint32_t corner = static_cast<std::uint32_t>(j * row + i);
                const std::uint32_t next_row = corner + static_cast<std::uint32_t>(row);
                indices.insert(indices.end(), {corner, next_row, corner + 1, corner + 1, next_row, next_row + 1});
            }
        }
        // Skirts along the four edges, drawn from both sides so that no winding rule can hide them.
        const auto add_skirt = [&vertices, &indices](std::uint32_t a, std::uint32_t b) {
            const std::uint32_t low_a = static_cast<std::uint32_t>(vertices.size());
            MeshVertex vertex = vertices[a];
            vertex.position.y = -1.0f;
            vertices.push_back(vertex);
            vertex = vertices[b];
            vertex.position.y = -1.0f;
            vertices.push_back(vertex);
            const std::uint32_t low_b = low_a + 1;
            indices.insert(indices.end(), {a, b, low_a, low_a, b, low_b, a, low_a, b, b, low_a, low_b});
        };
        for (int i = 0; i < cells; ++i) {
            const std::uint32_t step = static_cast<std::uint32_t>(i);
            const std::uint32_t last = static_cast<std::uint32_t>(cells);
            const std::uint32_t width_row = static_cast<std::uint32_t>(row);
            add_skirt(step, step + 1);                                          // z = 0 edge
            add_skirt(last * width_row + step, last * width_row + step + 1);    // z = 1 edge
            add_skirt(step * width_row, (step + 1) * width_row);                // x = 0 edge
            add_skirt(step * width_row + last, (step + 1) * width_row + last);  // x = 1 edge
        }
        impl_->terrain_grid =
            CreateMesh(vertices.data(), static_cast<int>(vertices.size()), indices.data(), static_cast<int>(indices.size()));
    }

    impl_->terrains.push_back(terrain);
    TerrainHandle handle;
    handle.id = static_cast<std::uint32_t>(impl_->terrains.size());
    return handle;
}

void Renderer::DestroyTerrain(TerrainHandle terrain) {
    if (impl_->in_frame) {
        FatalError("Renderer::DestroyTerrain: do not destroy terrains between BeginFrame and EndFrame");
    }
    if (terrain.id == 0 || terrain.id > impl_->terrains.size() || impl_->terrains[terrain.id - 1].height_texture == nullptr) {
        FatalError("Renderer::DestroyTerrain: invalid or already destroyed terrain handle");
    }
    SDL_ReleaseGPUTexture(impl_->device, impl_->terrains[terrain.id - 1].height_texture);
    impl_->terrains[terrain.id - 1].height_texture = nullptr;
}

} // namespace poligon

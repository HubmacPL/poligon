// Terrain height images and the shared grid mesh their patches are drawn with.

#include "../renderer/renderer_internal.h"

namespace poligon {

namespace {

// A texture array of `layers` images on the graphics card.
SDL_GPUTexture* CreateArrayTexture(SDL_GPUDevice* device, SDL_GPUTextureFormat format, int width, int height, int layers) {
    SDL_GPUTextureCreateInfo info{};
    info.type = SDL_GPU_TEXTURETYPE_2D_ARRAY;
    info.format = format;
    info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    info.width = static_cast<Uint32>(width);
    info.height = static_cast<Uint32>(height);
    info.layer_count_or_depth = static_cast<Uint32>(layers);
    info.num_levels = 1;
    info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    SDL_GPUTexture* texture = CreateTrackedTexture(device, &info);
    if (texture == nullptr) {
        FatalSdlError("cannot create terrain texture array");
    }
    return texture;
}

} // namespace

// Heights become 16-bit values between the page's lowest and highest point.
void Renderer::Impl::SetTerrainHeights(TerrainData& terrain, int page, const float* heights, const glm::dvec3& first_point) {
    const size_t point_count = static_cast<size_t>(terrain.width) * static_cast<size_t>(terrain.depth);
    float lowest = heights[0];
    float highest = heights[0];
    for (size_t i = 1; i < point_count; ++i) {
        lowest = glm::min(lowest, heights[i]);
        highest = glm::max(highest, heights[i]);
    }
    TerrainPage& target = terrain.pages[static_cast<size_t>(page)];
    target.origin = {first_point.x, first_point.y + lowest, first_point.z};
    target.last_point = {static_cast<float>(terrain.width - 1), static_cast<float>(terrain.depth - 1)};
    target.height_range = glm::max(highest - lowest, 0.001f);
    std::vector<std::uint16_t> values(point_count);
    for (size_t i = 0; i < point_count; ++i) {
        values[i] = static_cast<std::uint16_t>((heights[i] - lowest) / target.height_range * 65535.0f + 0.5f);
    }
    UploadTextureLayer(terrain.height_texture, static_cast<Uint32>(page), values.data(), terrain.width, terrain.depth, 2);
}

namespace {

// The grid mesh all patches share: a flat square from (0, 0) to (1, 1) with a rim of
// "skirt" vertices (y = -1) that the vertex shader pushes down.
MeshHandle CreateTerrainGridMesh(Renderer& renderer) {
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
    return renderer.CreateMesh(vertices.data(), static_cast<int>(vertices.size()), indices.data(), static_cast<int>(indices.size()));
}

} // namespace

TerrainHandle Renderer::CreateTerrain(const float* heights, int width, int depth, float spacing, const glm::dvec3& origin) {
    if (heights == nullptr || width < 2 || depth < 2 || width > 16384 || depth > 16384 || spacing <= 0.0f) {
        FatalError("Renderer::CreateTerrain: needs between 2 and 16384 points each way and a spacing above zero");
    }
    if (impl_->in_frame) {
        FatalError("Renderer::CreateTerrain: do not create terrains between BeginFrame and EndFrame");
    }
    TerrainData terrain;
    terrain.width = width;
    terrain.depth = depth;
    terrain.spacing = spacing;
    terrain.pages.resize(1);
    terrain.height_texture = CreateArrayTexture(impl_->device, kHeightFormat, width, depth, 1);
    impl_->SetTerrainHeights(terrain, 0, heights, origin);
    if (impl_->terrain_grid.id == 0) {
        impl_->terrain_grid = CreateTerrainGridMesh(*this);
    }
    impl_->terrains.push_back(terrain);
    TerrainHandle handle;
    handle.id = static_cast<std::uint32_t>(impl_->terrains.size());
    return handle;
}

TerrainHandle Renderer::CreateTerrainPages(int points, int page_count, float spacing, int weight_points, int border_points) {
    if (points < 2 || points > 4097 || page_count < 1 || page_count > 2048 || spacing <= 0.0f || weight_points < 0 ||
        weight_points == 1 || weight_points > 4097 || border_points < 0 || points - 2 * border_points < 2) {
        FatalError("Renderer::CreateTerrainPages: needs 2 to 4097 points, 1 to 2048 pages, a spacing above zero and 0 or "
                   "2 to 4097 weight points");
    }
    if (impl_->in_frame) {
        FatalError("Renderer::CreateTerrainPages: do not create terrains between BeginFrame and EndFrame");
    }
    TerrainData terrain;
    terrain.width = points;
    terrain.depth = points;
    terrain.spacing = spacing;
    terrain.pages.resize(static_cast<size_t>(page_count));
    terrain.paged = true;
    terrain.border_points = border_points;
    terrain.height_texture = CreateArrayTexture(impl_->device, kHeightFormat, points, points, page_count);
    if (weight_points > 0) {
        terrain.weight_points = weight_points;
        terrain.paged_weights = true;
        terrain.low_weights = CreateArrayTexture(impl_->device, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, weight_points, weight_points, page_count);
        terrain.high_weights = CreateArrayTexture(impl_->device, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, weight_points, weight_points, page_count);
    }
    if (impl_->terrain_grid.id == 0) {
        impl_->terrain_grid = CreateTerrainGridMesh(*this);
    }
    impl_->terrains.push_back(terrain);
    TerrainHandle handle;
    handle.id = static_cast<std::uint32_t>(impl_->terrains.size());
    return handle;
}

void Renderer::SetTerrainPage(TerrainHandle terrain, int page, const float* heights, const glm::dvec3& origin, const Image* weights,
                              int land_points_x, int land_points_z) {
    if (impl_->in_frame) {
        FatalError("Renderer::SetTerrainPage: do not fill pages between BeginFrame and EndFrame");
    }
    if (terrain.id == 0 || terrain.id > impl_->terrains.size() || impl_->terrains[terrain.id - 1].height_texture == nullptr) {
        FatalError("Renderer::SetTerrainPage: invalid or destroyed terrain handle");
    }
    TerrainData& data = impl_->terrains[terrain.id - 1];
    if (heights == nullptr || page < 0 || page >= static_cast<int>(data.pages.size())) {
        FatalError("Renderer::SetTerrainPage: needs heights and a page the terrain has");
    }
    impl_->SetTerrainHeights(data, page, heights, origin);
    TerrainPage& filled = data.pages[static_cast<size_t>(page)];
    filled.last_point = {static_cast<float>((land_points_x > 0 ? glm::min(land_points_x, data.width) : data.width) - 1),
                         static_cast<float>((land_points_z > 0 ? glm::min(land_points_z, data.depth) : data.depth) - 1)};
    if (data.paged_weights && weights != nullptr) {
        for (int i = 0; i < 2; ++i) {
            const Image& image = weights[i];
            if (image.width != data.weight_points || image.height != data.weight_points ||
                image.pixels.size() != static_cast<size_t>(image.width) * static_cast<size_t>(image.height) * 4) {
                FatalError("Renderer::SetTerrainPage: the weight images must be weight_points x weight_points");
            }
            impl_->UploadTextureLayer(i == 0 ? data.low_weights : data.high_weights, static_cast<Uint32>(page), image.pixels.data(),
                                      image.width, image.height, 4);
        }
    }
}

// A terrain of one map keeps its material's weight images as one-layer arrays, copied on the
// graphics card when the material names other images.
void Renderer::Impl::PrepareTerrainWeights(TerrainData& terrain, const TerrainMaterial& material) {
    if (terrain.paged_weights) {
        return;
    }
    for (int i = 0; i < 2; ++i) {
        const TextureHandle handle = material.weights[i];
        if (handle.id == terrain.copied_weights[i]) {
            continue;
        }
        SDL_GPUTexture*& target = i == 0 ? terrain.low_weights : terrain.high_weights;
        if (target != nullptr) {
            ReleaseTrackedTexture(device, target);
            target = nullptr;
        }
        terrain.copied_weights[i] = handle.id;
        if (handle.id == 0) {
            continue;
        }
        const glm::vec2 size = texture_sizes[handle.id - 1];
        const int width = static_cast<int>(size.x);
        const int height = static_cast<int>(size.y);
        target = CreateArrayTexture(device, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, width, height, 1);
        SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(device);
        if (commands == nullptr) {
            FatalSdlError("cannot acquire command buffer");
        }
        SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(commands);
        SDL_GPUTextureLocation from{};
        from.texture = textures[handle.id - 1];
        SDL_GPUTextureLocation to{};
        to.texture = target;
        SDL_CopyGPUTextureToTexture(copy_pass, &from, &to, static_cast<Uint32>(width), static_cast<Uint32>(height), 1, false);
        SDL_EndGPUCopyPass(copy_pass);
        if (!SDL_SubmitGPUCommandBuffer(commands)) {
            FatalSdlError("cannot submit terrain weight copy");
        }
    }
}

void Renderer::DestroyTerrain(TerrainHandle terrain) {
    if (impl_->in_frame) {
        FatalError("Renderer::DestroyTerrain: do not destroy terrains between BeginFrame and EndFrame");
    }
    if (terrain.id == 0 || terrain.id > impl_->terrains.size() || impl_->terrains[terrain.id - 1].height_texture == nullptr) {
        FatalError("Renderer::DestroyTerrain: invalid or already destroyed terrain handle");
    }
    TerrainData& data = impl_->terrains[terrain.id - 1];
    for (SDL_GPUTexture** texture : {&data.height_texture, &data.low_weights, &data.high_weights}) {
        if (*texture != nullptr) {
            ReleaseTrackedTexture(impl_->device, *texture);
            *texture = nullptr;
        }
    }
}

} // namespace poligon

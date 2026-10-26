// Counting the memory of the textures and buffers the renderer makes (FrameStats::texture_bytes,
// buffer_bytes). SDL GPU does not say how much memory the card uses, so the renderer adds up
// what it asks for: exact for its own resources, without what the driver adds.

#include "../renderer/renderer_internal.h"

namespace poligon {

namespace {

// Bytes per texel, or per 4 x 4 block for block-compressed formats (then `block` is true).
std::uint64_t TexelBytes(SDL_GPUTextureFormat format, bool& block) {
    block = false;
    switch (format) {
    case SDL_GPU_TEXTUREFORMAT_BC1_RGBA_UNORM:
    case SDL_GPU_TEXTUREFORMAT_BC1_RGBA_UNORM_SRGB:
        block = true;
        return 8;
    case SDL_GPU_TEXTUREFORMAT_BC3_RGBA_UNORM:
    case SDL_GPU_TEXTUREFORMAT_BC3_RGBA_UNORM_SRGB:
    case SDL_GPU_TEXTUREFORMAT_BC5_RG_UNORM:
        block = true;
        return 16;
    case SDL_GPU_TEXTUREFORMAT_R8_UNORM:
        return 1;
    case SDL_GPU_TEXTUREFORMAT_R16_UNORM:
    case SDL_GPU_TEXTUREFORMAT_R16_FLOAT:
    case SDL_GPU_TEXTUREFORMAT_R8G8_UNORM:
    case SDL_GPU_TEXTUREFORMAT_D16_UNORM:
        return 2;
    case SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT:
    case SDL_GPU_TEXTUREFORMAT_R32G32_FLOAT:
        return 8;
    case SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT:
        return 16;
    default:
        return 4;  // RGBA8, R32, RG16, R11G11B10, D32 and D24S8
    }
}

struct TrackedMemory {
    std::unordered_map<const void*, std::uint64_t> sizes;
    std::uint64_t texture_bytes = 0;
    std::uint64_t buffer_bytes = 0;
};

TrackedMemory& Tracked() {
    static TrackedMemory memory;
    return memory;
}

} // namespace

namespace render_internal {

SDL_GPUTexture* CreateTrackedTexture(SDL_GPUDevice* device, const SDL_GPUTextureCreateInfo* info) {
    SDL_GPUTexture* texture = SDL_CreateGPUTexture(device, info);
    if (texture == nullptr) {
        return nullptr;
    }
    bool block = false;
    const std::uint64_t unit = TexelBytes(info->format, block);
    const std::uint64_t layers = info->type == SDL_GPU_TEXTURETYPE_3D ? 1 : info->layer_count_or_depth;
    const std::uint64_t depth = info->type == SDL_GPU_TEXTURETYPE_3D ? info->layer_count_or_depth : 1;
    std::uint64_t bytes = 0;
    for (Uint32 level = 0; level < glm::max(info->num_levels, 1u); ++level) {
        const std::uint64_t width = glm::max(info->width >> level, 1u);
        const std::uint64_t height = glm::max(info->height >> level, 1u);
        const std::uint64_t level_depth = glm::max<std::uint64_t>(depth >> level, 1);
        bytes += block ? ((width + 3) / 4) * ((height + 3) / 4) * unit * level_depth : width * height * unit * level_depth;
    }
    const std::uint64_t samples = info->sample_count == SDL_GPU_SAMPLECOUNT_8   ? 8
                                  : info->sample_count == SDL_GPU_SAMPLECOUNT_4 ? 4
                                  : info->sample_count == SDL_GPU_SAMPLECOUNT_2 ? 2
                                                                                : 1;
    bytes *= layers * samples;
    TrackedMemory& memory = Tracked();
    memory.sizes[texture] = bytes;
    memory.texture_bytes += bytes;
    return texture;
}

void ReleaseTrackedTexture(SDL_GPUDevice* device, SDL_GPUTexture* texture) {
    if (texture == nullptr) {
        return;
    }
    TrackedMemory& memory = Tracked();
    const auto found = memory.sizes.find(texture);
    if (found != memory.sizes.end()) {
        memory.texture_bytes -= found->second;
        memory.sizes.erase(found);
    }
    SDL_ReleaseGPUTexture(device, texture);
}

SDL_GPUBuffer* CreateTrackedBuffer(SDL_GPUDevice* device, const SDL_GPUBufferCreateInfo* info) {
    SDL_GPUBuffer* buffer = SDL_CreateGPUBuffer(device, info);
    if (buffer != nullptr) {
        TrackedMemory& memory = Tracked();
        memory.sizes[buffer] = info->size;
        memory.buffer_bytes += info->size;
    }
    return buffer;
}

void ReleaseTrackedBuffer(SDL_GPUDevice* device, SDL_GPUBuffer* buffer) {
    if (buffer == nullptr) {
        return;
    }
    TrackedMemory& memory = Tracked();
    const auto found = memory.sizes.find(buffer);
    if (found != memory.sizes.end()) {
        memory.buffer_bytes -= found->second;
        memory.sizes.erase(found);
    }
    SDL_ReleaseGPUBuffer(device, buffer);
}

std::uint64_t GetTrackedTextureBytes() {
    return Tracked().texture_bytes;
}

std::uint64_t GetTrackedBufferBytes() {
    return Tracked().buffer_bytes;
}

} // namespace render_internal

} // namespace poligon

// Copying data to the graphics card: buffers, small images, per-frame matrices.

#include "../renderer/renderer_internal.h"

namespace poligon {

void Renderer::Impl::UploadR8(SDL_GPUTexture* texture, const std::uint8_t* values, int resolution) {
    SDL_GPUTransferBuffer* transfer =
        CreateFilledTransferBuffer(values, static_cast<Uint32>(resolution) * static_cast<Uint32>(resolution));
    SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(device);
    if (commands == nullptr) {
        FatalSdlError("cannot acquire command buffer");
    }
    SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(commands);
    SDL_GPUTextureTransferInfo source{};
    source.transfer_buffer = transfer;
    source.pixels_per_row = static_cast<Uint32>(resolution);
    source.rows_per_layer = static_cast<Uint32>(resolution);
    SDL_GPUTextureRegion destination{};
    destination.texture = texture;
    destination.w = static_cast<Uint32>(resolution);
    destination.h = static_cast<Uint32>(resolution);
    destination.d = 1;
    SDL_UploadToGPUTexture(copy_pass, &source, &destination, false);
    SDL_EndGPUCopyPass(copy_pass);
    if (!SDL_SubmitGPUCommandBuffer(commands)) {
        FatalSdlError("cannot submit texture upload");
    }
    SDL_ReleaseGPUTransferBuffer(device, transfer);
}

SDL_GPUTransferBuffer* Renderer::Impl::CreateFilledTransferBuffer(const void* data, Uint32 size) {
    SDL_GPUTransferBufferCreateInfo transfer_info{};
    transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transfer_info.size = size;
    SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(device, &transfer_info);
    if (transfer == nullptr) {
        FatalSdlError("cannot create upload transfer buffer");
    }
    void* mapped = SDL_MapGPUTransferBuffer(device, transfer, false);
    if (mapped == nullptr) {
        FatalSdlError("cannot map upload transfer buffer");
    }
    std::memcpy(mapped, data, size);
    SDL_UnmapGPUTransferBuffer(device, transfer);
    return transfer;
}

SDL_GPUBuffer* Renderer::Impl::CreateBufferWithData(SDL_GPUBufferUsageFlags usage, const void* data, Uint32 size) {
    SDL_GPUBufferCreateInfo buffer_info{};
    buffer_info.usage = usage;
    buffer_info.size = size;
    SDL_GPUBuffer* buffer = SDL_CreateGPUBuffer(device, &buffer_info);
    if (buffer == nullptr) {
        FatalSdlError("cannot create buffer");
    }
    SDL_GPUTransferBuffer* transfer = CreateFilledTransferBuffer(data, size);

    SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(device);
    if (commands == nullptr) {
        FatalSdlError("cannot acquire command buffer");
    }
    SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(commands);
    SDL_GPUTransferBufferLocation source{};
    source.transfer_buffer = transfer;
    SDL_GPUBufferRegion destination{};
    destination.buffer = buffer;
    destination.size = size;
    SDL_UploadToGPUBuffer(copy_pass, &source, &destination, false);
    SDL_EndGPUCopyPass(copy_pass);
    if (!SDL_SubmitGPUCommandBuffer(commands)) {
        FatalSdlError("cannot submit upload");
    }
    SDL_ReleaseGPUTransferBuffer(device, transfer);
    return buffer;
}

SDL_GPUTexture* Renderer::Impl::GetTexture(TextureHandle handle, TextureHandle fallback) const {
    const TextureHandle chosen = handle.id != 0 ? handle : fallback;
    return textures[chosen.id - 1];
}

void Renderer::Impl::UploadJointMatrices(SDL_GPUCommandBuffer* commands) {
    UploadMatrices(commands, joint_palette, joint_buffer, joint_buffer_capacity, joint_transfer, joint_transfer_capacity);
}

void Renderer::Impl::UploadMatrices(SDL_GPUCommandBuffer* commands, const std::vector<glm::mat4>& matrices, SDL_GPUBuffer*& buffer,
                    Uint32& capacity, SDL_GPUTransferBuffer*& transfer, Uint32& transfer_capacity) {
    if (matrices.empty()) {
        return;
    }
    const Uint32 count = static_cast<Uint32>(matrices.size());
    if (count > capacity) {
        if (buffer != nullptr) {
            SDL_ReleaseGPUBuffer(device, buffer);
        }
        capacity = count < 1024 ? 1024 : count * 2;
        SDL_GPUBufferCreateInfo buffer_info{};
        buffer_info.usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
        buffer_info.size = capacity * static_cast<Uint32>(sizeof(glm::mat4));
        buffer = SDL_CreateGPUBuffer(device, &buffer_info);
        if (buffer == nullptr) {
            FatalSdlError("cannot create matrix buffer");
        }
    }
    const Uint32 size = count * static_cast<Uint32>(sizeof(glm::mat4));
    if (count > transfer_capacity) {
        if (transfer != nullptr) {
            SDL_ReleaseGPUTransferBuffer(device, transfer);
        }
        transfer_capacity = capacity;
        SDL_GPUTransferBufferCreateInfo transfer_info{};
        transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        transfer_info.size = transfer_capacity * static_cast<Uint32>(sizeof(glm::mat4));
        transfer = SDL_CreateGPUTransferBuffer(device, &transfer_info);
        if (transfer == nullptr) {
            FatalSdlError("cannot create upload transfer buffer");
        }
    }
    // cycle = true: the previous frame's upload may still be in flight.
    void* mapped = SDL_MapGPUTransferBuffer(device, transfer, true);
    if (mapped == nullptr) {
        FatalSdlError("cannot map upload transfer buffer");
    }
    std::memcpy(mapped, matrices.data(), size);
    SDL_UnmapGPUTransferBuffer(device, transfer);
    SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(commands);
    SDL_GPUTransferBufferLocation source{};
    source.transfer_buffer = transfer;
    SDL_GPUBufferRegion destination{};
    destination.buffer = buffer;
    destination.size = size;
    // cycle = true: the previous frame may still be reading the old contents.
    SDL_UploadToGPUBuffer(copy_pass, &source, &destination, true);
    SDL_EndGPUCopyPass(copy_pass);
}

} // namespace poligon

// Buffers refilled every frame (see dynamic_buffer.h).

#include "../renderer/renderer_internal.h"

namespace poligon {
namespace render_internal {

void UploadDynamicBuffer(SDL_GPUDevice* device, SDL_GPUCommandBuffer* commands, const void* data, Uint32 size,
                         DynamicBuffer& target) {
    if (size == 0) {
        return;
    }
    if (size > target.capacity) {
        if (target.buffer != nullptr) {
            ReleaseTrackedBuffer(device, target.buffer);
        }
        target.capacity = size < 65536 ? 65536 : size * 2;
        SDL_GPUBufferCreateInfo buffer_info{};
        buffer_info.usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
        buffer_info.size = target.capacity;
        target.buffer = CreateTrackedBuffer(device, &buffer_info);
        if (target.buffer == nullptr) {
            FatalSdlError("cannot create storage buffer");
        }
    }
    if (size > target.transfer_capacity) {
        if (target.transfer != nullptr) {
            SDL_ReleaseGPUTransferBuffer(device, target.transfer);
        }
        target.transfer_capacity = target.capacity;
        SDL_GPUTransferBufferCreateInfo transfer_info{};
        transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        transfer_info.size = target.transfer_capacity;
        target.transfer = SDL_CreateGPUTransferBuffer(device, &transfer_info);
        if (target.transfer == nullptr) {
            FatalSdlError("cannot create upload transfer buffer");
        }
    }
    // cycle = true: the previous frame's upload may still be in flight.
    void* mapped = SDL_MapGPUTransferBuffer(device, target.transfer, true);
    if (mapped == nullptr) {
        FatalSdlError("cannot map upload transfer buffer");
    }
    std::memcpy(mapped, data, size);
    SDL_UnmapGPUTransferBuffer(device, target.transfer);
    SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(commands);
    SDL_GPUTransferBufferLocation source{};
    source.transfer_buffer = target.transfer;
    SDL_GPUBufferRegion destination{};
    destination.buffer = target.buffer;
    destination.size = size;
    // cycle = true: the previous frame may still be reading the old contents.
    SDL_UploadToGPUBuffer(copy_pass, &source, &destination, true);
    SDL_EndGPUCopyPass(copy_pass);
}

void ReleaseDynamicBuffer(SDL_GPUDevice* device, DynamicBuffer& target) {
    if (target.buffer != nullptr) {
        ReleaseTrackedBuffer(device, target.buffer);
    }
    if (target.transfer != nullptr) {
        SDL_ReleaseGPUTransferBuffer(device, target.transfer);
    }
    target = DynamicBuffer{};
}

} // namespace render_internal
} // namespace poligon

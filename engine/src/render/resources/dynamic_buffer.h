#pragma once

// A storage buffer that vertex shaders read, refilled every frame (overlay quads, decal boxes,
// particle cards, instance matrices): it grows as needed and is filled through an upload
// buffer kept between frames.

#include <SDL3/SDL.h>

namespace poligon {
namespace render_internal {

struct DynamicBuffer {
    SDL_GPUBuffer* buffer = nullptr;
    SDL_GPUTransferBuffer* transfer = nullptr;
    Uint32 capacity = 0;           // bytes the buffer holds
    Uint32 transfer_capacity = 0;  // bytes the upload buffer holds
};

// Copies `size` bytes into the buffer in a copy pass of `commands` (outside any render pass).
// Nothing happens for size 0.
void UploadDynamicBuffer(SDL_GPUDevice* device, SDL_GPUCommandBuffer* commands, const void* data, Uint32 size,
                         DynamicBuffer& target);
void ReleaseDynamicBuffer(SDL_GPUDevice* device, DynamicBuffer& target);

} // namespace render_internal
} // namespace poligon

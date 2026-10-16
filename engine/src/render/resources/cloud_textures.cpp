// The noise volumes clouds are shaped from (made on the card from cloud_noise.cpp).

#include "../renderer/renderer_internal.h"

#include "cloud_noise.h"

namespace poligon {

SDL_GPUTexture* Renderer::Impl::CreateVolume(int size, const std::vector<std::uint8_t>& bytes) {
    SDL_GPUTextureCreateInfo info{};
    info.type = SDL_GPU_TEXTURETYPE_3D;
    info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    info.width = static_cast<Uint32>(size);
    info.height = static_cast<Uint32>(size);
    info.layer_count_or_depth = static_cast<Uint32>(size);
    info.num_levels = 1;
    SDL_GPUTexture* texture = SDL_CreateGPUTexture(device, &info);
    if (texture == nullptr) {
        FatalSdlError("cannot create cloud noise volume");
    }
    SDL_GPUTransferBuffer* transfer = CreateFilledTransferBuffer(bytes.data(), static_cast<Uint32>(bytes.size()));
    SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(device);
    SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(commands);
    SDL_GPUTextureTransferInfo source{};
    source.transfer_buffer = transfer;
    source.pixels_per_row = static_cast<Uint32>(size);
    source.rows_per_layer = static_cast<Uint32>(size);
    SDL_GPUTextureRegion destination{};
    destination.texture = texture;
    destination.w = static_cast<Uint32>(size);
    destination.h = static_cast<Uint32>(size);
    destination.d = static_cast<Uint32>(size);
    SDL_UploadToGPUTexture(copy_pass, &source, &destination, false);
    SDL_EndGPUCopyPass(copy_pass);
    if (!SDL_SubmitGPUCommandBuffer(commands)) {
        FatalSdlError("cannot submit cloud noise upload");
    }
    SDL_ReleaseGPUTransferBuffer(device, transfer);
    return texture;
}

void Renderer::Impl::CreateCloudTextures() {
    if (cloud_shape != nullptr) {
        return;
    }
    cloud_shape = CreateVolume(64, MakeCloudShapeNoise(64, 1u));
    cloud_detail = CreateVolume(32, MakeCloudDetailNoise(32, 2u));
    const std::vector<std::uint8_t> weather = MakeCloudWeatherMap(256, 3u);
    SDL_GPUTextureCreateInfo info{};
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    info.width = 256;
    info.height = 256;
    info.layer_count_or_depth = 1;
    info.num_levels = 1;
    cloud_weather = SDL_CreateGPUTexture(device, &info);
    if (cloud_weather == nullptr) {
        FatalSdlError("cannot create cloud weather map");
    }
    SDL_GPUTransferBuffer* transfer = CreateFilledTransferBuffer(weather.data(), static_cast<Uint32>(weather.size()));
    SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(device);
    SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(commands);
    SDL_GPUTextureTransferInfo source{};
    source.transfer_buffer = transfer;
    source.pixels_per_row = 256;
    source.rows_per_layer = 256;
    SDL_GPUTextureRegion destination{};
    destination.texture = cloud_weather;
    destination.w = 256;
    destination.h = 256;
    destination.d = 1;
    SDL_UploadToGPUTexture(copy_pass, &source, &destination, false);
    SDL_EndGPUCopyPass(copy_pass);
    if (!SDL_SubmitGPUCommandBuffer(commands)) {
        FatalSdlError("cannot submit cloud weather upload");
    }
    SDL_ReleaseGPUTransferBuffer(device, transfer);
}

} // namespace poligon

// Textures and texture arrays on the graphics card, from images and from block-compressed images.

#include "../renderer/renderer_internal.h"

namespace poligon {

TextureHandle Renderer::CreateTexture(const Image& image, const TextureOptions& options) {
    if (image.width < 1 || image.height < 1 ||
        image.pixels.size() != static_cast<size_t>(image.width) * static_cast<size_t>(image.height) * 4) {
        FatalError("Renderer::CreateTexture: the image is empty or its pixel data has the wrong size");
    }
    if (impl_->in_frame) {
        FatalError("Renderer::CreateTexture: do not create textures between BeginFrame and EndFrame");
    }

    const Uint32 level_count = options.generate_mipmaps ? CountMipLevels(image.width, image.height) : 1;

    SDL_GPUTextureCreateInfo info{};
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = options.is_color ? SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB : SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    info.width = static_cast<Uint32>(image.width);
    info.height = static_cast<Uint32>(image.height);
    info.layer_count_or_depth = 1;
    info.num_levels = level_count;
    info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    SDL_GPUTexture* texture = CreateTrackedTexture(impl_->device, &info);
    if (texture == nullptr) {
        FatalSdlError("cannot create texture");
    }

    // Mipmap levels are computed here, on the CPU, and uploaded one by one, so their pixels are
    // the same on every backend and driver by construction (see MakeHalfSizeImage).
    SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(impl_->device);
    if (commands == nullptr) {
        FatalSdlError("cannot acquire command buffer");
    }
    SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(commands);
    std::vector<SDL_GPUTransferBuffer*> transfers;
    // The chain of halvings is built from unscaled images; a level whose alpha is scaled for
    // coverage is a copy, so that the scaling does not compound into the next level.
    Image smaller_level;
    Image scaled_level;
    const Image* level_image = &image;
    const float coverage = options.coverage_cutoff > 0.0f ? MeasureAlphaCoverage(image, options.coverage_cutoff) : 0.0f;
    for (Uint32 level = 0; level < level_count; ++level) {
        if (level > 0) {
            smaller_level = MakeHalfSizeImage(level_image == &scaled_level ? smaller_level : *level_image, options.is_color);
            level_image = &smaller_level;
            if (options.coverage_cutoff > 0.0f) {
                scaled_level = smaller_level;
                ScaleAlphaToCoverage(scaled_level, options.coverage_cutoff, coverage);
                level_image = &scaled_level;
            }
        }
        SDL_GPUTransferBuffer* transfer = impl_->CreateFilledTransferBuffer(
            level_image->pixels.data(), static_cast<Uint32>(level_image->pixels.size()));
        transfers.push_back(transfer);

        SDL_GPUTextureTransferInfo source{};
        source.transfer_buffer = transfer;
        source.pixels_per_row = static_cast<Uint32>(level_image->width);
        source.rows_per_layer = static_cast<Uint32>(level_image->height);
        SDL_GPUTextureRegion destination{};
        destination.texture = texture;
        destination.mip_level = level;
        destination.w = static_cast<Uint32>(level_image->width);
        destination.h = static_cast<Uint32>(level_image->height);
        destination.d = 1;
        SDL_UploadToGPUTexture(copy_pass, &source, &destination, false);
    }
    SDL_EndGPUCopyPass(copy_pass);
    if (!SDL_SubmitGPUCommandBuffer(commands)) {
        FatalSdlError("cannot submit texture upload");
    }
    for (SDL_GPUTransferBuffer* transfer : transfers) {
        SDL_ReleaseGPUTransferBuffer(impl_->device, transfer);
    }

    impl_->textures.push_back(texture);
    impl_->texture_layers.push_back(0);
    impl_->texture_sizes.emplace_back(static_cast<float>(image.width), static_cast<float>(image.height));
    TextureHandle handle;
    handle.id = static_cast<std::uint32_t>(impl_->textures.size());
    return handle;
}

namespace {

SDL_GPUTextureFormat ToGpuFormat(const CompressedImage& image) {
    switch (image.format) {
    case TextureFormat::Bc1:
        return image.is_color ? SDL_GPU_TEXTUREFORMAT_BC1_RGBA_UNORM_SRGB : SDL_GPU_TEXTUREFORMAT_BC1_RGBA_UNORM;
    case TextureFormat::Bc3:
        return image.is_color ? SDL_GPU_TEXTUREFORMAT_BC3_RGBA_UNORM_SRGB : SDL_GPU_TEXTUREFORMAT_BC3_RGBA_UNORM;
    case TextureFormat::Bc4:
        return SDL_GPU_TEXTUREFORMAT_BC4_R_UNORM;
    case TextureFormat::Bc5:
        return SDL_GPU_TEXTUREFORMAT_BC5_RG_UNORM;
    case TextureFormat::Rgba8:
        return image.is_color ? SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB : SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    }
    return SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
}

void CheckCompressedImage(const CompressedImage& image, const char* what) {
    bool valid = image.width > 0 && image.height > 0 && !image.levels.empty() && image.levels.size() <= 16;
    if (valid && image.format != TextureFormat::Rgba8) {
        valid = image.width % 4 == 0 && image.height % 4 == 0;
    }
    int width = image.width;
    int height = image.height;
    for (size_t level = 0; valid && level < image.levels.size(); ++level) {
        valid = image.levels[level].size() == GetCompressedLevelSize(image.format, width, height);
        width = glm::max(width / 2, 1);
        height = glm::max(height / 2, 1);
    }
    if (!valid) {
        const std::string message = std::string(what) + ": the compressed image is empty, not a multiple of 4 wide and high, or its levels have the wrong size";
        FatalError(message.c_str());
    }
}

} // namespace

TextureHandle Renderer::CreateTexture(const CompressedImage& image) {
    return CreateTextureArray(&image, 1, true);
}

TextureHandle Renderer::CreateTextureArray(const CompressedImage* images, int count) {
    return CreateTextureArray(images, count, false);
}

TextureHandle Renderer::CreateTextureArray(const CompressedImage* images, int count, bool plain) {
    if (images == nullptr || count < 1 || count > 256) {
        FatalError("Renderer::CreateTextureArray: needs between 1 and 256 images");
    }
    for (int i = 0; i < count; ++i) {
        CheckCompressedImage(images[i], "Renderer::CreateTexture");
        if (images[i].width != images[0].width || images[i].height != images[0].height || images[i].format != images[0].format ||
            images[i].is_color != images[0].is_color || images[i].levels.size() != images[0].levels.size()) {
            FatalError("Renderer::CreateTextureArray: the images must share size, format and number of levels");
        }
    }
    if (impl_->in_frame) {
        FatalError("Renderer::CreateTexture: do not create textures between BeginFrame and EndFrame");
    }
    SDL_GPUTextureCreateInfo info{};
    info.type = plain ? SDL_GPU_TEXTURETYPE_2D : SDL_GPU_TEXTURETYPE_2D_ARRAY;
    info.format = ToGpuFormat(images[0]);
    info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    info.width = static_cast<Uint32>(images[0].width);
    info.height = static_cast<Uint32>(images[0].height);
    info.layer_count_or_depth = static_cast<Uint32>(count);
    info.num_levels = static_cast<Uint32>(images[0].levels.size());
    info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    SDL_GPUTexture* texture = CreateTrackedTexture(impl_->device, &info);
    if (texture == nullptr) {
        FatalSdlError("cannot create compressed texture");
    }
    for (int layer = 0; layer < count; ++layer) {
        SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(impl_->device);
        if (commands == nullptr) {
            FatalSdlError("cannot acquire command buffer");
        }
        SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(commands);
        std::vector<SDL_GPUTransferBuffer*> transfers;
        int width = images[layer].width;
        int height = images[layer].height;
        for (size_t level = 0; level < images[layer].levels.size(); ++level) {
            const std::vector<std::uint8_t>& bytes = images[layer].levels[level];
            SDL_GPUTransferBuffer* transfer = impl_->CreateFilledTransferBuffer(bytes.data(), static_cast<Uint32>(bytes.size()));
            transfers.push_back(transfer);
            SDL_GPUTextureTransferInfo source{};
            source.transfer_buffer = transfer;
            SDL_GPUTextureRegion destination{};
            destination.texture = texture;
            destination.mip_level = static_cast<Uint32>(level);
            destination.layer = static_cast<Uint32>(layer);
            destination.w = static_cast<Uint32>(width);
            destination.h = static_cast<Uint32>(height);
            destination.d = 1;
            SDL_UploadToGPUTexture(copy_pass, &source, &destination, false);
            width = glm::max(width / 2, 1);
            height = glm::max(height / 2, 1);
        }
        SDL_EndGPUCopyPass(copy_pass);
        if (!SDL_SubmitGPUCommandBuffer(commands)) {
            FatalSdlError("cannot submit compressed texture upload");
        }
        for (SDL_GPUTransferBuffer* transfer : transfers) {
            SDL_ReleaseGPUTransferBuffer(impl_->device, transfer);
        }
    }
    impl_->textures.push_back(texture);
    impl_->texture_layers.push_back(plain ? 0 : count);
    impl_->texture_sizes.emplace_back(static_cast<float>(images[0].width), static_cast<float>(images[0].height));
    TextureHandle handle;
    handle.id = static_cast<std::uint32_t>(impl_->textures.size());
    return handle;
}

TextureHandle Renderer::CreateTextureArray(const Image* images, int count, const TextureOptions& options) {
    if (images == nullptr || count < 1 || count > 256) {
        FatalError("Renderer::CreateTextureArray: needs between 1 and 256 images");
    }
    for (int i = 0; i < count; ++i) {
        if (images[i].width != images[0].width || images[i].height != images[0].height || images[i].width < 1 ||
            images[i].pixels.size() != static_cast<size_t>(images[i].width) * static_cast<size_t>(images[i].height) * 4) {
            FatalError("Renderer::CreateTextureArray: the images must be non-empty and all of one size");
        }
    }
    if (impl_->in_frame) {
        FatalError("Renderer::CreateTextureArray: do not create textures between BeginFrame and EndFrame");
    }
    const Uint32 level_count = options.generate_mipmaps ? CountMipLevels(images[0].width, images[0].height) : 1;
    SDL_GPUTextureCreateInfo info{};
    info.type = SDL_GPU_TEXTURETYPE_2D_ARRAY;
    info.format = options.is_color ? SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB : SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    info.width = static_cast<Uint32>(images[0].width);
    info.height = static_cast<Uint32>(images[0].height);
    info.layer_count_or_depth = static_cast<Uint32>(count);
    info.num_levels = level_count;
    info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    SDL_GPUTexture* texture = CreateTrackedTexture(impl_->device, &info);
    if (texture == nullptr) {
        FatalSdlError("cannot create texture array");
    }
    // One image at a time, with its mipmaps computed on the CPU as for CreateTexture.
    for (int layer = 0; layer < count; ++layer) {
        SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(impl_->device);
        if (commands == nullptr) {
            FatalSdlError("cannot acquire command buffer");
        }
        SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(commands);
        std::vector<SDL_GPUTransferBuffer*> transfers;
        Image smaller_level;
        const Image* level_image = &images[layer];
        for (Uint32 level = 0; level < level_count; ++level) {
            if (level > 0) {
                smaller_level = MakeHalfSizeImage(*level_image, options.is_color);
                level_image = &smaller_level;
            }
            SDL_GPUTransferBuffer* transfer =
                impl_->CreateFilledTransferBuffer(level_image->pixels.data(), static_cast<Uint32>(level_image->pixels.size()));
            transfers.push_back(transfer);
            SDL_GPUTextureTransferInfo source{};
            source.transfer_buffer = transfer;
            source.pixels_per_row = static_cast<Uint32>(level_image->width);
            source.rows_per_layer = static_cast<Uint32>(level_image->height);
            SDL_GPUTextureRegion destination{};
            destination.texture = texture;
            destination.mip_level = level;
            destination.layer = static_cast<Uint32>(layer);
            destination.w = static_cast<Uint32>(level_image->width);
            destination.h = static_cast<Uint32>(level_image->height);
            destination.d = 1;
            SDL_UploadToGPUTexture(copy_pass, &source, &destination, false);
        }
        SDL_EndGPUCopyPass(copy_pass);
        if (!SDL_SubmitGPUCommandBuffer(commands)) {
            FatalSdlError("cannot submit texture array upload");
        }
        for (SDL_GPUTransferBuffer* transfer : transfers) {
            SDL_ReleaseGPUTransferBuffer(impl_->device, transfer);
        }
    }
    impl_->textures.push_back(texture);
    impl_->texture_layers.push_back(count);
    impl_->texture_sizes.emplace_back(static_cast<float>(images[0].width), static_cast<float>(images[0].height));
    TextureHandle handle;
    handle.id = static_cast<std::uint32_t>(impl_->textures.size());
    return handle;
}

void Renderer::DestroyTexture(TextureHandle texture) {
    if (impl_->in_frame) {
        FatalError("Renderer::DestroyTexture: do not destroy textures between BeginFrame and EndFrame");
    }
    if (texture.id == 0 || texture.id > impl_->textures.size() || impl_->textures[texture.id - 1] == nullptr) {
        FatalError("Renderer::DestroyTexture: invalid or already destroyed texture handle");
    }
    ReleaseTrackedTexture(impl_->device, impl_->textures[texture.id - 1]);
    impl_->textures[texture.id - 1] = nullptr;
}

} // namespace poligon

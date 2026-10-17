// The images a frame is drawn into (render targets) and the samplers.

#include "renderer_internal.h"

namespace poligon {

SDL_GPUTexture* Renderer::Impl::CreateTarget(SDL_GPUTextureFormat format, SDL_GPUTextureUsageFlags usage, int width, int height,
                             SDL_GPUSampleCount samples, const SDL_FColor* clear_color, const char* what,
                             float clear_depth) {
    SDL_GPUTextureCreateInfo info{};
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = format;
    info.usage = usage;
    info.width = static_cast<Uint32>(width);
    info.height = static_cast<Uint32>(height);
    info.layer_count_or_depth = 1;
    info.num_levels = 1;
    info.sample_count = samples;
    // Direct3D 12 clears fastest (and without debug layer warnings) when the clear values
    // are declared at creation. Other backends ignore these properties.
    const SDL_PropertiesID properties = SDL_CreateProperties();
    if (clear_color != nullptr) {
        SDL_SetFloatProperty(properties, SDL_PROP_GPU_TEXTURE_CREATE_D3D12_CLEAR_R_FLOAT, clear_color->r);
        SDL_SetFloatProperty(properties, SDL_PROP_GPU_TEXTURE_CREATE_D3D12_CLEAR_G_FLOAT, clear_color->g);
        SDL_SetFloatProperty(properties, SDL_PROP_GPU_TEXTURE_CREATE_D3D12_CLEAR_B_FLOAT, clear_color->b);
        SDL_SetFloatProperty(properties, SDL_PROP_GPU_TEXTURE_CREATE_D3D12_CLEAR_A_FLOAT, clear_color->a);
    }
    SDL_SetFloatProperty(properties, SDL_PROP_GPU_TEXTURE_CREATE_D3D12_CLEAR_DEPTH_FLOAT, clear_depth);
    info.props = properties;
    SDL_GPUTexture* texture = SDL_CreateGPUTexture(device, &info);
    SDL_DestroyProperties(properties);
    if (texture == nullptr) {
        const std::string message = std::string("cannot create ") + what;
        FatalSdlError(message.c_str());
    }
    return texture;
}

void Renderer::Impl::CreateTargets() {
    CreateSizedTargets();
    CreateFixedTargets();
}

void Renderer::Impl::CreateSizedTargets() {
    // Use the asked multisample count, or the nearest lower one the card supports.
    msaa_samples = config.msaa_samples >= 8 ? 8 : config.msaa_samples >= 4 ? 4 : config.msaa_samples >= 2 ? 2 : 1;
    while (msaa_samples > 1 &&
           (!SDL_GPUTextureSupportsSampleCount(device, kSceneFormat, ToSampleCount(msaa_samples)) ||
            !SDL_GPUTextureSupportsSampleCount(device, kDepthFormat, ToSampleCount(msaa_samples)))) {
        msaa_samples /= 2;
    }
    const SDL_GPUSampleCount samples = ToSampleCount(msaa_samples);
    const float scale = msaa_samples == 1 ? glm::clamp(config.render_scale, 0.25f, 1.0f) : 1.0f;
    render_width = glm::max(16, static_cast<int>(std::lround(static_cast<float>(config.width) * scale)));
    render_height = glm::max(16, static_cast<int>(std::lround(static_cast<float>(config.height) * scale)));
    // Everything drawn from the scene is at the drawing size; from temporal
    // anti-aliasing on (bloom, tone mapping, the output) at the output size.
    const int width = render_width;
    const int height = render_height;

    if (msaa_samples > 1) {
        scene_color = CreateTarget(kSceneFormat, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET, width, height, samples,
                                   &linear_clear_color, "multisampled scene target");
        scene_resolved = CreateTarget(kSceneFormat, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER,
                                      width, height, SDL_GPU_SAMPLECOUNT_1, &linear_clear_color, "resolved scene target");
    } else {
        scene_color = CreateTarget(kSceneFormat, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER, width,
                                   height, SDL_GPU_SAMPLECOUNT_1, &linear_clear_color, "scene target");
    }
    // The camera's depth runs from 1 at the near plane to 0 far away ("reversed depth"):
    // a floating-point depth image then keeps millimeters of precision kilometers away,
    // where the usual direction has meters and distant land and trees fight over pixels.
    scene_depth = CreateTarget(kDepthFormat, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET, width, height, samples, nullptr,
                               "scene depth target", 0.0f);
    // SAMPLER is needed because the image is copied (blitted) to the window.
    output_target = CreateTarget(kOutputFormat, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER,
                                 config.width, config.height, SDL_GPU_SAMPLECOUNT_1, nullptr, "output target");

    const SDL_GPUTextureUsageFlags drawn_and_read = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    prepass_depth = CreateTarget(kDepthFormat, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER,
                                 width, height, SDL_GPU_SAMPLECOUNT_1, nullptr, "depth image", 0.0f);
    ao_target = CreateTarget(kAoFormat, drawn_and_read, width, height, SDL_GPU_SAMPLECOUNT_1, nullptr,
                             "ambient occlusion image");
    ao_scratch = CreateTarget(kAoFormat, drawn_and_read, width, height, SDL_GPU_SAMPLECOUNT_1, nullptr,
                              "ambient occlusion image");
    fog_width = (width + 1) / 2;
    fog_height = (height + 1) / 2;
    fog_target = CreateTarget(kSceneFormat, drawn_and_read, fog_width, fog_height, SDL_GPU_SAMPLECOUNT_1, nullptr,
                              "fog image");
    for (SDL_GPUTexture*& target : taa_targets) {
        target = CreateTarget(kSceneFormat, drawn_and_read, config.width, config.height, SDL_GPU_SAMPLECOUNT_1, nullptr,
                              "temporal anti-aliasing image");
    }
    water_scene_copy = CreateTarget(kSceneFormat, drawn_and_read, width, height, SDL_GPU_SAMPLECOUNT_1, nullptr,
                                    "copy of the scene for water");
    const float cloud_scale = glm::clamp(config.cloud_resolution, 0.125f, 1.0f);
    const int cloud_width = glm::max(8, static_cast<int>(static_cast<float>(width) * cloud_scale));
    const int cloud_height = glm::max(8, static_cast<int>(static_cast<float>(height) * cloud_scale));
    cloud_target = CreateTarget(kSceneFormat, drawn_and_read, cloud_width, cloud_height, SDL_GPU_SAMPLECOUNT_1, nullptr, "cloud image");
    for (SDL_GPUTexture*& history : cloud_history) {
        history = CreateTarget(kSceneFormat, drawn_and_read, cloud_width, cloud_height, SDL_GPU_SAMPLECOUNT_1, nullptr,
                               "cloud image built over frames");
    }
    cloud_history_valid = false;
    water_depth_copy = CreateTarget(SDL_GPU_TEXTUREFORMAT_R32_FLOAT, drawn_and_read, width, height, SDL_GPU_SAMPLECOUNT_1,
                                    nullptr, "copy of the depth for water");

    // Bloom chain: each image half the size of the one before, down to a few pixels.
    int level_width = config.width;
    int level_height = config.height;
    do {
        level_width = level_width > 1 ? level_width / 2 : 1;
        level_height = level_height > 1 ? level_height / 2 : 1;
        BloomLevel level;
        level.width = level_width;
        level.height = level_height;
        level.texture = CreateTarget(kSceneFormat, drawn_and_read, level_width, level_height, SDL_GPU_SAMPLECOUNT_1,
                                     nullptr, "bloom image");
        bloom_levels.push_back(level);
    } while (static_cast<int>(bloom_levels.size()) < kMaxBloomLevels && level_width >= 16 && level_height >= 16);

}

void Renderer::Impl::CreateFixedTargets() {
    const SDL_GPUTextureUsageFlags drawn_and_read = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    // The four shadow cascades share one depth image, as 2 x 2 tiles.
    shadow_atlas = CreateTarget(kDepthFormat, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER,
                                config.shadow_map_size * 2, config.shadow_map_size * 2, SDL_GPU_SAMPLECOUNT_1,
                                nullptr, "shadow image");
    cloud_shadow_target = CreateTarget(SDL_GPU_TEXTUREFORMAT_R8_UNORM, drawn_and_read, 256, 256, SDL_GPU_SAMPLECOUNT_1, nullptr,
                                       "cloud shadow image");
    for (SDL_GPUTexture*& target : luminance_targets) {
        target = CreateTarget(kLuminanceFormat, drawn_and_read, 1, 1, SDL_GPU_SAMPLECOUNT_1, nullptr,
                              "luminance image");
    }
}

void Renderer::Impl::ReleaseSizedTargets() {
    SDL_GPUTexture** sized[] = {&scene_color,  &scene_resolved, &scene_depth,      &output_target,  &prepass_depth,
                                &ao_target,    &ao_scratch,     &fog_target,       &taa_targets[0], &taa_targets[1],
                                &water_scene_copy, &cloud_target, &water_depth_copy, &cloud_history[0], &cloud_history[1]};
    for (SDL_GPUTexture** texture : sized) {
        if (*texture != nullptr) {
            SDL_ReleaseGPUTexture(device, *texture);
            *texture = nullptr;
        }
    }
    for (const BloomLevel& level : bloom_levels) {
        SDL_ReleaseGPUTexture(device, level.texture);
    }
    bloom_levels.clear();
}

void Renderer::Impl::CreateSamplers() {
    SDL_GPUSamplerCreateInfo info{};
    info.min_filter = SDL_GPU_FILTER_LINEAR;
    info.mag_filter = SDL_GPU_FILTER_LINEAR;
    info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
    info.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    info.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    // Anisotropic filtering keeps textures sharp on surfaces seen at a shallow angle, such as the ground.
    info.enable_anisotropy = true;
    info.max_anisotropy = 8.0f;
    info.max_lod = 1000.0f;
    texture_sampler = SDL_CreateGPUSampler(device, &info);

    // Compares a depth with the stored one and blends the yes/no answers of neighboring
    // texels, which softens shadow edges.
    SDL_GPUSamplerCreateInfo shadow_info{};
    shadow_info.min_filter = SDL_GPU_FILTER_LINEAR;
    shadow_info.mag_filter = SDL_GPU_FILTER_LINEAR;
    shadow_info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    shadow_info.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    shadow_info.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    shadow_info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    shadow_info.enable_compare = true;
    shadow_info.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
    shadow_sampler = SDL_CreateGPUSampler(device, &shadow_info);

    SDL_GPUSamplerCreateInfo clamp_info{};
    clamp_info.min_filter = SDL_GPU_FILTER_NEAREST;
    clamp_info.mag_filter = SDL_GPU_FILTER_NEAREST;
    clamp_info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    clamp_info.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    clamp_info.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    clamp_info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    clamp_sampler = SDL_CreateGPUSampler(device, &clamp_info);

    SDL_GPUSamplerCreateInfo linear_clamp_info = clamp_info;
    linear_clamp_info.min_filter = SDL_GPU_FILTER_LINEAR;
    linear_clamp_info.mag_filter = SDL_GPU_FILTER_LINEAR;
    linear_clamp_sampler = SDL_CreateGPUSampler(device, &linear_clamp_info);

    SDL_GPUSamplerCreateInfo repeat_info{};
    repeat_info.min_filter = SDL_GPU_FILTER_LINEAR;
    repeat_info.mag_filter = SDL_GPU_FILTER_LINEAR;
    repeat_info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    repeat_info.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    repeat_info.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    repeat_info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    repeat_sampler = SDL_CreateGPUSampler(device, &repeat_info);

    if (texture_sampler == nullptr || shadow_sampler == nullptr || clamp_sampler == nullptr ||
        linear_clamp_sampler == nullptr || repeat_sampler == nullptr) {
        FatalSdlError("cannot create sampler");
    }
}

} // namespace poligon

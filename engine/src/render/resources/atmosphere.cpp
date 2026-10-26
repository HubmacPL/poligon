// The sky from the atmosphere model (Environment::sky_model = SkyModel::Atmosphere): the
// frame's sun and sky colors from the model (ApplyAtmosphere, at BeginFrame), and its tables
// on the graphics card, made on the first frame that needs them. The sky view table is drawn
// by passes/atmosphere_pass.cpp.

#include "../renderer/renderer_internal.h"
#include "../sky/atmosphere_tables.h"
#include "poligon/render/sky.h"
#include "poligon_shaders/atmosphere_sky_view_frag_dxil.h"
#include "poligon_shaders/atmosphere_sky_view_frag_spirv.h"
#include "poligon_shaders/fullscreen_vert_dxil.h"
#include "poligon_shaders/fullscreen_vert_spirv.h"

namespace poligon {

namespace {

// A table as half floats, four per texel (alpha 1).
std::vector<std::uint16_t> ToHalfFloats(const std::vector<glm::vec3>& values) {
    std::vector<std::uint16_t> halves;
    halves.reserve(values.size() * 4);
    for (const glm::vec3& value : values) {
        halves.push_back(glm::packHalf1x16(value.x));
        halves.push_back(glm::packHalf1x16(value.y));
        halves.push_back(glm::packHalf1x16(value.z));
        halves.push_back(glm::packHalf1x16(1.0f));
    }
    return halves;
}

} // namespace

void Renderer::Impl::ApplyAtmosphere() {
    atmosphere.on = environment.sky_model == SkyModel::Atmosphere;
    if (!atmosphere.on) {
        return;
    }
    if (environment.time_of_day >= 0.0f) {
        light.direction = ComputeSunDirection(environment.time_of_day, environment.day_of_year, environment.latitude);
    }
    // The model's light changes only with the sun; recomputed when the sun moves.
    if (!atmosphere.sky_valid || light.direction != atmosphere.sky_direction) {
        atmosphere.sky = ComputeSkyLight(light.direction);
        atmosphere.sky_direction = light.direction;
        atmosphere.sky_valid = true;
    }
    const SkyLight& sky = atmosphere.sky;
    const glm::vec3 above(light.color.r, light.color.g, light.color.b);
    atmosphere.sun_above_air = above;
    const auto to_color = [](const glm::vec3& value) { return Color{value.x, value.y, value.z, 1.0f}; };
    light.color = to_color(above * sky.sun_at_ground);
    environment.sky_zenith = to_color(above * sky.zenith);
    environment.sky_horizon = to_color(above * sky.horizon);
    environment.sky_ground = to_color(above * sky.ground);

    if (atmosphere.transmittance != nullptr) {
        return;
    }
    using namespace render_internal;
    const AtmosphereTables& tables = GetAtmosphereTables();
    const auto create_table = [this](int width, int height, SDL_GPUTextureUsageFlags usage) {
        SDL_GPUTextureCreateInfo info{};
        info.type = SDL_GPU_TEXTURETYPE_2D;
        info.format = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
        info.usage = usage;
        info.width = static_cast<Uint32>(width);
        info.height = static_cast<Uint32>(height);
        info.layer_count_or_depth = 1;
        info.num_levels = 1;
        SDL_GPUTexture* texture = CreateTrackedTexture(device, &info);
        if (texture == nullptr) {
            FatalSdlError("cannot create an atmosphere table");
        }
        return texture;
    };
    atmosphere.transmittance = create_table(kTransmittanceWidth, kTransmittanceHeight, SDL_GPU_TEXTUREUSAGE_SAMPLER);
    const std::vector<std::uint16_t> transmittance = ToHalfFloats(tables.transmittance);
    UploadTextureLayer(atmosphere.transmittance, 0, transmittance.data(), kTransmittanceWidth, kTransmittanceHeight, 8);
    atmosphere.multiple_scattering = create_table(kMultipleScatteringSize, kMultipleScatteringSize, SDL_GPU_TEXTUREUSAGE_SAMPLER);
    const std::vector<std::uint16_t> multiple = ToHalfFloats(tables.multiple_scattering);
    UploadTextureLayer(atmosphere.multiple_scattering, 0, multiple.data(), kMultipleScatteringSize, kMultipleScatteringSize, 8);
    atmosphere.sky_view =
        create_table(kSkyViewWidth, kSkyViewHeight, SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET);

    SDL_GPUShader* vertex = CreateShader(SDL_GPU_SHADERSTAGE_VERTEX, 0, 0, kFullscreenVertDxil, sizeof(kFullscreenVertDxil),
                                         kFullscreenVertSpirv, sizeof(kFullscreenVertSpirv));
    SDL_GPUShader* fragment = CreateShader(SDL_GPU_SHADERSTAGE_FRAGMENT, 2, 1, kAtmosphereSkyViewFragDxil,
                                           sizeof(kAtmosphereSkyViewFragDxil), kAtmosphereSkyViewFragSpirv,
                                           sizeof(kAtmosphereSkyViewFragSpirv));
    atmosphere.sky_view_pipeline =
        CreateFullscreenPipeline(vertex, fragment, SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT, FullscreenBlend::Replace);
    SDL_ReleaseGPUShader(device, vertex);
    SDL_ReleaseGPUShader(device, fragment);
}

void Renderer::Impl::ReleaseAtmosphere() {
    for (SDL_GPUTexture* texture : {atmosphere.transmittance, atmosphere.multiple_scattering, atmosphere.sky_view}) {
        if (texture != nullptr) {
            ReleaseTrackedTexture(device, texture);
        }
    }
    if (atmosphere.sky_view_pipeline != nullptr) {
        SDL_ReleaseGPUGraphicsPipeline(device, atmosphere.sky_view_pipeline);
    }
}

SDL_GPUTexture* Renderer::Impl::AtmosphereSkyView() const {
    return atmosphere.on ? atmosphere.sky_view : textures[white_texture.id - 1];
}

SDL_GPUTexture* Renderer::Impl::AtmosphereTransmittance() const {
    return atmosphere.on ? atmosphere.transmittance : textures[white_texture.id - 1];
}

} // namespace poligon

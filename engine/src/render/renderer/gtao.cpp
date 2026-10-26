// Ambient occlusion by Intel's XeGTAO (MIT; github.com/GameTechDev/XeGTAO): ground-truth
// ambient occlusion (Jimenez et al. 2016) as Intel wrote it, in place of our own. Its shaders run
// as SDL GPU compute pipelines, in three passes (view-space depth with smaller levels, the
// occlusion term with normals worked out from depth, an edge-aware blur), then a small pass puts
// the result into the ambient occlusion image the lighting reads. Its constants are made by its
// own GTAOUpdateConstants (XeGTAO.h), with the depth unpacking our reversed depth needs.

#include <algorithm>
#include <cassert>
#include <cmath>

#include "renderer_internal.h"

// The library's header, as it is; its C++ half gives GTAOSettings and GTAOUpdateConstants.
#include "../shaders/third_party/xegtao/XeGTAO.h"

#include "poligon_shaders/fullscreen_vert_dxil.h"
#include "poligon_shaders/fullscreen_vert_spirv.h"
#include "poligon_shaders/gtao_apply_frag_dxil.h"
#include "poligon_shaders/gtao_apply_frag_spirv.h"
#include "poligon_shaders/gtao_denoise_comp_dxil.h"
#include "poligon_shaders/gtao_denoise_comp_spirv.h"
#include "poligon_shaders/gtao_main_high_comp_dxil.h"
#include "poligon_shaders/gtao_main_high_comp_spirv.h"
#include "poligon_shaders/gtao_main_low_comp_dxil.h"
#include "poligon_shaders/gtao_main_low_comp_spirv.h"
#include "poligon_shaders/gtao_main_medium_comp_dxil.h"
#include "poligon_shaders/gtao_main_medium_comp_spirv.h"
#include "poligon_shaders/gtao_main_ultra_comp_dxil.h"
#include "poligon_shaders/gtao_main_ultra_comp_spirv.h"
#include "poligon_shaders/gtao_prefilter_comp_dxil.h"
#include "poligon_shaders/gtao_prefilter_comp_spirv.h"

namespace poligon {

SDL_GPUComputePipeline* Renderer::Impl::CreateComputePipeline(const unsigned char* dxil, size_t dxil_size, const unsigned char* spirv,
                                                              size_t spirv_size, Uint32 sampler_count, Uint32 readable_texture_count,
                                                              Uint32 writable_texture_count, Uint32 uniform_buffer_count, Uint32 threads_x,
                                                              Uint32 threads_y) {
    const SDL_GPUShaderFormat supported_formats = SDL_GetGPUShaderFormats(device);
    SDL_GPUComputePipelineCreateInfo info{};
    if (supported_formats & SDL_GPU_SHADERFORMAT_DXIL) {
        info.format = SDL_GPU_SHADERFORMAT_DXIL;
        info.code = dxil;
        info.code_size = dxil_size;
    } else {
        info.format = SDL_GPU_SHADERFORMAT_SPIRV;
        info.code = spirv;
        info.code_size = spirv_size;
    }
    info.entrypoint = "main";
    info.num_samplers = sampler_count;
    info.num_readonly_storage_textures = readable_texture_count;
    info.num_readwrite_storage_textures = writable_texture_count;
    info.num_uniform_buffers = uniform_buffer_count;
    info.threadcount_x = threads_x;
    info.threadcount_y = threads_y;
    info.threadcount_z = 1;
    SDL_GPUComputePipeline* pipeline = SDL_CreateGPUComputePipeline(device, &info);
    if (pipeline == nullptr) {
        FatalSdlError("cannot create a compute pipeline");
    }
    return pipeline;
}

void Renderer::Impl::CreateGtaoPipelines() {
    GtaoPipelines& p = gtao_pipelines;
    p.prefilter = CreateComputePipeline(kGtaoPrefilterCompDxil, sizeof(kGtaoPrefilterCompDxil), kGtaoPrefilterCompSpirv,
                                        sizeof(kGtaoPrefilterCompSpirv), 1, 0, XE_GTAO_DEPTH_MIP_LEVELS, 1, 8, 8);
    p.main[0] = CreateComputePipeline(kGtaoMainLowCompDxil, sizeof(kGtaoMainLowCompDxil), kGtaoMainLowCompSpirv, sizeof(kGtaoMainLowCompSpirv),
                                      1, 0, 2, 1, XE_GTAO_NUMTHREADS_X, XE_GTAO_NUMTHREADS_Y);
    p.main[1] = CreateComputePipeline(kGtaoMainMediumCompDxil, sizeof(kGtaoMainMediumCompDxil), kGtaoMainMediumCompSpirv,
                                      sizeof(kGtaoMainMediumCompSpirv), 1, 0, 2, 1, XE_GTAO_NUMTHREADS_X, XE_GTAO_NUMTHREADS_Y);
    p.main[2] = CreateComputePipeline(kGtaoMainHighCompDxil, sizeof(kGtaoMainHighCompDxil), kGtaoMainHighCompSpirv, sizeof(kGtaoMainHighCompSpirv),
                                      1, 0, 2, 1, XE_GTAO_NUMTHREADS_X, XE_GTAO_NUMTHREADS_Y);
    p.main[3] = CreateComputePipeline(kGtaoMainUltraCompDxil, sizeof(kGtaoMainUltraCompDxil), kGtaoMainUltraCompSpirv,
                                      sizeof(kGtaoMainUltraCompSpirv), 1, 0, 2, 1, XE_GTAO_NUMTHREADS_X, XE_GTAO_NUMTHREADS_Y);
    p.denoise = CreateComputePipeline(kGtaoDenoiseCompDxil, sizeof(kGtaoDenoiseCompDxil), kGtaoDenoiseCompSpirv,
                                      sizeof(kGtaoDenoiseCompSpirv), 1, 1, 1, 1, XE_GTAO_NUMTHREADS_X, XE_GTAO_NUMTHREADS_Y);
    SDL_GPUShader* vertex = CreateShader(SDL_GPU_SHADERSTAGE_VERTEX, 0, 0, kFullscreenVertDxil, sizeof(kFullscreenVertDxil),
                                         kFullscreenVertSpirv, sizeof(kFullscreenVertSpirv));
    // The result is read as a storage image (no sampler): CreateShader has no count for those.
    SDL_GPUShaderCreateInfo info{};
    const bool dxil = (SDL_GetGPUShaderFormats(device) & SDL_GPU_SHADERFORMAT_DXIL) != 0;
    info.format = dxil ? SDL_GPU_SHADERFORMAT_DXIL : SDL_GPU_SHADERFORMAT_SPIRV;
    info.code = dxil ? kGtaoApplyFragDxil : kGtaoApplyFragSpirv;
    info.code_size = dxil ? sizeof(kGtaoApplyFragDxil) : sizeof(kGtaoApplyFragSpirv);
    info.entrypoint = "main";
    info.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
    info.num_storage_textures = 1;
    info.num_uniform_buffers = 1;
    SDL_GPUShader* fragment = SDL_CreateGPUShader(device, &info);
    if (fragment == nullptr) {
        FatalSdlError("cannot create the ambient occlusion apply shader");
    }
    p.apply = CreateFullscreenPipeline(vertex, fragment, kAoFormat, FullscreenBlend::Replace);
    SDL_ReleaseGPUShader(device, vertex);
    SDL_ReleaseGPUShader(device, fragment);
}

void Renderer::Impl::ReleaseGtao() {
    GtaoPipelines& p = gtao_pipelines;
    for (SDL_GPUComputePipeline* pipeline : {p.prefilter, p.main[0], p.main[1], p.main[2], p.main[3], p.denoise}) {
        if (pipeline != nullptr) {
            SDL_ReleaseGPUComputePipeline(device, pipeline);
        }
    }
    if (p.apply != nullptr) {
        SDL_ReleaseGPUGraphicsPipeline(device, p.apply);
    }
}

void Renderer::Impl::ReleaseGtaoTargets() {
    for (SDL_GPUTexture** texture : {&gtao.depth, &gtao.term, &gtao.edges, &gtao.result}) {
        if (*texture != nullptr) {
            ReleaseTrackedTexture(device, *texture);
            *texture = nullptr;
        }
    }
    gtao.width = 0;
    gtao.height = 0;
}

void Renderer::Impl::RenderGtao(FrameContext& frame) {
    const int width = static_cast<int>(frame.width);
    const int height = static_cast<int>(frame.height);
    if (gtao.width != width || gtao.height != height) {
        ReleaseGtaoTargets();
        const auto create = [this, width, height](SDL_GPUTextureFormat format, Uint32 levels, SDL_GPUTextureUsageFlags usage) {
            SDL_GPUTextureCreateInfo info{};
            info.type = SDL_GPU_TEXTURETYPE_2D;
            info.format = format;
            info.usage = usage;
            info.width = static_cast<Uint32>(width);
            info.height = static_cast<Uint32>(height);
            info.layer_count_or_depth = 1;
            info.num_levels = levels;
            if (!SDL_GPUTextureSupportsFormat(device, format, info.type, usage)) {
                FatalError((std::string("Renderer: the graphics card cannot write an image format XeGTAO needs from a compute shader: ") + std::to_string(static_cast<int>(format))).c_str());
            }
            SDL_GPUTexture* texture = CreateTrackedTexture(device, &info);
            if (texture == nullptr) {
                FatalSdlError("cannot create an ambient occlusion image");
            }
            return texture;
        };
        // Unsigned integer images cannot be sampled in SDL GPU: they are read as storage images.
        const SDL_GPUTextureUsageFlags sampled = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE;
        gtao.depth = create(SDL_GPU_TEXTUREFORMAT_R32_FLOAT, XE_GTAO_DEPTH_MIP_LEVELS, sampled);
        gtao.term = create(SDL_GPU_TEXTUREFORMAT_R32_UINT, 1,
                           SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_READ | SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE);
        gtao.edges = create(SDL_GPU_TEXTUREFORMAT_R8_UNORM, 1, sampled);
        gtao.result = create(SDL_GPU_TEXTUREFORMAT_R32_UINT, 1,
                             SDL_GPU_TEXTUREUSAGE_GRAPHICS_STORAGE_READ | SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE);
        gtao.width = width;
        gtao.height = height;
    }

    // Intel's quality levels by the preset's sample count (Environment::ambient_occlusion_samples):
    // up to 8 low (1 slice of 2 steps), 12 medium (2 of 2), 20 high (3 of 3), more ultra (9 of 3).
    const int samples = environment.ambient_occlusion_samples;
    const int level = samples <= 8 ? 0 : samples <= 12 ? 1 : samples <= 20 ? 2 : 3;
    XeGTAO::GTAOSettings settings;
    settings.QualityLevel = level;
    settings.DenoisePasses = 1;
    settings.Radius = std::max(environment.ambient_occlusion_radius, 0.01f);
    XeGTAO::GTAOConstants constants{};
    XeGTAO::GTAOUpdateConstants(constants, width, height, settings, &frame.projection[0][0], false,
                                static_cast<unsigned int>(frame_index));
    // Our depth is reversed (1 near, 0 far): view distance = y / (depth + x) with x and y from
    // the projection (FrameContext::projection_info); XeGTAO unpacks mul / (add - depth), so
    // mul = -y and add = -x.
    constants.DepthUnpackConsts = {-frame.projection_info.y, -frame.projection_info.x};
    // With temporal anti-aliasing the noise moves every frame and is averaged away.
    constants.NoiseIndex = frame.taa_on ? static_cast<int>(frame_index % 64) : 0;

    SDL_GPUCommandBuffer* commands = frame.commands;
    // 1. View-space depth and its four smaller levels.
    {
        SDL_GPUStorageTextureReadWriteBinding writes[XE_GTAO_DEPTH_MIP_LEVELS]{};
        for (Uint32 mip = 0; mip < XE_GTAO_DEPTH_MIP_LEVELS; ++mip) {
            writes[mip].texture = gtao.depth;
            writes[mip].mip_level = mip;
        }
        SDL_GPUComputePass* pass = SDL_BeginGPUComputePass(commands, writes, XE_GTAO_DEPTH_MIP_LEVELS, nullptr, 0);
        SDL_BindGPUComputePipeline(pass, gtao_pipelines.prefilter);
        SDL_GPUTextureSamplerBinding depth{prepass_depth, clamp_sampler};
        SDL_BindGPUComputeSamplers(pass, 0, &depth, 1);
        SDL_PushGPUComputeUniformData(commands, 0, &constants, sizeof(constants));
        SDL_DispatchGPUCompute(pass, static_cast<Uint32>((width + 15) / 16), static_cast<Uint32>((height + 15) / 16), 1);
        SDL_EndGPUComputePass(pass);
    }
    // 2. The occlusion term and the edges.
    {
        SDL_GPUStorageTextureReadWriteBinding writes[2]{};
        writes[0].texture = gtao.term;
        writes[1].texture = gtao.edges;
        SDL_GPUComputePass* pass = SDL_BeginGPUComputePass(commands, writes, 2, nullptr, 0);
        SDL_BindGPUComputePipeline(pass, gtao_pipelines.main[level]);
        SDL_GPUTextureSamplerBinding depth{gtao.depth, clamp_sampler};
        SDL_BindGPUComputeSamplers(pass, 0, &depth, 1);
        SDL_PushGPUComputeUniformData(commands, 0, &constants, sizeof(constants));
        SDL_DispatchGPUCompute(pass, static_cast<Uint32>((width + XE_GTAO_NUMTHREADS_X - 1) / XE_GTAO_NUMTHREADS_X),
                               static_cast<Uint32>((height + XE_GTAO_NUMTHREADS_Y - 1) / XE_GTAO_NUMTHREADS_Y), 1);
        SDL_EndGPUComputePass(pass);
    }
    // 3. The blur, two pixels across per thread.
    {
        SDL_GPUStorageTextureReadWriteBinding write{};
        write.texture = gtao.result;
        SDL_GPUComputePass* pass = SDL_BeginGPUComputePass(commands, &write, 1, nullptr, 0);
        SDL_BindGPUComputePipeline(pass, gtao_pipelines.denoise);
        SDL_GPUTextureSamplerBinding edges{gtao.edges, clamp_sampler};
        SDL_BindGPUComputeSamplers(pass, 0, &edges, 1);
        SDL_BindGPUComputeStorageTextures(pass, 0, &gtao.term, 1);
        SDL_PushGPUComputeUniformData(commands, 0, &constants, sizeof(constants));
        const int pairs = (width + 1) / 2;
        SDL_DispatchGPUCompute(pass, static_cast<Uint32>((pairs + XE_GTAO_NUMTHREADS_X - 1) / XE_GTAO_NUMTHREADS_X),
                               static_cast<Uint32>((height + XE_GTAO_NUMTHREADS_Y - 1) / XE_GTAO_NUMTHREADS_Y), 1);
        SDL_EndGPUComputePass(pass);
    }
    // 4. Into the ambient occlusion image, with the strength applied.
    const glm::vec4 apply_settings(environment.ambient_occlusion_strength, 0.0f, 0.0f, 0.0f);
    SDL_GPUColorTargetInfo target{};
    target.texture = ao_target;
    target.load_op = SDL_GPU_LOADOP_DONT_CARE;
    target.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(commands, &target, 1, nullptr);
    if (pass == nullptr) {
        FatalSdlError("cannot begin the ambient occlusion apply pass");
    }
    SDL_BindGPUGraphicsPipeline(pass, gtao_pipelines.apply);
    SDL_BindGPUFragmentStorageTextures(pass, 0, &gtao.result, 1);
    SDL_PushGPUFragmentUniformData(commands, 0, &apply_settings, sizeof(apply_settings));
    SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
    SDL_EndGPURenderPass(pass);
}

} // namespace poligon

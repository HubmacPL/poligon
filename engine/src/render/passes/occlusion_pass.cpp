// Pass 3b: occlusion culling's half on the graphics card (Environment::occlusion_culling): the
// depth pass's image shrunk to 128 x 72 farthest depths (occlusion_depth.frag.hlsl) and copied
// to a buffer the processor reads two submissions later (renderer/occlusion.cpp). Window
// frames only; render targets are not culled this way.

#include "../renderer/renderer_internal.h"
#include "poligon_shaders/fullscreen_vert_dxil.h"
#include "poligon_shaders/fullscreen_vert_spirv.h"
#include "poligon_shaders/occlusion_depth_frag_dxil.h"
#include "poligon_shaders/occlusion_depth_frag_spirv.h"

namespace poligon {

void Renderer::Impl::RenderOcclusionPass(FrameContext& frame) {
    if (!environment.occlusion_culling || active_target != 0) {
        return;
    }
    if (occlusion.target == nullptr) {
        occlusion.target = CreateTarget(SDL_GPU_TEXTUREFORMAT_R32_FLOAT, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER,
                                        kCoverageWidth, kCoverageHeight, SDL_GPU_SAMPLECOUNT_1, nullptr, "occlusion depth image");
        SDL_GPUShader* vertex = CreateShader(SDL_GPU_SHADERSTAGE_VERTEX, 0, 0, kFullscreenVertDxil, sizeof(kFullscreenVertDxil),
                                             kFullscreenVertSpirv, sizeof(kFullscreenVertSpirv));
        SDL_GPUShader* fragment = CreateShader(SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 1, kOcclusionDepthFragDxil, sizeof(kOcclusionDepthFragDxil),
                                               kOcclusionDepthFragSpirv, sizeof(kOcclusionDepthFragSpirv));
        occlusion.pipeline = CreateFullscreenPipeline(vertex, fragment, SDL_GPU_TEXTUREFORMAT_R32_FLOAT, FullscreenBlend::Replace);
        SDL_ReleaseGPUShader(device, vertex);
        SDL_ReleaseGPUShader(device, fragment);
        for (CoverageCopy& copy : occlusion.copies) {
            SDL_GPUTransferBufferCreateInfo info{};
            info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
            info.size = static_cast<Uint32>(kCoverageWidth * kCoverageHeight * sizeof(float));
            copy.buffer = SDL_CreateGPUTransferBuffer(device, &info);
            if (copy.buffer == nullptr) {
                FatalSdlError("cannot create an occlusion copy buffer");
            }
        }
    }
    const glm::vec4 sizes(frame.width, frame.height, static_cast<float>(kCoverageWidth), static_cast<float>(kCoverageHeight));
    SDL_GPUTextureSamplerBinding binding{};
    binding.texture = prepass_depth;
    binding.sampler = clamp_sampler;
    DrawFullscreenPass(frame.commands, occlusion.target, false, occlusion.pipeline, &binding, 1, &sizes, sizeof(sizes));

    // Into the copy this submission fills (the oldest of the ring: read two submissions ago).
    CoverageCopy& copy = occlusion.copies[submission_count % kCoverageCopies];
    copy.submission = submission_count + 1;  // the number this frame's submission will get
    copy.view_projection = frame.unshifted_view_projection;
    copy.origin = origin;
    SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(frame.commands);
    SDL_GPUTextureRegion source{};
    source.texture = occlusion.target;
    source.w = kCoverageWidth;
    source.h = kCoverageHeight;
    source.d = 1;
    SDL_GPUTextureTransferInfo destination{};
    destination.transfer_buffer = copy.buffer;
    destination.pixels_per_row = kCoverageWidth;
    destination.rows_per_layer = kCoverageHeight;
    SDL_DownloadFromGPUTexture(copy_pass, &source, &destination);
    SDL_EndGPUCopyPass(copy_pass);
}

} // namespace poligon

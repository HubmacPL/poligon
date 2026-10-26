#pragma once

// Ambient occlusion by Intel's XeGTAO (renderer/gtao.cpp; shaders/post/gtao_*; the library's
// own files in shaders/third_party/xegtao): its images, per view and made at the drawing size
// on first use, and its pipelines.

#include <SDL3/SDL_gpu.h>

namespace poligon {
namespace render_internal {

struct GtaoTargets {
    SDL_GPUTexture* depth = nullptr;   // view-space depth with 4 smaller levels (R32 float)
    SDL_GPUTexture* term = nullptr;    // the occlusion term before the blur (R32 unsigned)
    SDL_GPUTexture* edges = nullptr;   // the edges the blur keeps (R8)
    SDL_GPUTexture* result = nullptr;  // after the blur (R32 unsigned: visibility times 255)
    int width = 0;
    int height = 0;
};

struct GtaoPipelines {
    SDL_GPUComputePipeline* prefilter = nullptr;
    SDL_GPUComputePipeline* main[4] = {nullptr, nullptr, nullptr, nullptr};  // Intel's quality levels low to ultra
    SDL_GPUComputePipeline* denoise = nullptr;
    SDL_GPUGraphicsPipeline* apply = nullptr;  // the result into the ambient occlusion image
};

} // namespace render_internal
} // namespace poligon

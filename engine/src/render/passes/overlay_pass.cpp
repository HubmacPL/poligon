// Pass 7b: 2D over the finished picture (rectangles, sprites, text), after tone mapping and
// before the debug windows, in pixels of the output image.

#include "../renderer/renderer_internal.h"

namespace poligon {

void Renderer::Impl::RenderOverlayPass(FrameContext& frame) {
    if (overlay.quads.empty()) {
        return;
    }
    SDL_GPUCommandBuffer* commands = frame.commands;
    UploadDynamicBuffer(device, commands, overlay.quads.data(), static_cast<Uint32>(overlay.quads.size() * sizeof(OverlayQuad)),
                        overlay.buffer);

    SDL_GPUColorTargetInfo color_info{};
    color_info.texture = output_target;
    color_info.load_op = SDL_GPU_LOADOP_LOAD;
    color_info.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(commands, &color_info, 1, nullptr);
    if (pass == nullptr) {
        FatalSdlError("cannot begin overlay pass");
    }
    SDL_BindGPUGraphicsPipeline(pass, overlay.pipeline);
    SDL_BindGPUVertexStorageBuffers(pass, 0, &overlay.buffer.buffer, 1);
    for (const OverlayBatch& batch : overlay.batches) {
        const glm::vec4 uniforms(static_cast<float>(config.width), static_cast<float>(config.height), static_cast<float>(batch.first), 0.0f);
        SDL_PushGPUVertexUniformData(commands, 0, &uniforms, sizeof(uniforms));
        SDL_GPUTextureSamplerBinding binding{};
        binding.texture = batch.texture;
        binding.sampler = batch.pixelated ? clamp_sampler : linear_clamp_sampler;
        SDL_BindGPUFragmentSamplers(pass, 0, &binding, 1);
        SDL_DrawGPUPrimitives(pass, 6, batch.count, 0, 0);
    }
    SDL_EndGPURenderPass(pass);
}

} // namespace poligon

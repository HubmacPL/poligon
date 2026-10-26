// Pass 1b: the shadows of up to four spot lights (SpotLight::shadows), each the scene's depth
// seen from its light through the cone, in one quarter of a 2048 x 2048 depth image, like a
// shadow cascade but with a perspective camera. Meshes (DrawMesh, DrawSkinnedMesh, DrawModel)
// and instanced meshes cast them; terrain and impostors do not.

#include "../renderer/renderer_internal.h"

namespace poligon {

void Renderer::Impl::RenderSpotShadowPass(FrameContext& frame) {
    if (lights.shadowed_count == 0) {
        return;
    }
    SDL_GPUCommandBuffer* commands = frame.commands;
    if (lights.spot_shadow_atlas == nullptr) {
        lights.spot_shadow_atlas = CreateTarget(kDepthFormat, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER,
                                                kSpotShadowSize * 2, kSpotShadowSize * 2, SDL_GPU_SAMPLECOUNT_1, nullptr,
                                                "spot light shadow image");
    }
    SDL_GPUDepthStencilTargetInfo depth_info{};
    depth_info.texture = lights.spot_shadow_atlas;
    depth_info.clear_depth = 1.0f;
    depth_info.load_op = SDL_GPU_LOADOP_CLEAR;
    depth_info.store_op = SDL_GPU_STOREOP_STORE;
    depth_info.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
    depth_info.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(commands, nullptr, 0, &depth_info);
    if (pass == nullptr) {
        FatalSdlError("cannot begin spot light shadow pass");
    }
    SDL_GPUGraphicsPipeline* bound_pipeline = nullptr;
    for (int tile = 0; tile < lights.shadowed_count; ++tile) {
        SDL_GPUViewport viewport{};
        viewport.x = static_cast<float>((tile % 2) * kSpotShadowSize);
        viewport.y = static_cast<float>((tile / 2) * kSpotShadowSize);
        viewport.w = static_cast<float>(kSpotShadowSize);
        viewport.h = static_cast<float>(kSpotShadowSize);
        viewport.min_depth = 0.0f;
        viewport.max_depth = 1.0f;
        SDL_SetGPUViewport(pass, &viewport);

        const glm::mat4& view_projection = lights.spot_view_projection[tile];
        VertexFrameUniforms frame_uniforms;
        frame_uniforms.view_projection = view_projection;
        frame_uniforms.wind = WindUniform();
        frame_uniforms.trample = trample_info;
        SDL_PushGPUVertexUniformData(commands, 0, &frame_uniforms, sizeof(frame_uniforms));
        for (const DrawCommand& command : draw_commands) {
            if (IsBoxOutside(view_projection * command.transform, command.bounds_min, command.bounds_max, false)) {
                continue;
            }
            ++stats.shadow_draws;
            BindMeshPipeline(pass, kShadowPass, command, bound_pipeline);
            const ObjectUniforms object_uniforms = MakeObjectUniforms(command);
            SDL_PushGPUVertexUniformData(commands, 1, &object_uniforms, sizeof(object_uniforms));
            const MaterialUniforms material_uniforms = MakeMaterialUniforms(command.material, temporal_noise_offset);
            SDL_PushGPUFragmentUniformData(commands, 0, &material_uniforms, sizeof(material_uniforms));
            SDL_GPUTextureSamplerBinding binding{};
            binding.texture = GetTexture(command.material.texture, white_texture);
            binding.sampler = texture_sampler;
            SDL_BindGPUFragmentSamplers(pass, 0, &binding, 1);
            DrawCommandGeometry(pass, command);
        }
        DrawInstanceBatches(commands, pass, kShadowPass, kSpotShadowList + tile, false, bound_pipeline);
    }
    SDL_EndGPURenderPass(pass);
}

} // namespace poligon

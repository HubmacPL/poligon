// Pass 4b: water surfaces over the finished scene.

#include "../renderer/renderer_internal.h"

namespace poligon {

void Renderer::Impl::RenderWaterPass(FrameContext& frame) {
    Impl& impl = *this;
    SDL_GPUCommandBuffer* commands = frame.commands;
    const glm::mat4& view_projection = frame.view_projection;
    const bool water_on = frame.water_on;
    const float width = frame.width;
    const float height = frame.height;
    FragmentFrameUniforms& frame_uniforms = frame.frame_uniforms;
    SDL_GPUTexture*& scene_image = frame.scene_image;
    SDL_GPUTexture* white = frame.white;

    // --- Pass 4b: water. It reads the scene and its depth as they are without it (copies),
    // and writes its own depth, so that fog and anti-aliasing treat it as a surface. ---
    if (water_on) {
        SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(commands);
        SDL_GPUTextureLocation source{};
        source.texture = scene_image;
        SDL_GPUTextureLocation destination{};
        destination.texture = impl.water_scene_copy;
        SDL_CopyGPUTextureToTexture(copy_pass, &source, &destination, static_cast<Uint32>(impl.render_width),
                                    static_cast<Uint32>(impl.render_height), 1, false);
        SDL_EndGPUCopyPass(copy_pass);
        SDL_GPUTextureSamplerBinding depth_binding{};
        depth_binding.texture = impl.prepass_depth;
        depth_binding.sampler = impl.clamp_sampler;
        const glm::vec4 no_uniforms(0.0f);
        impl.DrawFullscreenPass(commands, impl.water_depth_copy, false, impl.depth_copy_pipeline, &depth_binding, 1,
                                &no_uniforms, sizeof(no_uniforms));

        SDL_GPUColorTargetInfo color_info{};
        color_info.texture = scene_image;
        color_info.load_op = SDL_GPU_LOADOP_LOAD;
        color_info.store_op = SDL_GPU_STOREOP_STORE;
        SDL_GPUDepthStencilTargetInfo depth_info{};
        depth_info.texture = impl.prepass_depth;
        depth_info.load_op = SDL_GPU_LOADOP_LOAD;
        depth_info.store_op = SDL_GPU_STOREOP_STORE;
        depth_info.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
        depth_info.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
        SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(commands, &color_info, 1, &depth_info);
        if (pass == nullptr) {
            FatalSdlError("cannot begin water pass");
        }
        SDL_BindGPUGraphicsPipeline(pass, impl.water_pipeline);
        VertexFrameUniforms vertex_frame_uniforms;
        vertex_frame_uniforms.view_projection = view_projection;
        vertex_frame_uniforms.wind = impl.WindUniform();
        vertex_frame_uniforms.trample = impl.trample_info;
        SDL_PushGPUVertexUniformData(commands, 0, &vertex_frame_uniforms, sizeof(vertex_frame_uniforms));
        SDL_PushGPUFragmentUniformData(commands, 0, &frame_uniforms, sizeof(frame_uniforms));
        SDL_GPUTextureSamplerBinding bindings[6]{};
        bindings[0].texture = impl.water_scene_copy;
        bindings[0].sampler = impl.linear_clamp_sampler;
        bindings[1].texture = impl.water_depth_copy;
        bindings[1].sampler = impl.clamp_sampler;
        bindings[2].texture = impl.shadow_atlas;
        bindings[2].sampler = impl.shadow_sampler;
        bindings[3].texture = white;
        bindings[3].sampler = impl.clamp_sampler;
        impl.AddLightingBindings(&bindings[4]);
        SDL_BindGPUFragmentSamplers(pass, 0, bindings, 6);
        const glm::mat4 inverse_view_projection = glm::inverse(view_projection);
        for (const WaterCommand& command : impl.water_commands) {
            const Mesh& mesh = impl.meshes[command.mesh_id - 1];
            if (IsBoxOutside(view_projection * command.transform, mesh.bounds_min, mesh.bounds_max, true)) {
                continue;
            }
            ObjectUniforms object_uniforms;
            object_uniforms.model = command.transform;
            object_uniforms.color = glm::vec4(1.0f);
            object_uniforms.uv_scale = glm::vec4(1.0f, 1.0f, 0.0f, 0.0f);
            SDL_PushGPUVertexUniformData(commands, 1, &object_uniforms, sizeof(object_uniforms));
            const WaterMaterial& look = command.material;
            WaterUniforms water_uniforms;
            water_uniforms.view_projection = view_projection;
            water_uniforms.inverse_view_projection = inverse_view_projection;
            water_uniforms.absorption = glm::vec4(look.absorption, look.scattering);
            water_uniforms.water_color = glm::vec4(look.color.r, look.color.g, look.color.b, look.roughness);
            water_uniforms.waves = glm::vec4(look.wave_size, look.wave_strength, environment.wind_time, look.refraction);
            const glm::vec2 wind = glm::length(environment.wind_direction) > 0.0f ? glm::normalize(environment.wind_direction)
                                                                                 : glm::vec2(1.0f, 0.0f);
            water_uniforms.wind = glm::vec4(wind, environment.wind_strength, look.shore_fade);
            water_uniforms.screen = glm::vec4(width, height, static_cast<float>(glm::clamp(environment.water_reflection_steps, 0, 128)),
                                              environment.water_reflection_distance);
            SDL_PushGPUFragmentUniformData(commands, 1, &water_uniforms, sizeof(water_uniforms));
            SDL_GPUBufferBinding vertex_binding{};
            vertex_binding.buffer = mesh.vertex_buffer;
            SDL_BindGPUVertexBuffers(pass, 0, &vertex_binding, 1);
            SDL_GPUBufferBinding index_binding{};
            index_binding.buffer = mesh.index_buffer;
            SDL_BindGPUIndexBuffer(pass, &index_binding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
            SDL_DrawGPUIndexedPrimitives(pass, mesh.index_count, 1, 0, 0, 0);
        }
        SDL_EndGPURenderPass(pass);
    }
}

} // namespace poligon

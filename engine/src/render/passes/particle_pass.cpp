// Pass 5b: particles over the finished scene, after temporal anti-aliasing (moving cards
// without motion vectors would smear in its history) and before bloom and exposure (so they
// glow and adapt like the scene). Each card is tested against the scene's depth image by its
// fragment shader, which also softens where it nears the scene. Drawn after volumetric fog
// and given fog and haze at their own distance; the volumetric fog in front of them is not
// added (see rendering.md).

#include "../renderer/renderer_internal.h"

namespace poligon {

void Renderer::Impl::RenderParticlePass(FrameContext& frame) {
    if (particles.calls.empty()) {
        return;
    }
    SDL_GPUCommandBuffer* commands = frame.commands;
    UploadDynamicBuffer(device, commands, particles.cards.data(), static_cast<Uint32>(particles.cards.size() * sizeof(ParticleCard)),
                        particles.buffer);

    // After TAA the scene image has the output's size and no sub-pixel shift.
    const glm::vec2 image_size = frame.taa_on ? glm::vec2(frame.output_width, frame.output_height) : glm::vec2(frame.width, frame.height);
    SDL_GPUColorTargetInfo color_info{};
    color_info.texture = frame.scene_image;
    color_info.load_op = SDL_GPU_LOADOP_LOAD;
    color_info.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(commands, &color_info, 1, nullptr);
    if (pass == nullptr) {
        FatalSdlError("cannot begin particle pass");
    }
    SDL_PushGPUFragmentUniformData(commands, 0, &frame.frame_uniforms, sizeof(frame.frame_uniforms));
    PushLightUniforms(commands, pass, image_size.x, image_size.y);
    SDL_GPUGraphicsPipeline* bound = nullptr;
    for (const ParticleCall& call : particles.calls) {
        SDL_GPUGraphicsPipeline* pipeline =
            call.options.blend == ParticleBlend::Additive ? particles.additive_pipeline : particles.alpha_pipeline;
        if (pipeline != bound) {
            SDL_BindGPUGraphicsPipeline(pass, pipeline);
            SDL_BindGPUVertexStorageBuffers(pass, 0, &particles.buffer.buffer, 1);
            bound = pipeline;
        }
        struct {
            glm::mat4 view_projection;
            glm::vec4 camera;
            glm::vec4 atlas;
        } vertex_uniforms{frame.unshifted_view_projection, glm::vec4(0.0f, 0.0f, 0.0f, static_cast<float>(call.first)),  // camera at 0
                          glm::vec4(static_cast<float>(glm::max(call.options.atlas_columns, 1)),
                                    static_cast<float>(glm::max(call.options.atlas_rows, 1)), glm::max(call.options.stretch, 0.0f), 0.0f)};
        SDL_PushGPUVertexUniformData(commands, 0, &vertex_uniforms, sizeof(vertex_uniforms));
        ParticleUniforms uniforms;
        uniforms.screen = glm::vec4(image_size, frame.width, frame.height);
        uniforms.projection = frame.projection_info;
        uniforms.settings = glm::vec4(glm::max(call.options.soft_distance, 0.0f), call.options.lighting == ParticleLighting::Simple ? 1.0f : 0.0f,
                                      call.options.blend == ParticleBlend::Additive ? 1.0f : 0.0f, 0.0f);
        SDL_PushGPUFragmentUniformData(commands, 1, &uniforms, sizeof(uniforms));
        SDL_GPUTextureSamplerBinding bindings[8]{};
        bindings[0].texture = GetTexture(call.texture, white_texture);
        bindings[0].sampler = linear_clamp_sampler;
        bindings[1].texture = prepass_depth;
        bindings[1].sampler = clamp_sampler;
        bindings[2].texture = shadow_atlas;
        bindings[2].sampler = shadow_sampler;
        bindings[3].texture = textures[white_texture.id - 1];
        bindings[3].sampler = clamp_sampler;
        AddLightingBindings(&bindings[4]);
        SDL_BindGPUFragmentSamplers(pass, 0, bindings, 8);
        SDL_DrawGPUPrimitives(pass, 6, call.count, 0, 0);
    }
    SDL_EndGPURenderPass(pass);
}

} // namespace poligon

// Pass 4a: decals pressed onto the opaque scene (grass included), before water, so that water
// shows them under its surface. They are lit like the surfaces they lie on.

#include "../renderer/renderer_internal.h"

namespace poligon {

void Renderer::Impl::RenderDecalPass(FrameContext& frame) {
    decals.instances.clear();
    if (decals.commands.empty()) {
        return;
    }
    SDL_GPUCommandBuffer* commands = frame.commands;
    const glm::mat4& view_projection = frame.view_projection;

    // Boxes in view, in the order they were drawn; consecutive ones with the same material
    // make one call (an instanced draw), so the order is also the order they are laid on.
    struct Run {
        size_t command = 0;  // the first decal's command, for its material
        Uint32 first = 0;
        Uint32 count = 0;
    };
    std::vector<Run> runs;
    for (size_t i = 0; i < decals.commands.size(); ++i) {
        const DecalCommand& command = decals.commands[i];
        if (IsBoxOutside(view_projection * command.transform, glm::vec3(-0.5f), glm::vec3(0.5f), true)) {
            continue;
        }
        if (runs.empty() || !SameDecalMaterial(decals.commands[runs.back().command].material, command.material)) {
            runs.push_back({i, static_cast<Uint32>(decals.instances.size()), 0});
        }
        decals.instances.push_back({command.transform, glm::inverse(command.transform)});
        ++runs.back().count;
    }
    if (runs.empty()) {
        return;
    }
    UploadDynamicBuffer(device, commands, decals.instances.data(), static_cast<Uint32>(decals.instances.size() * sizeof(DecalInstance)),
                        decals.buffer);

    SDL_GPUColorTargetInfo color_info{};
    color_info.texture = frame.scene_image;
    color_info.load_op = SDL_GPU_LOADOP_LOAD;
    color_info.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(commands, &color_info, 1, nullptr);
    if (pass == nullptr) {
        FatalSdlError("cannot begin decal pass");
    }
    SDL_BindGPUGraphicsPipeline(pass, decals.pipeline);
    SDL_BindGPUVertexStorageBuffers(pass, 0, &decals.buffer.buffer, 1);
    SDL_PushGPUFragmentUniformData(commands, 0, &frame.frame_uniforms, sizeof(frame.frame_uniforms));
    PushLightUniforms(commands, pass, frame.width, frame.height);
    const glm::mat4 inverse_view_projection = glm::inverse(view_projection);
    for (const Run& run : runs) {
        const DecalMaterial& material = decals.commands[run.command].material;
        struct {
            glm::mat4 view_projection;
            glm::vec4 first;
        } vertex_uniforms{view_projection, glm::vec4(static_cast<float>(run.first), 0.0f, 0.0f, 0.0f)};
        SDL_PushGPUVertexUniformData(commands, 0, &vertex_uniforms, sizeof(vertex_uniforms));
        DecalUniforms uniforms;
        uniforms.inverse_view_projection = inverse_view_projection;
        uniforms.screen = glm::vec4(static_cast<float>(render_width), static_cast<float>(render_height),
                                    material.normal_texture.id != 0 ? 1.0f : 0.0f, glm::clamp(material.opacity, 0.0f, 1.0f));
        uniforms.color = ToVec4(material.color);
        uniforms.surface = glm::vec4(material.metallic, glm::clamp(material.roughness, 0.0f, 1.0f), glm::clamp(material.edge_fade, 0.0f, 1.0f),
                                     std::cos(glm::radians(glm::clamp(material.max_angle_degrees, 0.0f, 180.0f))));
        SDL_PushGPUFragmentUniformData(commands, 1, &uniforms, sizeof(uniforms));
        SDL_GPUTextureSamplerBinding bindings[9]{};
        bindings[0].texture = GetTexture(material.texture, white_texture);
        bindings[0].sampler = linear_clamp_sampler;
        bindings[1].texture = GetTexture(material.normal_texture, flat_normal_texture);
        bindings[1].sampler = linear_clamp_sampler;
        bindings[2].texture = prepass_depth;
        bindings[2].sampler = clamp_sampler;
        bindings[3].texture = shadow_atlas;
        bindings[3].sampler = shadow_sampler;
        bindings[4].texture = frame.ao_on ? ao_target : textures[white_texture.id - 1];
        bindings[4].sampler = clamp_sampler;
        AddLightingBindings(&bindings[5]);
        SDL_BindGPUFragmentSamplers(pass, 0, bindings, 9);
        SDL_DrawGPUPrimitives(pass, 36, run.count, 0, 0);
    }
    SDL_EndGPURenderPass(pass);
}

} // namespace poligon

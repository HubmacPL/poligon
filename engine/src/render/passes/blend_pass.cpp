// Pass 5a: blended materials (BlendMode::Blend: glass, windows) over the finished scene,
// after water and volumetric fog and before temporal anti-aliasing. Each object is lit like
// any surface and mixed over the scene by its alpha, from the farthest to the nearest (by
// the middle of its box), tested against the depth pass's image by its shader. The
// fog in front of it is its own: simple fog at its distance in ShadeSurface, volumetric fog
// worked out from the fog walked to the scene behind it (ApplyFogInFront in mesh.frag.hlsl).
// Blended objects cast no shadow and no ambient occlusion, and are in no depth image.

#include <algorithm>

#include "../renderer/renderer_internal.h"

namespace poligon {

void Renderer::Impl::RenderBlendPass(FrameContext& frame) {
    if (blended_commands.empty()) {
        return;
    }
    const glm::mat4& view_projection = frame.view_projection;
    // Farthest first; the camera is at 0.
    std::vector<std::pair<float, std::uint32_t>> order;
    order.reserve(blended_commands.size());
    for (std::uint32_t i = 0; i < blended_commands.size(); ++i) {
        const DrawCommand& command = blended_commands[i];
        if (IsBoxOutside(view_projection * command.transform, command.bounds_min, command.bounds_max, true)) {
            continue;
        }
        const glm::vec3 middle(command.transform * glm::vec4((command.bounds_min + command.bounds_max) * 0.5f, 1.0f));
        order.emplace_back(glm::dot(middle, middle), i);
    }
    stats.meshes_submitted += static_cast<int>(blended_commands.size());
    stats.meshes_drawn += static_cast<int>(order.size());
    if (order.empty()) {
        return;
    }
    // Stable, so objects at the same distance keep the order they were drawn in.
    std::stable_sort(order.begin(), order.end(), [](const auto& a, const auto& b) { return a.first > b.first; });

    SDL_GPUCommandBuffer* commands = frame.commands;
    SDL_GPUColorTargetInfo color_info{};
    color_info.texture = frame.scene_image;
    color_info.load_op = SDL_GPU_LOADOP_LOAD;
    color_info.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(commands, &color_info, 1, nullptr);
    if (pass == nullptr) {
        FatalSdlError("cannot begin blend pass");
    }

    VertexFrameUniforms vertex_frame_uniforms;
    vertex_frame_uniforms.view_projection = view_projection;
    vertex_frame_uniforms.wind = WindUniform();
    vertex_frame_uniforms.trample = trample_info;
    SDL_PushGPUVertexUniformData(commands, 0, &vertex_frame_uniforms, sizeof(vertex_frame_uniforms));
    // The scene pass's frame values, without ambient occlusion: its image belongs to what lies behind.
    FragmentFrameUniforms frame_uniforms = frame.frame_uniforms;
    frame_uniforms.screen.x = 0.0f;
    SDL_PushGPUFragmentUniformData(commands, 0, &frame_uniforms, sizeof(frame_uniforms));
    PushLightUniforms(commands, pass, frame.width, frame.height);
    BlendUniforms blend_uniforms;
    blend_uniforms.fog = glm::vec4(frame.fog_on ? 1.0f : 0.0f, environment.fog_density, environment.fog_height_falloff,
                                   static_cast<float>(static_cast<double>(environment.fog_base_height) - camera.position.y));
    blend_uniforms.screen = glm::vec4(frame.width, frame.height, glm::min(kFogWalkDistance, camera.far_plane), 0.0f);
    blend_uniforms.inverse_view_projection = glm::inverse(view_projection);
    SDL_PushGPUFragmentUniformData(commands, 2, &blend_uniforms, sizeof(blend_uniforms));

    SDL_GPUGraphicsPipeline* bound_pipeline = nullptr;
    for (const auto& entry : order) {
        const DrawCommand& command = blended_commands[entry.second];
        BindMeshPipeline(pass, kBlendPass, command, bound_pipeline);
        const ObjectUniforms object_uniforms = MakeObjectUniforms(command);
        SDL_PushGPUVertexUniformData(commands, 1, &object_uniforms, sizeof(object_uniforms));
        const MaterialUniforms material_uniforms = MakeMaterialUniforms(command.material, temporal_noise_offset);
        SDL_PushGPUFragmentUniformData(commands, 1, &material_uniforms, sizeof(material_uniforms));
        SDL_GPUTextureSamplerBinding bindings[12]{};
        FillMeshBindings(command.material, false, bindings);
        bindings[10].texture = frame.fog_on ? fog_target : frame.white;
        bindings[10].sampler = linear_clamp_sampler;
        bindings[11].texture = prepass_depth;
        bindings[11].sampler = clamp_sampler;
        SDL_BindGPUFragmentSamplers(pass, 0, bindings, 12);
        DrawCommandGeometry(pass, command);
    }
    SDL_EndGPURenderPass(pass);
}

} // namespace poligon

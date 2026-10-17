// Pass 1: the shadow cascades.

#include "../renderer/renderer_internal.h"

namespace poligon {

float Renderer::Impl::CascadeTileScale(int cascade) const {
    return cascade >= 2 ? glm::clamp(config.far_shadow_scale, 0.125f, 1.0f) : 1.0f;
}

Renderer::Impl::Cascades Renderer::Impl::ComputeCascades() const {
    Cascades cascades;
    const float aspect_ratio = static_cast<float>(config.width) / static_cast<float>(config.height);
    const float tan_half_fov = std::tan(glm::radians(camera.vertical_fov_degrees) * 0.5f);
    const glm::vec3 forward = glm::normalize(camera.target - camera.position);
    const glm::vec3 right = glm::normalize(glm::cross(forward, camera.up));
    const glm::vec3 up = glm::cross(right, forward);

    const glm::vec3 light_direction = glm::normalize(light.direction);
    const glm::vec3 light_up = std::fabs(light_direction.y) > 0.99f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
    // Rotation that looks along the light, used to align cascades to the shadow image grid.
    const glm::mat4 light_rotation = glm::lookAt(glm::vec3(0.0f), light_direction, light_up);
    const glm::mat4 light_rotation_inverse = glm::inverse(light_rotation);

    const float near_distance = camera.near_plane;
    cascades.far_distance = ComputeShadowCascadeEnds(near_distance, environment.shadow_distance);
    float range_start = near_distance;
    for (int i = 0; i < kCascadeCount; ++i) {
        const float range_end = cascades.far_distance[i];

        // A sphere around this part of the view. Its size does not change when the camera
        // turns, which keeps shadow edges from crawling.
        glm::vec3 corners[8];
        int corner_index = 0;
        for (const float distance : {range_start, range_end}) {
            const float half_height = distance * tan_half_fov;
            const float half_width = half_height * aspect_ratio;
            const glm::vec3 center = camera.position + forward * distance;
            for (const float x : {-1.0f, 1.0f}) {
                for (const float y : {-1.0f, 1.0f}) {
                    corners[corner_index++] = center + right * (x * half_width) + up * (y * half_height);
                }
            }
        }
        glm::vec3 sphere_center(0.0f);
        for (const glm::vec3& corner : corners) {
            sphere_center += corner / 8.0f;
        }
        float radius = 0.0f;
        for (const glm::vec3& corner : corners) {
            radius = glm::max(radius, glm::length(corner - sphere_center));
        }
        radius = std::ceil(radius * 8.0f) / 8.0f;

        // Move the center in whole shadow texels only; otherwise shadows shimmer as the camera moves.
        const float tile_scale = CascadeTileScale(i);
        cascades.tile_scale[i] = tile_scale;
        const float texel_size = radius * 2.0f / (static_cast<float>(config.shadow_map_size) * tile_scale);
        cascades.texel_size[i] = texel_size;
        glm::vec3 center_in_light = glm::vec3(light_rotation * glm::vec4(sphere_center, 1.0f));
        center_in_light.x = std::floor(center_in_light.x / texel_size) * texel_size;
        center_in_light.y = std::floor(center_in_light.y / texel_size) * texel_size;
        sphere_center = glm::vec3(light_rotation_inverse * glm::vec4(center_in_light, 1.0f));

        const glm::vec3 eye = sphere_center - light_direction * (radius + kShadowCasterDistance);
        const glm::mat4 light_view = glm::lookAt(eye, sphere_center, light_up);
        const glm::mat4 light_projection =
            glm::ortho(-radius, radius, -radius, radius, 0.0f, radius * 2.0f + kShadowCasterDistance);
        cascades.light_view_projection[i] = light_projection * light_view;
        cascades.depth_range[i] = radius * 2.0f + kShadowCasterDistance;

        // Clip coordinates (-1..1, y up) to this cascade's tile in the 2 x 2 shadow image
        // (0..1, y down).
        const float tile_x = static_cast<float>(i % 2);
        const float tile_y = static_cast<float>(i / 2);
        // A cascade with a smaller share uses the top-left part of its tile.
        glm::mat4 to_tile(1.0f);
        to_tile[0][0] = 0.25f * tile_scale;
        to_tile[1][1] = -0.25f * tile_scale;
        to_tile[3][0] = 0.25f * tile_scale + tile_x * 0.5f;
        to_tile[3][1] = 0.25f * tile_scale + tile_y * 0.5f;
        cascades.shadow_matrix[i] = to_tile * cascades.light_view_projection[i];

        range_start = range_end;
    }
    return cascades;
}

void Renderer::Impl::RenderShadowPass(FrameContext& frame) {
    Impl& impl = *this;
    SDL_GPUCommandBuffer* commands = frame.commands;
    const bool shadows_on = frame.shadows_on;
    Cascades& cascades = frame.cascades;

    // --- Pass 1: shadows. The scene's depth as seen from the sun, once per cascade. ---
    if (shadows_on) {
        cascades = impl.ComputeCascades();
        // A mesh outside the camera's view can still cast a shadow into it, so shadows are
        // culled separately, against each cascade's own volume.
        for (int cascade = 0; cascade < kCascadeCount; ++cascade) {
            impl.cascade_commands[cascade].clear();
            for (std::uint32_t i = 0; i < impl.draw_commands.size(); ++i) {
                const DrawCommand& command = impl.draw_commands[i];
                if (!IsBoxOutside(cascades.light_view_projection[cascade] * command.transform, command.bounds_min,
                                  command.bounds_max, false)) {
                    impl.cascade_commands[cascade].push_back(i);
                }
            }
            impl.stats.shadow_draws += static_cast<int>(impl.cascade_commands[cascade].size());
        }

        SDL_GPUDepthStencilTargetInfo depth_info{};
        depth_info.texture = impl.shadow_atlas;
        depth_info.clear_depth = 1.0f;
        depth_info.load_op = SDL_GPU_LOADOP_CLEAR;
        depth_info.store_op = SDL_GPU_STOREOP_STORE;
        depth_info.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
        depth_info.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
        SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(commands, nullptr, 0, &depth_info);
        if (pass == nullptr) {
            FatalSdlError("cannot begin shadow pass");
        }
        SDL_GPUGraphicsPipeline* bound_pipeline = nullptr;
        const float tile_size = static_cast<float>(impl.config.shadow_map_size);
        for (int cascade = 0; cascade < kCascadeCount; ++cascade) {
            SDL_GPUViewport viewport{};
            viewport.x = static_cast<float>(cascade % 2) * tile_size;
            viewport.y = static_cast<float>(cascade / 2) * tile_size;
            viewport.w = tile_size * cascades.tile_scale[cascade];
            viewport.h = tile_size * cascades.tile_scale[cascade];
            viewport.min_depth = 0.0f;
            viewport.max_depth = 1.0f;
            SDL_SetGPUViewport(pass, &viewport);

            VertexFrameUniforms frame_uniforms;
            frame_uniforms.view_projection = cascades.light_view_projection[cascade];
            frame_uniforms.wind = impl.WindUniform();
            frame_uniforms.trample = impl.trample_info;
            SDL_PushGPUVertexUniformData(commands, 0, &frame_uniforms, sizeof(frame_uniforms));

            for (const std::uint32_t command_index : impl.cascade_commands[cascade]) {
                const DrawCommand& command = impl.draw_commands[command_index];
                impl.BindMeshPipeline(pass, kShadowPass, command, bound_pipeline);
                const ObjectUniforms object_uniforms = Impl::MakeObjectUniforms(command);
                SDL_PushGPUVertexUniformData(commands, 1, &object_uniforms, sizeof(object_uniforms));
                const MaterialUniforms material_uniforms =
                    Impl::MakeMaterialUniforms(command.material, impl.temporal_noise_offset);
                SDL_PushGPUFragmentUniformData(commands, 0, &material_uniforms, sizeof(material_uniforms));

                SDL_GPUTextureSamplerBinding binding{};
                binding.texture = impl.GetTexture(command.material.texture, impl.white_texture);
                binding.sampler = impl.texture_sampler;
                SDL_BindGPUFragmentSamplers(pass, 0, &binding, 1);
                impl.DrawCommandGeometry(pass, command);
            }
            impl.DrawInstanceBatches(commands, pass, kShadowPass, 1 + cascade, false, bound_pipeline);
        }
        SDL_EndGPURenderPass(pass);
    }
}

} // namespace poligon

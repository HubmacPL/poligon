// Pass 4: the sky and every lit surface, in linear wide-range colors.

#include "../renderer/renderer_internal.h"

namespace poligon {

void Renderer::Impl::RenderScenePass(FrameContext& frame) {
    Impl& impl = *this;
    SDL_GPUCommandBuffer* commands = frame.commands;
    const glm::mat4& view_projection = frame.view_projection;
    const glm::vec3& light_direction = frame.light_direction;
    const bool shadows_on = frame.shadows_on;
    const bool ao_on = frame.ao_on;
    const bool fog_on = frame.fog_on;
    const bool clouds_on = frame.clouds_on;
    const bool scene_after_depth = frame.scene_after_depth;
    Cascades& cascades = frame.cascades;
    const float cloud_shadow_size = frame.cloud_shadow_size;
    const glm::vec2& cloud_shadow_corner = frame.cloud_shadow_corner;
    FragmentFrameUniforms& frame_uniforms = frame.frame_uniforms;

    // --- Pass 4: the scene, in linear wide-range colors. ---
    {
        SDL_GPUColorTargetInfo color_info{};
        color_info.texture = impl.scene_color;
        color_info.clear_color = impl.linear_clear_color;
        color_info.load_op = SDL_GPU_LOADOP_CLEAR;
        if (impl.msaa_samples > 1) {
            // Average the samples of every pixel into the single-sample image.
            color_info.store_op = SDL_GPU_STOREOP_RESOLVE;
            color_info.resolve_texture = impl.scene_resolved;
        } else {
            color_info.store_op = SDL_GPU_STOREOP_STORE;
        }

        SDL_GPUDepthStencilTargetInfo depth_info{};
        if (scene_after_depth) {
            // The depth pass's image, kept as it is: the scene pass only tests against it.
            depth_info.texture = impl.prepass_depth;
            depth_info.load_op = SDL_GPU_LOADOP_LOAD;
            depth_info.store_op = SDL_GPU_STOREOP_STORE;
        } else {
            depth_info.texture = impl.scene_depth;
            depth_info.clear_depth = 0.0f;  // reversed depth
            depth_info.load_op = SDL_GPU_LOADOP_CLEAR;
            depth_info.store_op = SDL_GPU_STOREOP_DONT_CARE;
        }
        depth_info.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
        depth_info.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;

        SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(commands, &color_info, 1, &depth_info);
        if (pass == nullptr) {
            FatalSdlError("cannot begin scene pass");
        }

        if (environment.draw_sky) {
            SkyUniforms sky_uniforms;
            sky_uniforms.inverse_view_projection = glm::inverse(view_projection);
            sky_uniforms.camera_position = glm::vec4(camera.position, 1.0f);
            sky_uniforms.light_direction = glm::vec4(light_direction, 0.0f);
            sky_uniforms.sun_color = ToVec4(light.color);
            sky_uniforms.sky_zenith = ToVec4(environment.sky_zenith);
            sky_uniforms.sky_horizon = ToVec4(environment.sky_horizon);
            sky_uniforms.sky_ground = ToVec4(environment.sky_ground);
            SDL_BindGPUGraphicsPipeline(pass, impl.sky_pipeline);
            SDL_PushGPUFragmentUniformData(commands, 0, &sky_uniforms, sizeof(sky_uniforms));
            SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
        }

        VertexFrameUniforms vertex_frame_uniforms;
        vertex_frame_uniforms.view_projection = view_projection;
        vertex_frame_uniforms.wind = impl.WindUniform();
        vertex_frame_uniforms.trample = impl.trample_info;
        SDL_PushGPUVertexUniformData(commands, 0, &vertex_frame_uniforms, sizeof(vertex_frame_uniforms));

        for (int i = 0; i < kCascadeCount; ++i) {
            frame_uniforms.shadow_matrices[i] = cascades.shadow_matrix[i];
        }
        frame_uniforms.cascade_far = cascades.far_distance;
        frame_uniforms.cascade_texel = cascades.texel_size;
        frame_uniforms.cascade_scale = cascades.tile_scale;
        frame_uniforms.cascade_depth = cascades.depth_range;
        // Shadows soften with distance from what casts them: the sun is a disc, not a point.
        const float sun_size = glm::radians(glm::clamp(environment.sun_angular_size, 0.0f, 10.0f));
        frame_uniforms.shadow_soft =
            glm::vec4(environment.soft_shadows && sun_size > 0.0f ? 2.0f * std::tan(sun_size * 0.5f) : 0.0f, 2.0f, 0.0f, 0.0f);
        frame_uniforms.cloud_shadow_area = glm::vec4(cloud_shadow_corner, 1.0f / cloud_shadow_size,
                                                     clouds_on && environment.cloud_shadows ? 1.0f : 0.0f);
        if (impl.sky_map.resolution >= 2 && impl.sky_occlusion_texture != nullptr) {
            frame_uniforms.sky_occlusion_area = glm::vec4(impl.sky_map.corner, 1.0f / impl.sky_map.size, 1.0f);
        }
        frame_uniforms.aerial = glm::vec4(environment.sky_horizon.r * environment.sky_light_intensity,
                                          environment.sky_horizon.g * environment.sky_light_intensity,
                                          environment.sky_horizon.b * environment.sky_light_intensity, environment.aerial_density);
        frame_uniforms.foliage = glm::vec4(environment.foliage_translucency, 0.0f, 0.0f, 0.0f);
        // Ambient light and reflections from the sky light map, when the sky lights the scene.
        frame_uniforms.sky_radiance = glm::vec4(static_cast<float>(kSkyRadianceLevels - 2),
                                                environment.sky_light && impl.sky_radiance_ready ? 1.0f : 0.0f,
                                                static_cast<float>(kSkyRadianceLevels - 1),
                                                environment.sky_light && impl.sky_radiance_ready ? glm::clamp(environment.fog_sky_color, 0.0f, 1.0f) : 0.0f);
        frame_uniforms.camera_position = glm::vec4(camera.position, 1.0f);
        frame_uniforms.camera_forward = glm::vec4(glm::normalize(camera.target - camera.position), 0.0f);
        frame_uniforms.light_direction = glm::vec4(light_direction, 0.0f);
        frame_uniforms.light_color = ToVec4(light.color);
        if (environment.sky_light) {
            // Upward-facing surfaces see mostly sky between the zenith and the horizon;
            // downward-facing ones see the ground.
            const glm::vec4 sky_average = (ToVec4(environment.sky_zenith) + ToVec4(environment.sky_horizon)) * 0.5f;
            frame_uniforms.ambient_sky = sky_average * environment.sky_light_intensity;
            frame_uniforms.ambient_ground = ToVec4(environment.sky_ground) * environment.sky_light_intensity;
        } else {
            frame_uniforms.ambient_sky = ToVec4(light.ambient);
            frame_uniforms.ambient_ground = ToVec4(light.ambient);
        }
        if (clouds_on) {
            // Under clouds the sky's light is greyer and softer: its color moves towards its own
            // brightness, by up to half at full coverage.
            const float cover = glm::clamp(environment.cloud_coverage, 0.0f, 1.0f) * 0.5f;
            const auto greyer = [cover](glm::vec4 color) {
                const float brightness = color.x * 0.2126f + color.y * 0.7152f + color.z * 0.0722f;
                return glm::mix(color, glm::vec4(brightness * 1.1f, brightness * 1.1f, brightness * 1.1f, color.w), cover);
            };
            frame_uniforms.ambient_sky = greyer(frame_uniforms.ambient_sky);
        }
        frame_uniforms.sky_zenith = ToVec4(environment.sky_zenith) * environment.sky_light_intensity;
        frame_uniforms.sky_horizon = ToVec4(environment.sky_horizon) * environment.sky_light_intensity;
        frame_uniforms.sky_ground = ToVec4(environment.sky_ground) * environment.sky_light_intensity;
        // Volumetric fog is added after the scene pass; the simple fog is then left out here.
        frame_uniforms.fog = glm::vec4(environment.fog_color.r, environment.fog_color.g, environment.fog_color.b,
                                       fog_on ? 0.0f : environment.fog_density);
        frame_uniforms.screen = glm::vec4(ao_on ? 1.0f : 0.0f, environment.show_shadow_cascades && shadows_on ? 1.0f : 0.0f,
                                          0.0f, 0.0f);
        frame_uniforms.options = glm::vec4(shadows_on ? 1.0f : 0.0f, impl.msaa_samples > 1 ? 1.0f : 0.0f,
                                           environment.sky_light ? 1.0f : 0.0f,
                                           1.0f / static_cast<float>(impl.config.shadow_map_size * 2));
        SDL_PushGPUFragmentUniformData(commands, 0, &frame_uniforms, sizeof(frame_uniforms));

        SDL_GPUGraphicsPipeline* bound_pipeline = nullptr;
        for (const std::uint32_t command_index : impl.visible_commands) {
            const DrawCommand& command = impl.draw_commands[command_index];
            const Material& material = command.material;
            impl.BindMeshPipeline(pass, scene_after_depth ? kSceneAfterDepthPass : kScenePass, command, bound_pipeline);

            const ObjectUniforms object_uniforms = Impl::MakeObjectUniforms(command);
            SDL_PushGPUVertexUniformData(commands, 1, &object_uniforms, sizeof(object_uniforms));
            const MaterialUniforms material_uniforms = Impl::MakeMaterialUniforms(material, impl.temporal_noise_offset);
            SDL_PushGPUFragmentUniformData(commands, 1, &material_uniforms, sizeof(material_uniforms));

            SDL_GPUTextureSamplerBinding bindings[8]{};
            bindings[0].texture = impl.GetTexture(material.texture, impl.white_texture);
            bindings[0].sampler = impl.texture_sampler;
            bindings[1].texture = impl.GetTexture(material.normal_texture, impl.flat_normal_texture);
            bindings[1].sampler = impl.texture_sampler;
            bindings[2].texture = impl.GetTexture(material.orm_texture, impl.white_texture);
            bindings[2].sampler = impl.texture_sampler;
            bindings[3].texture = impl.shadow_atlas;
            bindings[3].sampler = impl.shadow_sampler;
            bindings[4].texture = ao_on ? impl.ao_target : impl.textures[impl.white_texture.id - 1];
            bindings[4].sampler = impl.clamp_sampler;
            impl.AddLightingBindings(&bindings[5]);
            SDL_BindGPUFragmentSamplers(pass, 0, bindings, 8);
            impl.DrawCommandGeometry(pass, command);
        }
        impl.DrawInstanceBatches(commands, pass, scene_after_depth ? kSceneAfterDepthPass : kScenePass, 0, ao_on,
                                 bound_pipeline);
        SDL_EndGPURenderPass(pass);
    }
}

} // namespace poligon

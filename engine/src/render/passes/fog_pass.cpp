// Pass 5: volumetric fog.

#include "../renderer/renderer_internal.h"

namespace poligon {

void Renderer::Impl::RenderFogPass(FrameContext& frame) {
    Impl& impl = *this;
    SDL_GPUCommandBuffer* commands = frame.commands;
    const glm::mat4& view_projection = frame.view_projection;
    const glm::vec3& light_direction = frame.light_direction;
    const bool shadows_on = frame.shadows_on;
    const bool fog_on = frame.fog_on;
    const bool clouds_on = frame.clouds_on;
    Cascades& cascades = frame.cascades;
    const float width = frame.width;
    const float height = frame.height;
    const glm::vec4& projection_info = frame.projection_info;
    const float cloud_shadow_size = frame.cloud_shadow_size;
    const glm::dvec2& cloud_shadow_corner = frame.cloud_shadow_corner;
    SDL_GPUTexture*& scene_image = frame.scene_image;

    // --- Pass 5: volumetric fog at half resolution, then laid over the scene. ---
    if (fog_on) {
        FogUniforms fog_uniforms{};
        fog_uniforms.inverse_view_projection = glm::inverse(view_projection);
        for (int i = 0; i < kCascadeCount; ++i) {
            fog_uniforms.shadow_matrices[i] = cascades.shadow_matrix[i];
        }
        fog_uniforms.cascade_far = cascades.far_distance;
        // Relative to the camera, which is at 0 (see poligon/core/math.h).
        fog_uniforms.camera_position = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
        // w: the frame's noise offset for the step pattern (see temporal_noise_offset).
        fog_uniforms.camera_forward =
            glm::vec4(glm::normalize(glm::vec3(camera.target - camera.position)), impl.temporal_noise_offset);
        fog_uniforms.light_direction = glm::vec4(light_direction, 0.0f);
        fog_uniforms.light_color = ToVec4(light.color);
        fog_uniforms.fog = glm::vec4(environment.fog_color.r, environment.fog_color.g, environment.fog_color.b,
                                     environment.fog_density);
        fog_uniforms.fog_shape = glm::vec4(environment.fog_height_falloff,
                                           static_cast<float>(static_cast<double>(environment.fog_base_height) - camera.position.y),
                                           environment.fog_sun_glow, environment.fog_shadow_brightness);
        fog_uniforms.options = glm::vec4(shadows_on ? 1.0f : 0.0f, glm::min(kFogWalkDistance, camera.far_plane), width,
                                         height);
        // y: the sky light map's level for roughness 1, z: how much the fog takes the sky's color from it.
        const bool use_sky_map = environment.sky_light && impl.sky_radiance_ready;
        fog_uniforms.quality = glm::vec4(static_cast<float>(glm::clamp(environment.fog_steps, 4, 128)),
                                         static_cast<float>(kSkyRadianceLevels - 2),
                                         use_sky_map ? glm::clamp(environment.fog_sky_color, 0.0f, 1.0f) : 0.0f, 0.0f);
        const glm::vec2 corner(cloud_shadow_corner - glm::dvec2(camera.position.x, camera.position.z));
        fog_uniforms.cloud_shadow_area = glm::vec4(corner, 1.0f / cloud_shadow_size,
                                                   clouds_on && environment.cloud_shadows ? 1.0f : 0.0f);
        SDL_GPUTextureSamplerBinding bindings[4]{};
        bindings[0].texture = impl.prepass_depth;
        bindings[0].sampler = impl.clamp_sampler;
        bindings[1].texture = impl.shadow_atlas;
        bindings[1].sampler = impl.shadow_sampler;
        bindings[3].texture = impl.sky_radiance;
        bindings[3].sampler = impl.cube_sampler;
        bindings[2].texture = impl.frame_cloud_shadow;
        bindings[2].sampler = impl.linear_clamp_sampler;
        impl.DrawFullscreenPass(commands, impl.fog_target, false, impl.fog_pipeline, bindings, 4, &fog_uniforms,
                                sizeof(fog_uniforms));

        FogCompositeUniforms composite_uniforms;
        composite_uniforms.projection_info = projection_info;
        composite_uniforms.sizes =
            glm::vec4(static_cast<float>(impl.fog_width), static_cast<float>(impl.fog_height), width, height);
        bindings[0].texture = impl.fog_target;
        bindings[0].sampler = impl.clamp_sampler;
        bindings[1].texture = impl.prepass_depth;
        bindings[1].sampler = impl.clamp_sampler;
        impl.DrawFullscreenPass(commands, scene_image, true, impl.fog_composite_pipeline, bindings, 2,
                                &composite_uniforms, sizeof(composite_uniforms));
    }
}

} // namespace poligon

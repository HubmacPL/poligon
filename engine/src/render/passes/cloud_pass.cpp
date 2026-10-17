// Clouds: pass 1b, the share of sunlight they let through over the land around the camera
// (every lit surface and the fog read it), and pass 4c, the clouds themselves over the sky.

#include "../renderer/renderer_internal.h"

namespace poligon {

void Renderer::Impl::RenderCloudShadowPass(FrameContext& frame) {
    Impl& impl = *this;
    SDL_GPUCommandBuffer* commands = frame.commands;
    const glm::mat4& view_projection = frame.view_projection;
    const glm::vec3& light_direction = frame.light_direction;
    const bool clouds_on = frame.clouds_on;
    CloudUniforms& cloud_uniforms = frame.cloud_uniforms;
    const float cloud_shadow_size = frame.cloud_shadow_size;
    const glm::vec2& cloud_shadow_corner = frame.cloud_shadow_corner;

    // --- Pass 1b: cloud shadows. For the land around the camera, the share of sunlight the
    // clouds let through; every lit surface (and the fog) reads it. ---
    if (clouds_on) {
        const glm::vec2 wind = glm::length(environment.wind_direction) > 0.0f ? glm::normalize(environment.wind_direction)
                                                                             : glm::vec2(1.0f, 0.0f);
        const glm::vec2 drift = -wind * environment.cloud_speed * environment.wind_time;
        cloud_uniforms.inverse_view_projection = glm::inverse(view_projection);
        cloud_uniforms.camera_position = glm::vec4(camera.position, impl.temporal_noise_offset * 0.6180339f);
        cloud_uniforms.light_direction = glm::vec4(light_direction, 0.0f);
        cloud_uniforms.light_color = ToVec4(light.color);
        cloud_uniforms.sky_zenith = ToVec4(environment.sky_zenith) * environment.sky_light_intensity;
        cloud_uniforms.sky_horizon = ToVec4(environment.sky_horizon) * environment.sky_light_intensity;
        cloud_uniforms.layer = glm::vec4(environment.cloud_bottom, glm::max(environment.cloud_top, environment.cloud_bottom + 10.0f),
                                         glm::clamp(environment.cloud_coverage, 0.0f, 1.0f), environment.cloud_density);
        cloud_uniforms.shape = glm::vec4(environment.cloud_size, environment.cloud_size * 2.0f, drift.x, drift.y);
        cloud_uniforms.quality = glm::vec4(static_cast<float>(glm::clamp(environment.cloud_steps, 4, 256)),
                                           static_cast<float>(glm::clamp(environment.cloud_light_steps, 1, 16)), 40000.0f, 0.0f);
        cloud_uniforms.shadow_area = glm::vec4(cloud_shadow_corner, cloud_shadow_size, 0.0f);
        cloud_uniforms.weather = glm::vec4(glm::clamp(environment.cloud_variation, 0.0f, 1.0f), 0.0f, 0.0f, 0.0f);
        if (environment.cloud_shadows) {
            SDL_GPUTextureSamplerBinding bindings[3]{};
            bindings[0].texture = impl.cloud_shape;
            bindings[0].sampler = impl.repeat_sampler;
            bindings[1].texture = impl.cloud_detail;
            bindings[1].sampler = impl.repeat_sampler;
            bindings[2].texture = impl.cloud_weather;
            bindings[2].sampler = impl.repeat_sampler;
            impl.DrawFullscreenPass(commands, impl.cloud_shadow_target, false, impl.cloud_shadow_pipeline, bindings, 3, &cloud_uniforms,
                                    sizeof(cloud_uniforms));
        }
    }
    impl.frame_cloud_shadow = clouds_on && environment.cloud_shadows ? impl.cloud_shadow_target : impl.textures[impl.white_texture.id - 1];
    impl.frame_sky_occlusion = impl.sky_map.resolution >= 2 && impl.sky_occlusion_texture != nullptr
                                   ? impl.sky_occlusion_texture
                                   : impl.textures[impl.white_texture.id - 1];
}

void Renderer::Impl::RenderCloudPass(FrameContext& frame) {
    Impl& impl = *this;
    SDL_GPUCommandBuffer* commands = frame.commands;
    const bool clouds_on = frame.clouds_on;
    const float width = frame.width;
    const float height = frame.height;
    CloudUniforms& cloud_uniforms = frame.cloud_uniforms;
    SDL_GPUTexture*& scene_image = frame.scene_image;

    // --- Pass 4c: clouds, walked at a smaller size where the sky shows, built up over frames,
    // then laid over it. ---
    if (clouds_on) {
        // One pixel of every 2 x 2 block is walked per frame, in turn; the rest comes from the
        // previous frame's image, unless there is none to use (first frame, a camera jump,
        // a frame without clouds in between): then every pixel is walked.
        const bool history_usable = impl.cloud_history_valid && impl.cloud_history_frame + 1 == impl.frame_index &&
                                    glm::distance(camera.position, impl.previous_camera_position) <= kTaaCutDistance;
        const int phase = static_cast<int>(impl.frame_index % 4);
        cloud_uniforms.weather.y = static_cast<float>(phase);
        cloud_uniforms.weather.z = history_usable ? 0.0f : 1.0f;
        // Each pixel is walked every fourth frame: its step offset moves by the golden ratio
        // per walk, so that over its walks the offsets cover the step evenly (the frame-based
        // offset would give the four phases different sets and stripe the image by rows).
        if (history_usable) {
            const float walk = static_cast<float>((impl.frame_index / 4) % 64);
            cloud_uniforms.camera_position.w = walk * 0.618034f - std::floor(walk * 0.618034f);
        }
        cloud_uniforms.quality.w = static_cast<float>(width);
        cloud_uniforms.shadow_area.w = static_cast<float>(height);
        SDL_GPUTextureSamplerBinding bindings[4]{};
        bindings[0].texture = impl.prepass_depth;
        bindings[0].sampler = impl.clamp_sampler;
        bindings[1].texture = impl.cloud_shape;
        bindings[1].sampler = impl.repeat_sampler;
        bindings[2].texture = impl.cloud_detail;
        bindings[2].sampler = impl.repeat_sampler;
        bindings[3].texture = impl.cloud_weather;
        bindings[3].sampler = impl.repeat_sampler;
        impl.DrawFullscreenPass(commands, impl.cloud_target, history_usable, impl.clouds_pipeline, bindings, 4, &cloud_uniforms,
                                sizeof(cloud_uniforms));

        const int write = 1 - impl.cloud_history_index;
        CloudResolveUniforms resolve;
        resolve.inverse_view_projection = cloud_uniforms.inverse_view_projection;
        resolve.previous_view_projection = impl.previous_cloud_view_projection;
        resolve.camera_position = glm::vec4(camera.position, 0.0f);
        // A walked pixel keeps half of what it was: the step pattern averages out over frames.
        resolve.params = glm::vec4(static_cast<float>(phase), history_usable ? 0.0f : 1.0f, history_usable ? 1.0f : 0.0f, 0.5f);
        SDL_GPUTextureSamplerBinding resolve_bindings[2]{};
        resolve_bindings[0].texture = impl.cloud_target;
        resolve_bindings[0].sampler = impl.clamp_sampler;
        resolve_bindings[1].texture = impl.cloud_history[impl.cloud_history_index];
        resolve_bindings[1].sampler = impl.linear_clamp_sampler;
        impl.DrawFullscreenPass(commands, impl.cloud_history[write], false, impl.clouds_resolve_pipeline, resolve_bindings, 2,
                                &resolve, sizeof(resolve));
        impl.cloud_history_index = write;
        impl.cloud_history_valid = true;
        impl.cloud_history_frame = impl.frame_index;
        impl.previous_cloud_view_projection = frame.view_projection;

        SDL_GPUTextureSamplerBinding composite[2]{};
        composite[0].texture = impl.cloud_history[write];
        composite[0].sampler = impl.linear_clamp_sampler;
        composite[1].texture = impl.prepass_depth;
        composite[1].sampler = impl.clamp_sampler;
        const glm::vec4 no_uniforms(0.0f);
        impl.DrawFullscreenPass(commands, scene_image, true, impl.clouds_composite_pipeline, composite, 2, &no_uniforms,
                                sizeof(no_uniforms));
    }
}

} // namespace poligon

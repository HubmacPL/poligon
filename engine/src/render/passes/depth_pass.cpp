// Passes 2 and 3: the scene's depth, then ambient occlusion from it; what casts no ambient
// occlusion (grass) joins the depth image after it.

#include "../renderer/renderer_internal.h"

namespace poligon {

void Renderer::Impl::RenderDepthPass(FrameContext& frame) {
    Impl& impl = *this;
    SDL_GPUCommandBuffer* commands = frame.commands;
    const glm::mat4& view_projection = frame.view_projection;
    const glm::mat4& projection = frame.projection;
    const glm::mat4& unshifted_view_projection = frame.unshifted_view_projection;
    const bool taa_on = frame.taa_on;
    const bool ao_on = frame.ao_on;
    const bool fog_on = frame.fog_on;
    const bool scene_after_depth = frame.scene_after_depth;
    const bool water_on = frame.water_on;
    const float width = frame.width;
    const float height = frame.height;
    const glm::vec4& projection_info = frame.projection_info;

    // --- Pass 2: depth of the scene, for ambient occlusion, fog and temporal anti-aliasing,
    // and without multisampling also for the scene pass itself: it then tests this depth
    // before shading, so each pixel is lit once instead of once per leaf behind it. With
    // temporal anti-aliasing the same pass writes motion vectors (motion_target). ---
    // The vertex uniforms of both depth passes: with TAA, the cameras and wind of this frame
    // and the previous one, for motion vectors.
    VertexFrameUniforms frame_uniforms;
    frame_uniforms.view_projection = view_projection;
    frame_uniforms.wind = impl.WindUniform();
    frame_uniforms.trample = impl.trample_info;
    frame_uniforms.motion_view_projection = unshifted_view_projection;
    frame_uniforms.previous_view_projection = frame.previous_view_projection;
    frame_uniforms.previous_wind = impl.has_previous_wind ? impl.previous_wind : frame_uniforms.wind;
    frame_uniforms.motion = glm::vec4(taa_on ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f);
    const auto begin_depth_pass = [&](bool clear) {
        SDL_GPUColorTargetInfo motion_info{};
        motion_info.texture = impl.motion_target;
        motion_info.clear_color = SDL_FColor{kNoMotion, kNoMotion, 0.0f, 0.0f};
        motion_info.load_op = clear ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
        motion_info.store_op = SDL_GPU_STOREOP_STORE;
        SDL_GPUDepthStencilTargetInfo depth_info{};
        depth_info.texture = impl.prepass_depth;
        depth_info.clear_depth = 0.0f;  // reversed depth
        depth_info.load_op = clear ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
        depth_info.store_op = SDL_GPU_STOREOP_STORE;
        depth_info.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
        depth_info.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
        SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(commands, &motion_info, 1, &depth_info);
        if (pass == nullptr) {
            FatalSdlError("cannot begin depth pass");
        }
        SDL_PushGPUVertexUniformData(commands, 0, &frame_uniforms, sizeof(frame_uniforms));
        return pass;
    };
    // Decals find the surfaces they lie on in this depth image, particles and blended surfaces what hides them.
    if (ao_on || fog_on || taa_on || scene_after_depth || water_on || (environment.clouds && environment.draw_sky) ||
        !impl.decals.commands.empty() || !impl.particles.calls.empty() || !impl.blended_commands.empty() ||
        (environment.occlusion_culling && impl.active_target == 0)) {
        SDL_GPURenderPass* pass = begin_depth_pass(true);
        // With ambient occlusion on, what does not cast it is left out here and added after it.
        impl.DrawDepthGeometry(commands, pass, ao_on ? Impl::kAoCasters : Impl::kAllBatches);
        SDL_EndGPURenderPass(pass);
    }

    // --- Pass 3: ambient occlusion from that depth, then a blur across and a blur down. ---
    if (ao_on) {
        AoUniforms ao_uniforms;
        ao_uniforms.projection_info = projection_info;
        ao_uniforms.screen =
            glm::vec4(width, height, static_cast<float>(glm::clamp(environment.ambient_occlusion_samples, 4, 64)), 0.0f);
        ao_uniforms.ao = glm::vec4(environment.ambient_occlusion_radius, environment.ambient_occlusion_strength,
                                   0.5f * height * projection[1][1], impl.temporal_noise_offset);
        SDL_GPUTextureSamplerBinding bindings[2]{};
        bindings[0].texture = impl.prepass_depth;
        bindings[0].sampler = impl.clamp_sampler;
        impl.DrawFullscreenPass(commands, impl.ao_target, false, impl.ao_pipeline, bindings, 1, &ao_uniforms,
                                sizeof(ao_uniforms));

        AoBlurUniforms blur_uniforms;
        blur_uniforms.projection_info = projection_info;
        bindings[1].texture = impl.prepass_depth;
        bindings[1].sampler = impl.clamp_sampler;
        bindings[0].texture = impl.ao_target;
        blur_uniforms.direction = glm::vec4(1.0f, 0.0f, width, height);
        impl.DrawFullscreenPass(commands, impl.ao_scratch, false, impl.ao_blur_pipeline, bindings, 2, &blur_uniforms,
                                sizeof(blur_uniforms));
        bindings[0].texture = impl.ao_scratch;
        blur_uniforms.direction = glm::vec4(0.0f, 1.0f, width, height);
        impl.DrawFullscreenPass(commands, impl.ao_target, false, impl.ao_blur_pipeline, bindings, 2, &blur_uniforms,
                                sizeof(blur_uniforms));

        // What casts no ambient occlusion (grass) joins the depth image now, for the scene
        // pass, water, fog and anti-aliasing.
        if (impl.HasAoNonCasters()) {
            SDL_GPURenderPass* pass = begin_depth_pass(false);
            impl.DrawDepthGeometry(commands, pass, Impl::kAoNonCasters);
            SDL_EndGPURenderPass(pass);
        }
    }
}

} // namespace poligon

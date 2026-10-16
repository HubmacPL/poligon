// Pass 5b: temporal anti-aliasing.

#include "../renderer/renderer_internal.h"

namespace poligon {

void Renderer::Impl::RenderTemporalAaPass(FrameContext& frame) {
    Impl& impl = *this;
    SDL_GPUCommandBuffer* commands = frame.commands;
    const glm::mat4& view_projection = frame.view_projection;
    const glm::mat4& unshifted_view_projection = frame.unshifted_view_projection;
    const glm::vec2& jitter_pixels = frame.jitter_pixels;
    const bool taa_on = frame.taa_on;
    const float width = frame.width;
    const float height = frame.height;
    const float output_width = frame.output_width;
    const float output_height = frame.output_height;
    SDL_GPUTexture*& scene_image = frame.scene_image;

    // --- Pass 5b: temporal anti-aliasing. The new frame is blended into the previous
    // result; from here on the result stands for the scene. ---
    if (taa_on) {
        const int write = 1 - impl.taa_index;
        TaaUniforms taa_uniforms;
        taa_uniforms.inverse_view_projection = glm::inverse(view_projection);
        taa_uniforms.current_view_projection = unshifted_view_projection;
        taa_uniforms.previous_view_projection = impl.previous_view_projection;
        taa_uniforms.parameters = glm::vec4(glm::clamp(impl.post.temporal_aa_blend, 0.01f, 1.0f),
                                            impl.has_taa_history ? 1.0f : 0.0f, output_width, output_height);
        taa_uniforms.input = glm::vec4(width, height, jitter_pixels.x, jitter_pixels.y);
        SDL_GPUTextureSamplerBinding bindings[3]{};
        bindings[0].texture = scene_image;
        bindings[0].sampler = impl.clamp_sampler;
        bindings[1].texture = impl.taa_targets[impl.taa_index];
        bindings[1].sampler = impl.linear_clamp_sampler;
        bindings[2].texture = impl.prepass_depth;
        bindings[2].sampler = impl.clamp_sampler;
        impl.DrawFullscreenPass(commands, impl.taa_targets[write], false, impl.taa_pipeline, bindings, 3, &taa_uniforms,
                                sizeof(taa_uniforms));
        impl.taa_index = write;
        impl.has_taa_history = true;
        scene_image = impl.taa_targets[write];
    }
}

} // namespace poligon

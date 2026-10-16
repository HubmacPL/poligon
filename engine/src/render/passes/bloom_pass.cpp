// Pass 6: bloom and the brightness measured for automatic exposure.

#include "../renderer/renderer_internal.h"

namespace poligon {

void Renderer::Impl::RenderBloomPass(FrameContext& frame) {
    Impl& impl = *this;
    SDL_GPUCommandBuffer* commands = frame.commands;
    const bool taa_on = frame.taa_on;
    const bool bloom_on = frame.bloom_on;
    const float width = frame.width;
    const float height = frame.height;
    const float output_width = frame.output_width;
    const float output_height = frame.output_height;
    SDL_GPUTexture*& scene_image = frame.scene_image;

    // --- Pass 6: bloom. A chain of ever smaller blurred copies of the scene, then mixed
    // back together from the smallest up. Automatic exposure reads the smallest copy. ---
    if (bloom_on || impl.post.auto_exposure) {
        SDL_GPUTexture* source = scene_image;
        float source_width = taa_on ? output_width : width;
        float source_height = taa_on ? output_height : height;
        for (size_t i = 0; i < impl.bloom_levels.size(); ++i) {
            const BloomLevel& level = impl.bloom_levels[i];
            ParameterUniforms uniforms;
            uniforms.parameters = glm::vec4(1.0f / source_width, 1.0f / source_height, i == 0 ? 1.0f : 0.0f, 0.0f);
            SDL_GPUTextureSamplerBinding binding{};
            binding.texture = source;
            binding.sampler = impl.linear_clamp_sampler;
            impl.DrawFullscreenPass(commands, level.texture, false, impl.bloom_down_pipeline, &binding, 1, &uniforms,
                                    sizeof(uniforms));
            source = level.texture;
            source_width = static_cast<float>(level.width);
            source_height = static_cast<float>(level.height);
        }
    }
    if (impl.post.auto_exposure) {
        const int previous = impl.luminance_index;
        const int current = 1 - previous;
        ParameterUniforms uniforms;
        uniforms.parameters = glm::vec4(
            impl.has_luminance_history ? glm::clamp(impl.post.auto_exposure_speed, 0.0f, 1.0f) : 1.0f, 0.0f, 0.0f, 0.0f);
        SDL_GPUTextureSamplerBinding bindings[2]{};
        bindings[0].texture = impl.bloom_levels.back().texture;
        bindings[0].sampler = impl.linear_clamp_sampler;
        bindings[1].texture = impl.luminance_targets[previous];
        bindings[1].sampler = impl.clamp_sampler;
        impl.DrawFullscreenPass(commands, impl.luminance_targets[current], false, impl.luminance_pipeline, bindings, 2,
                                &uniforms, sizeof(uniforms));
        impl.luminance_index = current;
        impl.has_luminance_history = true;
    }
    if (bloom_on) {
        for (size_t i = impl.bloom_levels.size() - 1; i > 0; --i) {
            const BloomLevel& smaller = impl.bloom_levels[i];
            ParameterUniforms uniforms;
            uniforms.parameters = glm::vec4(1.0f / static_cast<float>(smaller.width),
                                            1.0f / static_cast<float>(smaller.height), kBloomSpread, 0.0f);
            SDL_GPUTextureSamplerBinding binding{};
            binding.texture = smaller.texture;
            binding.sampler = impl.linear_clamp_sampler;
            impl.DrawFullscreenPass(commands, impl.bloom_levels[i - 1].texture, true, impl.bloom_up_pipeline, &binding, 1,
                                    &uniforms, sizeof(uniforms));
        }
    }
}

} // namespace poligon

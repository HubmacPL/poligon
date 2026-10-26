// Passes 7 and 8: tone mapping into the output image, then the debug windows over it.

#include "../renderer/renderer_internal.h"

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlgpu3.h>

namespace poligon {

void Renderer::Impl::RenderOutputPass(FrameContext& frame) {
    Impl& impl = *this;
    SDL_GPUCommandBuffer* commands = frame.commands;
    const bool taa_on = frame.taa_on;
    const bool ao_on = frame.ao_on;
    const bool fog_on = frame.fog_on;
    const bool bloom_on = frame.bloom_on;
    SDL_GPUTexture*& scene_image = frame.scene_image;
    SDL_GPUTexture* white = frame.white;

    // --- Pass 7: tone mapping into the output image. ---
    {
        // The debug view shows one stage's image instead of the finished frame; a stage that
        // is turned off shows as white.
        SDL_GPUTexture* debug_image = white;
        float debug_mode = 0.0f;
        switch (impl.post.debug_view) {
        case DebugView::None:
            break;
        case DebugView::AmbientOcclusion:
            debug_image = ao_on ? impl.ao_target : white;
            debug_mode = 1.0f;
            break;
        case DebugView::Fog:
            debug_image = fog_on ? impl.fog_target : white;
            debug_mode = 2.0f;
            break;
        case DebugView::Bloom:
            debug_image = bloom_on ? impl.bloom_levels[0].texture : white;
            debug_mode = 2.0f;
            break;
        case DebugView::CloudShadow:
            debug_image = impl.frame_cloud_shadow;
            debug_mode = 1.0f;
            break;
        case DebugView::Motion:
            debug_image = taa_on ? impl.motion_target : white;
            debug_mode = taa_on ? 3.0f : 1.0f;
            break;
        }

        SDL_GPUTextureSamplerBinding bindings[4]{};
        bindings[0].texture = scene_image;
        bindings[0].sampler = impl.linear_clamp_sampler;
        bindings[1].texture = bloom_on ? impl.bloom_levels[0].texture : white;
        bindings[1].sampler = impl.linear_clamp_sampler;
        bindings[2].texture = impl.post.auto_exposure ? impl.luminance_targets[impl.luminance_index] : white;
        bindings[2].sampler = impl.clamp_sampler;
        bindings[3].texture = debug_image;
        bindings[3].sampler = impl.linear_clamp_sampler;
        TonemapUniforms uniforms;
        uniforms.parameters = glm::vec4(impl.post.exposure, static_cast<float>(impl.post.tone_mapping),
                                        bloom_on ? impl.post.bloom_strength : 0.0f,
                                        impl.post.auto_exposure ? 1.0f : 0.0f);
        uniforms.auto_exposure = glm::vec4(impl.post.auto_exposure_target, impl.post.auto_exposure_min,
                                           impl.post.auto_exposure_max, debug_mode);
        // White balance: warm raises red and lowers blue, tint trades green for magenta;
        // divided by its own brightness, so that it changes color and not exposure.
        const float temperature = glm::clamp(impl.post.temperature, -1.0f, 1.0f);
        const float tint = glm::clamp(impl.post.tint, -1.0f, 1.0f);
        glm::vec3 balance(1.0f + 0.2f * temperature, 1.0f - 0.15f * tint, 1.0f - 0.2f * temperature);
        balance /= glm::dot(balance, glm::vec3(0.2126f, 0.7152f, 0.0722f));
        uniforms.white_balance = glm::vec4(balance, 0.0f);
        uniforms.grading = glm::vec4(glm::max(impl.post.saturation, 0.0f), glm::max(impl.post.contrast, 0.0f),
                                     glm::clamp(impl.post.vignette, 0.0f, 1.0f), glm::clamp(impl.post.sharpen, 0.0f, 2.0f));
        // 1: the scene image has the output's size (read pixel by pixel); 0: it is smaller
        // (drawn at render_scale without TAA) and is stretched.
        const bool same_size = taa_on || (impl.render_width == impl.config.width && impl.render_height == impl.config.height);
        uniforms.input = glm::vec4(same_size ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f);
        impl.DrawFullscreenPass(commands, impl.output_target, false, impl.tonemap_pipeline, bindings, 4, &uniforms,
                                sizeof(uniforms));
    }

    // --- Pass 7b: 2D (HUD, text), over the tone-mapped picture. ---
    impl.RenderOverlayPass(frame);

    // --- Pass 8: debug windows, drawn over the finished picture. ---
    if (impl.debug_ui_active) {
        ImGui::Render();
        ImDrawData* draw_data = ImGui::GetDrawData();
        ImGui_ImplSDLGPU3_PrepareDrawData(draw_data, commands);
        SDL_GPUColorTargetInfo color_info{};
        color_info.texture = impl.output_target;
        color_info.load_op = SDL_GPU_LOADOP_LOAD;
        color_info.store_op = SDL_GPU_STOREOP_STORE;
        SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(commands, &color_info, 1, nullptr);
        if (pass == nullptr) {
            FatalSdlError("cannot begin debug window pass");
        }
        ImGui_ImplSDLGPU3_RenderDrawData(draw_data, commands, pass);
        SDL_EndGPURenderPass(pass);
    }
}

} // namespace poligon

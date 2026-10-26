// The sky light map: the sky with its clouds captured into a small cube map around the
// camera, then blurred level by level as ever rougher surfaces reflect it. Surfaces take
// their ambient light and reflections of the sky from it (lighting.hlsli), the water its
// reflection where the screen holds nothing.
//
// The map is renewed over kSkyLightCycle frames: one face captured per frame, then the
// captured sky's smaller levels, then the blurred levels; the map surfaces read changes only
// on that last step, all at once. On the first frame, or when the previous frame did not
// renew it, everything is done at once.

#include "../renderer/renderer_internal.h"

namespace poligon {

void Renderer::Impl::RenderSkyLightPass(FrameContext& frame) {
    if (!environment.sky_light) {
        return;  // surfaces then take the two ambient colors
    }
    SDL_GPUCommandBuffer* commands = frame.commands;
    if (dummy_volume == nullptr) {
        // Stands in for the cloud noise volumes when there are no clouds (never read then).
        SDL_GPUTextureCreateInfo info{};
        info.type = SDL_GPU_TEXTURETYPE_3D;
        info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
        info.width = 1;
        info.height = 1;
        info.layer_count_or_depth = 1;
        info.num_levels = 1;
        dummy_volume = CreateTrackedTexture(device, &info);
        if (dummy_volume == nullptr) {
            FatalSdlError("cannot create a stand-in volume");
        }
    }

    const bool clouds = frame.clouds_on;
    const float intensity = environment.sky_light_intensity;
    SkyCaptureUniforms capture;
    capture.zenith = ToVec4(environment.sky_zenith) * intensity;
    capture.horizon = ToVec4(environment.sky_horizon) * intensity;
    capture.ground = ToVec4(environment.sky_ground) * intensity;
    capture.eye = glm::vec4(glm::vec3(camera.position), 0.0f);  // world float: the clouds' noise is kilometers wide
    capture.atmosphere = glm::vec4(atmosphere.sun_above_air * (kPi * intensity), atmosphere.on ? 1.0f : 0.0f);
    capture.to_sun = glm::vec4(-glm::normalize(light.direction), 0.0f);
    SDL_GPUTextureSamplerBinding capture_bindings[5]{};
    capture_bindings[0].texture = clouds ? cloud_shape : dummy_volume;
    capture_bindings[0].sampler = repeat_sampler;
    capture_bindings[1].texture = clouds ? cloud_detail : dummy_volume;
    capture_bindings[1].sampler = repeat_sampler;
    capture_bindings[2].texture = clouds ? cloud_weather : textures[white_texture.id - 1];
    capture_bindings[2].sampler = repeat_sampler;
    capture_bindings[3].texture = AtmosphereSkyView();
    capture_bindings[3].sampler = linear_clamp_sampler;
    capture_bindings[4].texture = AtmosphereTransmittance();
    capture_bindings[4].sampler = linear_clamp_sampler;

    const auto capture_face = [&](int face) {
        // The clouds are walked with fixed steps: the map is small and blurred, so fewer than
        // the view's are enough, and no frame-to-frame offset is needed.
        capture.capture = glm::vec4(static_cast<float>(face), clouds ? 1.0f : 0.0f, 24.0f, 3.0f);
        DrawFullscreenToCube(commands, sky_capture, face, 0, sky_capture_pipeline, capture_bindings, 5, &frame.cloud_uniforms,
                             sizeof(frame.cloud_uniforms), &capture, sizeof(capture));
    };
    const auto blur_levels = [&]() {
        SDL_GPUTextureSamplerBinding binding{};
        binding.texture = sky_capture;
        binding.sampler = cube_sampler;
        for (int level = 0; level < kSkyRadianceLevels; ++level) {
            // Levels for roughness 0 to 1, then the cosine-weighted light for ambient light.
            const bool ambient = level == kSkyRadianceLevels - 1;
            SkyPrefilterUniforms prefilter;
            prefilter.params = glm::vec4(0.0f, ambient ? -1.0f : static_cast<float>(level) / static_cast<float>(kSkyRadianceLevels - 2),
                                         static_cast<float>(kSkyCaptureSize), ambient ? 64.0f : 32.0f);
            for (int face = 0; face < 6; ++face) {
                prefilter.params.x = static_cast<float>(face);
                DrawFullscreenToCube(commands, sky_radiance, face, level, sky_prefilter_pipeline, &binding, 1, &prefilter,
                                     sizeof(prefilter));
            }
        }
    };

    // Render targets use the window's map: the cycle follows the window's frames only.
    if (active_target != 0 && sky_radiance_ready) {
        return;
    }
    const bool continuing = sky_radiance_ready && sky_light_frame + 1 == frame_index;
    if (!continuing) {
        for (int face = 0; face < 6; ++face) {
            capture_face(face);
        }
        SDL_GenerateMipmapsForGPUTexture(commands, sky_capture);
        blur_levels();
        sky_radiance_ready = true;
    } else {
        const int step = static_cast<int>(frame_index % kSkyLightCycle);
        if (step < 6) {
            capture_face(step);
        } else if (step == 6) {
            SDL_GenerateMipmapsForGPUTexture(commands, sky_capture);
        } else {
            blur_levels();
        }
    }
    sky_light_frame = frame_index;
}

} // namespace poligon

// Pass 2b: the sky view table of the atmosphere model (atmosphere_sky_view.frag.hlsl), for
// this frame's sun, before the sky light map and the scene read it. 192 x 108 pixels, about
// 14 steps through the air each; only with SkyModel::Atmosphere.

#include "../renderer/renderer_internal.h"

namespace poligon {

void Renderer::Impl::RenderAtmospherePass(FrameContext& frame) {
    if (!atmosphere.on || !(environment.draw_sky || environment.sky_light)) {
        return;
    }
    SkyViewUniforms uniforms;
    uniforms.to_sun = glm::vec4(-glm::normalize(light.direction), 0.0f);
    SDL_GPUTextureSamplerBinding bindings[2]{};
    bindings[0].texture = atmosphere.transmittance;
    bindings[0].sampler = linear_clamp_sampler;
    bindings[1].texture = atmosphere.multiple_scattering;
    bindings[1].sampler = linear_clamp_sampler;
    DrawFullscreenPass(frame.commands, atmosphere.sky_view, false, atmosphere.sky_view_pipeline, bindings, 2, &uniforms,
                       sizeof(uniforms));
}

} // namespace poligon

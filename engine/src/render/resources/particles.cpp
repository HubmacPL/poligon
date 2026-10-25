// Particles: recording (Renderer::DrawParticles: copy, sort from back to front) and the
// particle pipelines. Drawn by passes/particle_pass.cpp.

#include "../renderer/renderer_internal.h"

#include "poligon_shaders/particle_frag_dxil.h"
#include "poligon_shaders/particle_frag_spirv.h"
#include "poligon_shaders/particle_vert_dxil.h"
#include "poligon_shaders/particle_vert_spirv.h"

namespace poligon {

void Renderer::Impl::CreateParticlePipelines() {
    SDL_GPUShader* vertex = CreateShader(SDL_GPU_SHADERSTAGE_VERTEX, 0, 1, kParticleVertDxil, sizeof(kParticleVertDxil),
                                         kParticleVertSpirv, sizeof(kParticleVertSpirv), 1);
    SDL_GPUShader* fragment = CreateShader(SDL_GPU_SHADERSTAGE_FRAGMENT, 7, 2, kParticleFragDxil, sizeof(kParticleFragDxil),
                                           kParticleFragSpirv, sizeof(kParticleFragSpirv));
    particles.alpha_pipeline = CreateFullscreenPipeline(vertex, fragment, kSceneFormat, FullscreenBlend::Mix);
    particles.additive_pipeline = CreateFullscreenPipeline(vertex, fragment, kSceneFormat, FullscreenBlend::Add);
    SDL_ReleaseGPUShader(device, vertex);
    SDL_ReleaseGPUShader(device, fragment);
}

void Renderer::Impl::ReleaseParticles() {
    SDL_ReleaseGPUGraphicsPipeline(device, particles.alpha_pipeline);
    SDL_ReleaseGPUGraphicsPipeline(device, particles.additive_pipeline);
    ReleaseDynamicBuffer(device, particles.buffer);
}

void Renderer::DrawParticles(TextureHandle texture, const glm::dvec3& origin, const Particle* particles, int count,
                             const ParticleOptions& options) {
    if (!impl_->in_frame) {
        FatalError("Renderer::DrawParticles: call BeginFrame first");
    }
    if (texture.id > impl_->textures.size() || (texture.id != 0 && impl_->textures[texture.id - 1] == nullptr)) {
        FatalError("Renderer::DrawParticles: invalid or destroyed texture handle");
    }
    if (count < 0 || (count > 0 && particles == nullptr)) {
        FatalError("Renderer::DrawParticles: needs `count` particles");
    }
    if (count == 0) {
        return;
    }
    ParticleState& state = impl_->particles;
    ParticleCall call;
    call.texture = texture;
    call.options = options;
    call.first = static_cast<Uint32>(state.cards.size());
    call.count = static_cast<Uint32>(count);
    state.calls.push_back(call);
    // Positions relative to the emitter's origin become relative to the camera (the camera
    // is at 0). Alpha blending needs the far cards first; the order of equal distances stays.
    const glm::vec3 shift(origin - impl_->origin);
    state.order.clear();
    for (int i = 0; i < count; ++i) {
        const glm::vec3 offset = particles[i].position + shift;
        state.order.push_back({-glm::dot(offset, offset), static_cast<Uint32>(i)});
    }
    std::stable_sort(state.order.begin(), state.order.end(),
                     [](const std::pair<float, Uint32>& a, const std::pair<float, Uint32>& b) { return a.first < b.first; });
    for (const auto& [key, index] : state.order) {
        const Particle& particle = particles[index];
        ParticleCard card;
        card.position_size = glm::vec4(particle.position + shift, particle.size);
        card.velocity_rotation = glm::vec4(particle.velocity, particle.rotation);
        card.color = ToVec4(particle.color);
        card.frame = glm::vec4(particle.frame, 0.0f, 0.0f, 0.0f);
        state.cards.push_back(card);
    }
}

} // namespace poligon

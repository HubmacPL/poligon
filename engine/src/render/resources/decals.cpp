// Decals: recording (Renderer::DrawDecal) and the decal pipeline. Drawn onto the scene by
// passes/decal_pass.cpp.

#include "../renderer/renderer_internal.h"

#include "poligon_shaders/decal_frag_dxil.h"
#include "poligon_shaders/decal_frag_spirv.h"
#include "poligon_shaders/decal_vert_dxil.h"
#include "poligon_shaders/decal_vert_spirv.h"

namespace poligon {
namespace render_internal {

bool SameDecalMaterial(const DecalMaterial& a, const DecalMaterial& b) {
    return a.texture.id == b.texture.id && a.normal_texture.id == b.normal_texture.id && a.color.r == b.color.r &&
           a.color.g == b.color.g && a.color.b == b.color.b && a.color.a == b.color.a && a.metallic == b.metallic &&
           a.roughness == b.roughness && a.opacity == b.opacity && a.edge_fade == b.edge_fade &&
           a.max_angle_degrees == b.max_angle_degrees;
}

} // namespace render_internal

void Renderer::Impl::CreateDecalPipeline() {
    SDL_GPUShader* vertex = CreateShader(SDL_GPU_SHADERSTAGE_VERTEX, 0, 1, kDecalVertDxil, sizeof(kDecalVertDxil), kDecalVertSpirv,
                                         sizeof(kDecalVertSpirv), 1);
    SDL_GPUShader* fragment = CreateShader(SDL_GPU_SHADERSTAGE_FRAGMENT, 8, 2, kDecalFragDxil, sizeof(kDecalFragDxil), kDecalFragSpirv,
                                           sizeof(kDecalFragSpirv));
    // The box's back faces only: each pixel inside it is visited once, also from inside.
    decals.pipeline = CreateFullscreenPipeline(vertex, fragment, kSceneFormat, FullscreenBlend::Mix, SDL_GPU_CULLMODE_FRONT);
    SDL_ReleaseGPUShader(device, vertex);
    SDL_ReleaseGPUShader(device, fragment);
}

void Renderer::Impl::ReleaseDecals() {
    SDL_ReleaseGPUGraphicsPipeline(device, decals.pipeline);
    ReleaseDynamicBuffer(device, decals.buffer);
}

void Renderer::DrawDecal(const glm::mat4& transform, const DecalMaterial& material) {
    if (!impl_->in_frame) {
        FatalError("Renderer::DrawDecal: call BeginFrame first");
    }
    for (const TextureHandle texture : {material.texture, material.normal_texture}) {
        if (texture.id > impl_->textures.size() || (texture.id != 0 && impl_->textures[texture.id - 1] == nullptr)) {
            FatalError("Renderer::DrawDecal: invalid or destroyed texture handle");
        }
    }
    impl_->decals.commands.push_back({transform, material});
}

} // namespace poligon

// Render targets: pictures drawn by a second camera (Renderer::CreateRenderTarget). Each has a
// view of its own (ViewState: its images and histories), swapped in for its frames.

#include "renderer_internal.h"

namespace poligon {

void Renderer::Impl::SwapView(ViewState& view) {
    std::swap(config.width, view.width);
    std::swap(config.height, view.height);
    std::swap(render_width, view.render_width);
    std::swap(render_height, view.render_height);
    std::swap(scene_color, view.scene_color);
    std::swap(scene_depth, view.scene_depth);
    std::swap(scene_resolved, view.scene_resolved);
    std::swap(output_target, view.output_target);
    std::swap(prepass_depth, view.prepass_depth);
    std::swap(ao_target, view.ao_target);
    std::swap(ao_scratch, view.ao_scratch);
    std::swap(fog_target, view.fog_target);
    std::swap(fog_width, view.fog_width);
    std::swap(fog_height, view.fog_height);
    std::swap(bloom_levels, view.bloom_levels);
    std::swap(luminance_targets, view.luminance_targets);
    std::swap(luminance_index, view.luminance_index);
    std::swap(has_luminance_history, view.has_luminance_history);
    std::swap(water_scene_copy, view.water_scene_copy);
    std::swap(water_depth_copy, view.water_depth_copy);
    std::swap(cloud_target, view.cloud_target);
    std::swap(cloud_history, view.cloud_history);
    std::swap(cloud_history_index, view.cloud_history_index);
    std::swap(cloud_history_valid, view.cloud_history_valid);
    std::swap(cloud_history_frame, view.cloud_history_frame);
    std::swap(previous_cloud_view_projection, view.previous_cloud_view_projection);
    std::swap(taa_targets, view.taa_targets);
    std::swap(motion_target, view.motion_target);
    std::swap(taa_index, view.taa_index);
    std::swap(has_taa_history, view.has_taa_history);
    std::swap(previous_view_projection, view.previous_view_projection);
    std::swap(previous_camera_position, view.previous_camera_position);
    std::swap(frame_index, view.frame_index);
    std::swap(drawn_objects, view.drawn_objects);
    std::swap(previous_drawn_objects, view.previous_drawn_objects);
    std::swap(previous_joint_palette, view.previous_joint_palette);
    std::swap(previous_wind, view.previous_wind);
    std::swap(has_previous_wind, view.has_previous_wind);
}

void Renderer::Impl::CreateViewTargets() {
    CreateSizedTargets();
    const SDL_GPUTextureUsageFlags drawn_and_read = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    for (SDL_GPUTexture*& target : luminance_targets) {
        target = CreateTarget(kLuminanceFormat, drawn_and_read, 1, 1, SDL_GPU_SAMPLECOUNT_1, nullptr, "luminance image");
    }
    has_luminance_history = false;
    has_taa_history = false;
    cloud_history_valid = false;
    has_previous_wind = false;
    drawn_objects.clear();
    previous_drawn_objects.clear();
}

void Renderer::Impl::ReleaseViewTargets() {
    ReleaseSizedTargets();
    for (SDL_GPUTexture*& target : luminance_targets) {
        if (target != nullptr) {
            ReleaseTrackedTexture(device, target);
            target = nullptr;
        }
    }
}

namespace {

RenderTargetData& FindTarget(std::vector<RenderTargetData>& targets, RenderTargetHandle handle, const char* who) {
    if (handle.id == 0 || handle.id > targets.size() || !targets[handle.id - 1].alive) {
        FatalError((std::string(who) + ": invalid or destroyed render target handle").c_str());
    }
    return targets[handle.id - 1];
}

} // namespace

RenderTargetHandle Renderer::CreateRenderTarget(int width, int height) {
    if (impl_->in_frame) {
        FatalError("Renderer::CreateRenderTarget: do not create render targets between BeginFrame and EndFrame");
    }
    if (width < 16 || height < 16 || width > 8192 || height > 8192) {
        FatalError("Renderer::CreateRenderTarget: the size must be from 16 to 8192 pixels each way");
    }
    Impl& impl = *impl_;
    RenderTargetData target;
    target.alive = true;
    target.view.width = width;
    target.view.height = height;
    impl.SwapView(target.view);
    impl.CreateViewTargets();
    impl.SwapView(target.view);
    // The picture is a texture like any other.
    impl.textures.push_back(target.view.output_target);
    impl.texture_layers.push_back(0);
    impl.texture_sizes.push_back(glm::vec2(static_cast<float>(width), static_cast<float>(height)));
    target.texture.id = static_cast<std::uint32_t>(impl.textures.size());
    impl.render_targets.push_back(std::move(target));
    RenderTargetHandle handle;
    handle.id = static_cast<std::uint32_t>(impl.render_targets.size());
    return handle;
}

void Renderer::ResizeRenderTarget(RenderTargetHandle handle, int width, int height) {
    if (impl_->in_frame) {
        FatalError("Renderer::ResizeRenderTarget: do not resize render targets between BeginFrame and EndFrame");
    }
    if (width < 16 || height < 16 || width > 8192 || height > 8192) {
        FatalError("Renderer::ResizeRenderTarget: the size must be from 16 to 8192 pixels each way");
    }
    Impl& impl = *impl_;
    RenderTargetData& target = FindTarget(impl.render_targets, handle, "Renderer::ResizeRenderTarget");
    if (width == target.view.width && height == target.view.height) {
        return;
    }
    impl.SwapView(target.view);
    impl.ReleaseViewTargets();
    impl.config.width = width;
    impl.config.height = height;
    impl.CreateViewTargets();
    impl.SwapView(target.view);
    impl.textures[target.texture.id - 1] = target.view.output_target;
    impl.texture_sizes[target.texture.id - 1] = glm::vec2(static_cast<float>(width), static_cast<float>(height));
}

void Renderer::DestroyRenderTarget(RenderTargetHandle handle) {
    if (impl_->in_frame) {
        FatalError("Renderer::DestroyRenderTarget: do not destroy render targets between BeginFrame and EndFrame");
    }
    Impl& impl = *impl_;
    RenderTargetData& target = FindTarget(impl.render_targets, handle, "Renderer::DestroyRenderTarget");
    impl.SwapView(target.view);
    impl.ReleaseViewTargets();
    impl.SwapView(target.view);
    impl.textures[target.texture.id - 1] = nullptr;
    target.alive = false;
}

TextureHandle Renderer::GetRenderTargetTexture(RenderTargetHandle handle) const {
    if (handle.id == 0 || handle.id > impl_->render_targets.size() || !impl_->render_targets[handle.id - 1].alive) {
        FatalError("Renderer::GetRenderTargetTexture: invalid or destroyed render target handle");
    }
    return impl_->render_targets[handle.id - 1].texture;
}

void Renderer::BeginFrame(const Camera& camera, const DirectionalLight& light, const Environment& environment,
                          RenderTargetHandle target) {
    if (target.id != 0) {
        if (impl_->in_frame) {
            FatalError("Renderer::BeginFrame: the previous frame was not finished with EndFrame");
        }
        RenderTargetData& data = FindTarget(impl_->render_targets, target, "Renderer::BeginFrame");
        impl_->SwapView(data.view);
        impl_->active_target = target.id;
    }
    BeginFrame(camera, light, environment);
}

} // namespace poligon

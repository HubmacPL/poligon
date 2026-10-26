// Point and spot lights: recording (Renderer::DrawLight), keeping the nearest kMaxLights,
// assigning them to screen tiles on the processor and the uniforms every lit fragment shader
// reads (LightUniforms in lighting.hlsli). Spot light shadows are drawn by
// passes/spot_shadow_pass.cpp.
//
// Tiles on the processor rather than in a compute shader: with at most 64 lights the work is
// a few thousand tile marks per frame (well under 0.1 ms), and the result fits in one uniform
// buffer with the lights themselves, so no shader needs another texture or a storage buffer.

#include <algorithm>
#include <cmath>

#include "../renderer/renderer_internal.h"

namespace poligon {

namespace {

void CheckLight(const glm::vec3& color_value, float range, const char* who) {
    if (!(range > 0.0f) || !std::isfinite(color_value.x + color_value.y + color_value.z)) {
        FatalError((std::string(who) + ": the range must be above 0 and the color finite").c_str());
    }
}

} // namespace

void Renderer::DrawLight(const PointLight& light) {
    if (!impl_->in_frame) {
        FatalError("Renderer::DrawLight: call BeginFrame first");
    }
    LightRecord record;
    record.position = glm::vec3(light.position - impl_->origin);
    record.color = glm::vec3(light.color.r, light.color.g, light.color.b) * std::max(light.intensity, 0.0f);
    record.range = light.range;
    CheckLight(record.color, record.range, "Renderer::DrawLight");
    impl_->lights.records.push_back(record);
}

void Renderer::DrawLight(const SpotLight& light) {
    if (!impl_->in_frame) {
        FatalError("Renderer::DrawLight: call BeginFrame first");
    }
    const float length = glm::length(light.direction);
    if (!(length > 0.0f)) {
        FatalError("Renderer::DrawLight: a spot light needs a direction");
    }
    const float outer = glm::clamp(light.outer_angle_degrees, 0.5f, 89.0f);
    const float inner = glm::clamp(light.inner_angle_degrees, 0.0f, outer);
    LightRecord record;
    record.position = glm::vec3(light.position - impl_->origin);
    record.direction = light.direction / length;
    record.color = glm::vec3(light.color.r, light.color.g, light.color.b) * std::max(light.intensity, 0.0f);
    record.range = light.range;
    record.cos_inner = std::cos(glm::radians(inner));
    record.cos_outer = std::cos(glm::radians(outer));
    record.spot = true;
    record.shadows = light.shadows;
    CheckLight(record.color, record.range, "Renderer::DrawLight");
    impl_->lights.records.push_back(record);
}

void Renderer::Impl::PrepareLights(const glm::mat4& view_projection) {
    LightUniforms& uniforms = lights.uniforms;
    LightData& data = lights.data;
    // Lights whose reach touches the view, nearest first (the volumetric fog takes the first
    // four); beyond kMaxLights the farthest are left out. A stable sort keeps ties in the
    // order they were drawn, so the result is the same on every run.
    const ViewVolume volume = MakeViewVolume(view_projection, true);
    std::vector<std::pair<float, std::uint32_t>> order;
    for (std::uint32_t i = 0; i < lights.records.size(); ++i) {
        const LightRecord& record = lights.records[i];
        if (!IsSphereOutside(volume, record.position, record.range)) {
            order.emplace_back(glm::length(record.position), i);
        }
    }
    std::stable_sort(order.begin(), order.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    const int count = std::min(static_cast<int>(order.size()), kMaxLights);
    stats.lights_drawn = count;
    stats.lights_skipped = static_cast<int>(lights.records.size()) - count;

    std::fill(std::begin(data.tiles), std::end(data.tiles), glm::uvec4(0u));
    lights.shadowed_count = 0;
    for (int index = 0; index < count; ++index) {
        const LightRecord& record = lights.records[order[static_cast<size_t>(index)].second];
        GpuLight& gpu_light = data.lights[index];
        gpu_light.position_range = glm::vec4(record.position, record.range);
        gpu_light.color_spot = glm::vec4(record.color, record.spot ? 1.0f : 0.0f);
        gpu_light.direction_outer = glm::vec4(record.direction, record.cos_outer);
        gpu_light.inner_shadow = glm::vec4(record.cos_inner, -1.0f, 0.0f, 0.0f);
        if (record.spot && record.shadows && lights.shadowed_count < kMaxShadowedSpotLights) {
            // Its own camera: from the light along its axis, the cone's full width, out to its range.
            const int tile = lights.shadowed_count++;
            lights.shadowed_lights[tile] = index;
            const glm::vec3 up = std::fabs(record.direction.y) > 0.99f ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
            const float field = 2.0f * std::acos(glm::clamp(record.cos_outer, -1.0f, 1.0f)) + glm::radians(2.0f);
            const glm::mat4 projection = glm::perspective(field, 1.0f, std::max(record.range * 0.002f, 0.02f), record.range);
            lights.spot_view_projection[tile] = projection * glm::lookAt(record.position, record.position + record.direction, up);
            // Clip coordinates to this light's tile in the 2 x 2 image (y down). Column 3 is
            // multiplied by w, so after the shader divides by w the offset stays as it is.
            glm::mat4 to_tile(1.0f);
            to_tile[0][0] = 0.25f;
            to_tile[1][1] = -0.25f;
            to_tile[3][0] = 0.25f + static_cast<float>(tile % 2) * 0.5f;
            to_tile[3][1] = 0.25f + static_cast<float>(tile / 2) * 0.5f;
            uniforms.spot_shadow_matrices[tile] = to_tile * lights.spot_view_projection[tile];
            gpu_light.inner_shadow.y = static_cast<float>(tile);
        }

        // The tiles its reach covers on screen: the box around its sphere, projected; the
        // whole screen when the camera is inside the box or the box crosses the camera's plane.
        int x0 = 0;
        int y0 = 0;
        int x1 = kLightTilesX - 1;
        int y1 = kLightTilesY - 1;
        glm::vec2 low(1e9f);
        glm::vec2 high(-1e9f);
        bool whole = false;
        for (int corner = 0; corner < 8 && !whole; ++corner) {
            const glm::vec3 offset((corner & 1) ? record.range : -record.range, (corner & 2) ? record.range : -record.range,
                                   (corner & 4) ? record.range : -record.range);
            const glm::vec4 clip = view_projection * glm::vec4(record.position + offset, 1.0f);
            if (clip.w <= 0.001f) {
                whole = true;
                break;
            }
            const glm::vec2 ndc = glm::vec2(clip) / clip.w;
            low = glm::min(low, ndc);
            high = glm::max(high, ndc);
        }
        if (!whole) {
            const auto tile_of = [](float value, int tiles) {
                return glm::clamp(static_cast<int>(std::floor(value * static_cast<float>(tiles))), 0, tiles - 1);
            };
            // Normalized coordinates (y up) to tiles (y down).
            x0 = tile_of(low.x * 0.5f + 0.5f, kLightTilesX);
            x1 = tile_of(high.x * 0.5f + 0.5f, kLightTilesX);
            y0 = tile_of(0.5f - high.y * 0.5f, kLightTilesY);
            y1 = tile_of(0.5f - low.y * 0.5f, kLightTilesY);
        }
        const std::uint32_t bit = 1u << (index & 31);
        const int half = index >> 5;
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                const int tile = y * kLightTilesX + x;
                data.tiles[tile >> 1][(tile & 1) * 2 + half] |= bit;
            }
        }
    }
    uniforms.info = glm::vec4(static_cast<float>(count), static_cast<float>(kLightTilesX), static_cast<float>(kLightTilesY), 0.0f);
}

void Renderer::Impl::UploadLights(SDL_GPUCommandBuffer* commands) {
    UploadDynamicBuffer(device, commands, &lights.data, sizeof(lights.data), lights.buffer);
}

void Renderer::Impl::PushLightUniforms(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, float width, float height) {
    lights.uniforms.pixel_to_tile = glm::vec4(static_cast<float>(kLightTilesX) / width, static_cast<float>(kLightTilesY) / height, 0.0f, 0.0f);
    SDL_PushGPUFragmentUniformData(commands, 3, &lights.uniforms, sizeof(lights.uniforms));
    SDL_BindGPUFragmentStorageBuffers(pass, 0, &lights.buffer.buffer, 1);
}

void Renderer::Impl::ReleaseLights() {
    if (lights.spot_shadow_atlas != nullptr) {
        ReleaseTrackedTexture(device, lights.spot_shadow_atlas);
    }
    ReleaseDynamicBuffer(device, lights.buffer);
}

} // namespace poligon

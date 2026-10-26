// Occlusion culling by the depth of earlier frames (Environment::occlusion_culling): the
// shrunk depth of the frame two submissions back (passes/occlusion_pass.cpp), moved onto this
// frame's camera, and the test of a sphere against it. Instances, meshes and impostors wholly
// behind it are left out of the picture (not of shadows).
//
// Never read in the frame being drawn: the copy read is one the graphics card has finished,
// so the processor waits only if the card is two frames behind. The result depends only on
// earlier frames, so a headless run gives the same frames every time.

#include "renderer_internal.h"

namespace poligon {

void Renderer::Impl::WaitForSubmission(std::uint64_t submission) {
    while (!submitted.empty() && submitted.front().first <= submission) {
        SDL_GPUFence* fence = submitted.front().second;
        if (!SDL_WaitForGPUFences(device, true, &fence, 1)) {
            FatalSdlError("cannot wait for a frame");
        }
        SDL_ReleaseGPUFence(device, fence);
        submitted.pop_front();
    }
}

void Renderer::Impl::PrepareCoverage(const glm::mat4& view_projection) {
    occlusion.ready = false;
    if (!environment.occlusion_culling || active_target != 0 || occlusion.target == nullptr || submission_count < 2) {
        return;
    }
    // The newest copy of a frame at least two submissions back.
    const CoverageCopy* newest = nullptr;
    for (const CoverageCopy& copy : occlusion.copies) {
        if (copy.submission != 0 && copy.submission <= submission_count - 1 && (newest == nullptr || copy.submission > newest->submission)) {
            newest = &copy;
        }
    }
    // None, or the camera has jumped since: nothing is culled this frame.
    if (newest == nullptr || glm::length(newest->origin - origin) > 25.0) {
        return;
    }
    WaitForSubmission(newest->submission);
    const float* depths = static_cast<const float*>(SDL_MapGPUTransferBuffer(device, newest->buffer, false));
    if (depths == nullptr) {
        FatalSdlError("cannot read the occlusion copy");
    }
    occlusion.scratch.assign(depths, depths + kCoverageWidth * kCoverageHeight);
    SDL_UnmapGPUTransferBuffer(device, newest->buffer);

    // Each block's depth, put back where it was in the world and seen by this frame's camera.
    // Where several land in one pixel the farthest counts; where none land nothing is known.
    std::vector<float>& coverage = occlusion.coverage;
    coverage.assign(static_cast<size_t>(kCoverageWidth * kCoverageHeight), 0.0f);
    const glm::mat4 old_inverse = glm::inverse(newest->view_projection);
    const glm::vec3 shift(newest->origin - origin);
    for (int y = 0; y < kCoverageHeight; ++y) {
        for (int x = 0; x < kCoverageWidth; ++x) {
            const float depth = occlusion.scratch[static_cast<size_t>(y * kCoverageWidth + x)];
            if (depth <= 0.0f) {
                continue;  // the sky: hides nothing
            }
            const glm::vec4 ndc((static_cast<float>(x) + 0.5f) / kCoverageWidth * 2.0f - 1.0f,
                                1.0f - (static_cast<float>(y) + 0.5f) / kCoverageHeight * 2.0f, depth, 1.0f);
            glm::vec4 world = old_inverse * ndc;
            world /= world.w;
            const glm::vec4 clip = view_projection * glm::vec4(glm::vec3(world) + shift, 1.0f);
            if (clip.w <= 0.001f) {
                continue;
            }
            const int nx = static_cast<int>(std::floor((clip.x / clip.w * 0.5f + 0.5f) * kCoverageWidth));
            const int ny = static_cast<int>(std::floor((0.5f - clip.y / clip.w * 0.5f) * kCoverageHeight));
            if (nx < 0 || ny < 0 || nx >= kCoverageWidth || ny >= kCoverageHeight) {
                continue;
            }
            float& cell = coverage[static_cast<size_t>(ny * kCoverageWidth + nx)];
            const float moved = clip.z / clip.w;
            cell = cell == 0.0f ? moved : std::min(cell, moved);
        }
    }
    // Each pixel takes the farthest of its 3 x 3 neighbors (unknown counts as farthest): blocks
    // land a little off where the camera moved, so the edges of what hides are pulled in.
    occlusion.scratch = coverage;
    for (int y = 0; y < kCoverageHeight; ++y) {
        for (int x = 0; x < kCoverageWidth; ++x) {
            float farthest = 1.0f;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    const int sx = glm::clamp(x + dx, 0, kCoverageWidth - 1);
                    const int sy = glm::clamp(y + dy, 0, kCoverageHeight - 1);
                    farthest = std::min(farthest, occlusion.scratch[static_cast<size_t>(sy * kCoverageWidth + sx)]);
                }
            }
            coverage[static_cast<size_t>(y * kCoverageWidth + x)] = farthest;
        }
    }
    occlusion.view_projection = view_projection;
    occlusion.ready = true;
}

bool Renderer::Impl::IsOccluded(const glm::vec3& center, float radius) const {
    const glm::mat4& view_projection = occlusion.view_projection;
    if (!occlusion.ready) {
        return false;
    }
    glm::vec2 low(1e9f);
    glm::vec2 high(-1e9f);
    float nearest = 0.0f;
    for (int corner = 0; corner < 8; ++corner) {
        const glm::vec3 offset((corner & 1) ? radius : -radius, (corner & 2) ? radius : -radius, (corner & 4) ? radius : -radius);
        const glm::vec4 clip = view_projection * glm::vec4(center + offset, 1.0f);
        if (clip.w <= 0.001f) {
            return false;  // reaches behind the camera
        }
        const glm::vec3 ndc = glm::vec3(clip) / clip.w;
        low = glm::min(low, glm::vec2(ndc));
        high = glm::max(high, glm::vec2(ndc));
        nearest = std::max(nearest, ndc.z);  // reversed depth: larger is nearer
    }
    const int x0 = glm::clamp(static_cast<int>(std::floor((low.x * 0.5f + 0.5f) * kCoverageWidth)), 0, kCoverageWidth - 1);
    const int x1 = glm::clamp(static_cast<int>(std::floor((high.x * 0.5f + 0.5f) * kCoverageWidth)), 0, kCoverageWidth - 1);
    const int y0 = glm::clamp(static_cast<int>(std::floor((0.5f - high.y * 0.5f) * kCoverageHeight)), 0, kCoverageHeight - 1);
    const int y1 = glm::clamp(static_cast<int>(std::floor((0.5f - low.y * 0.5f) * kCoverageHeight)), 0, kCoverageHeight - 1);
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            if (occlusion.coverage[static_cast<size_t>(y * kCoverageWidth + x)] <= nearest) {
                return false;
            }
        }
    }
    return true;
}

void Renderer::Impl::ReleaseOcclusion() {
    if (occlusion.target != nullptr) {
        ReleaseTrackedTexture(device, occlusion.target);
        SDL_ReleaseGPUGraphicsPipeline(device, occlusion.pipeline);
    }
    for (CoverageCopy& copy : occlusion.copies) {
        if (copy.buffer != nullptr) {
            SDL_ReleaseGPUTransferBuffer(device, copy.buffer);
        }
    }
}

} // namespace poligon

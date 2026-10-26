#pragma once

// Occlusion culling by the depth of earlier frames (Environment::occlusion_culling):
// passes/occlusion_pass.cpp shrinks the depth pass's image to kCoverageWidth x kCoverageHeight
// (each pixel the farthest depth of its block) and copies it to a buffer the processor can
// read; renderer/occlusion.cpp reads the copy of the frame two submissions back (never the
// frame being drawn), moves it onto this frame's camera and tests boxes and spheres against it.

#include <SDL3/SDL_gpu.h>

#include <cstdint>
#include <vector>

#include "poligon/core/math.h"

namespace poligon {
namespace render_internal {

constexpr int kCoverageWidth = 128;
constexpr int kCoverageHeight = 72;
// Copies kept in flight: the frame read is this many submissions older than the one drawn.
constexpr int kCoverageCopies = 3;

struct CoverageCopy {
    SDL_GPUTransferBuffer* buffer = nullptr;
    std::uint64_t submission = 0;  // the submission that fills it; 0 = empty
    glm::mat4 view_projection{1.0f};  // that frame's camera (unshifted), relative to `origin`
    glm::dvec3 origin{0.0};
};

struct OcclusionState {
    SDL_GPUTexture* target = nullptr;  // the shrunk depth (R32 float)
    SDL_GPUGraphicsPipeline* pipeline = nullptr;
    CoverageCopy copies[kCoverageCopies];
    // The coverage for this frame's camera: per pixel of the kCoverageWidth x kCoverageHeight
    // grid, the depth (reversed: 1 near, 0 far) nearer than which something surely stands;
    // 0 where nothing is known. Valid only when `ready`.
    std::vector<float> coverage;
    bool ready = false;
    glm::mat4 view_projection{1.0f};  // the camera it was moved onto (unshifted)
    std::vector<float> scratch;  // the copy as read
};

} // namespace render_internal
} // namespace poligon

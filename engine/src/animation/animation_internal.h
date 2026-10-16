#pragma once

// Private to the animation libraries: what poligon_animation (playing, archives) and
// poligon_animation_build (building sets from clips) share.

#include <string>
#include <vector>

#include <ozz/animation/runtime/animation.h>
#include <ozz/animation/runtime/skeleton.h>
#include <ozz/base/memory/unique_ptr.h>

#include "poligon/animation/animation.h"

namespace poligon {

struct AnimationSet::Impl {
    ozz::unique_ptr<ozz::animation::Skeleton> skeleton;
    std::vector<ozz::unique_ptr<ozz::animation::Animation>> clips;
    std::vector<std::string> clip_names;
    std::vector<glm::mat4> inverse_binds;
    // Used by players before their first Play.
    std::vector<glm::mat4> rest_matrices;
};

} // namespace poligon

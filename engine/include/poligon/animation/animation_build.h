#pragma once

#include <memory>
#include <vector>

#include "poligon/animation/animation.h"

namespace poligon {

// Builds an AnimationSet from a skeleton and its clips (as LoadModelFromFile gives them),
// with ozz-animation's offline tools. Takes a moment; build once per model and share it. A
// clip may leave joints out; those stay in their rest pose.
//
// Needs the library poligon_animation_build (a tool-side part, linked by poligon_engine): the
// asset cooker builds sets this way and stores them as archives, so a game that only loads
// cooked models gets its sets from AnimationSet::LoadArchive and can do without it.
std::unique_ptr<AnimationSet> BuildAnimationSet(const Skeleton& skeleton, const std::vector<AnimationClip>& clips);

} // namespace poligon

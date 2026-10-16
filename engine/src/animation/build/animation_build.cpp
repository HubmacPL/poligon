// Building an AnimationSet from clips with ozz-animation's offline tools (the library
// poligon_animation_build): the asset cooker, tools and examples that read model files use
// it; a game that loads cooked models does not need it.
#include "poligon/animation/animation_build.h"

#include <string>

#include <ozz/animation/offline/animation_builder.h>
#include <ozz/animation/offline/raw_animation.h>
#include <ozz/animation/offline/raw_skeleton.h>
#include <ozz/animation/offline/skeleton_builder.h>

#include "poligon/core/fatal_error.h"
#include "../animation_internal.h"

namespace poligon {

namespace {

ozz::math::Float3 ToOzz(const glm::vec3& v) {
    return ozz::math::Float3(v.x, v.y, v.z);
}

ozz::math::Quaternion ToOzz(const glm::quat& q) {
    return ozz::math::Quaternion(q.x, q.y, q.z, q.w);
}

// Our joints are a flat list with parents first; ozz wants a tree. Children are added in
// list order, which makes ozz number the joints exactly as we do (both depth-first).
void AddChildren(const Skeleton& skeleton, int parent, ozz::animation::offline::RawSkeleton::Joint::Children& children) {
    for (size_t i = 0; i < skeleton.joints.size(); ++i) {
        const Joint& joint = skeleton.joints[i];
        if (joint.parent != parent) {
            continue;
        }
        ozz::animation::offline::RawSkeleton::Joint raw;
        // The index goes into the name, so that the numbering can be verified afterwards
        // even when a file gives two joints the same name.
        raw.name = (std::to_string(i) + ":" + joint.name).c_str();
        raw.transform.translation = ToOzz(joint.rest.translation);
        raw.transform.rotation = ToOzz(joint.rest.rotation);
        raw.transform.scale = ToOzz(joint.rest.scale);
        AddChildren(skeleton, static_cast<int>(i), raw.children);
        children.push_back(raw);
    }
}

// Key times must rise and stay within the clip; files are not always that tidy.
float TidyTime(float time, float duration, float previous) {
    time = time < 0.0f ? 0.0f : time > duration ? duration : time;
    return time < previous ? previous : time;
}

} // namespace

std::unique_ptr<AnimationSet> BuildAnimationSet(const Skeleton& skeleton, const std::vector<AnimationClip>& clips) {
    std::unique_ptr<AnimationSet> set(new AnimationSet());
    AnimationSet::Impl& impl = *set->impl_;
    if (skeleton.joints.empty()) {
        FatalError("BuildAnimationSet: the skeleton has no joints");
    }
    const size_t joint_count = skeleton.joints.size();

    ozz::animation::offline::RawSkeleton raw_skeleton;
    AddChildren(skeleton, -1, raw_skeleton.roots);
    if (!raw_skeleton.Validate() || raw_skeleton.num_joints() != static_cast<int>(joint_count)) {
        FatalError("BuildAnimationSet: the skeleton is not a valid tree (a joint's parent must come before it)");
    }
    impl.skeleton = ozz::animation::offline::SkeletonBuilder()(raw_skeleton);
    if (!impl.skeleton) {
        FatalError("BuildAnimationSet: cannot build the skeleton");
    }
    for (size_t i = 0; i < joint_count; ++i) {
        const std::string expected = std::to_string(i) + ":" + skeleton.joints[i].name;
        if (expected != impl.skeleton->joint_names()[i]) {
            FatalError("BuildAnimationSet: joints came out in a different order than in the skeleton");
        }
        impl.inverse_binds.push_back(skeleton.joints[i].inverse_bind);
    }
    impl.rest_matrices = ComputeJointMatrices(skeleton, GetRestPose(skeleton));

    for (const AnimationClip& clip : clips) {
        ozz::animation::offline::RawAnimation raw;
        raw.name = clip.name.c_str();
        // ozz needs a length above zero; a clip of a single pose gets a short one.
        raw.duration = clip.duration_seconds > 0.0f ? clip.duration_seconds : 1.0f / 30.0f;
        raw.tracks.resize(joint_count);
        for (const AnimationChannel& channel : clip.channels) {
            if (channel.joint < 0 || channel.joint >= static_cast<int>(joint_count)) {
                FatalError("BuildAnimationSet: a clip moves a joint the skeleton does not have");
            }
            ozz::animation::offline::RawAnimation::JointTrack& track = raw.tracks[static_cast<size_t>(channel.joint)];
            float previous = 0.0f;
            for (size_t k = 0; k < channel.translations.size(); ++k) {
                previous = TidyTime(channel.translation_times[k], raw.duration, previous);
                track.translations.push_back({previous, ToOzz(channel.translations[k])});
            }
            previous = 0.0f;
            for (size_t k = 0; k < channel.rotations.size(); ++k) {
                previous = TidyTime(channel.rotation_times[k], raw.duration, previous);
                track.rotations.push_back({previous, ToOzz(glm::normalize(channel.rotations[k]))});
            }
            previous = 0.0f;
            for (size_t k = 0; k < channel.scales.size(); ++k) {
                previous = TidyTime(channel.scale_times[k], raw.duration, previous);
                track.scales.push_back({previous, ToOzz(channel.scales[k])});
            }
        }
        // A part without keys means "no movement" to ozz, which is not the rest pose; give
        // every such part one key holding the joint's rest value.
        for (size_t i = 0; i < joint_count; ++i) {
            ozz::animation::offline::RawAnimation::JointTrack& track = raw.tracks[i];
            const JointTransform& rest = skeleton.joints[i].rest;
            if (track.translations.empty()) {
                track.translations.push_back({0.0f, ToOzz(rest.translation)});
            }
            if (track.rotations.empty()) {
                track.rotations.push_back({0.0f, ToOzz(rest.rotation)});
            }
            if (track.scales.empty()) {
                track.scales.push_back({0.0f, ToOzz(rest.scale)});
            }
        }
        if (!raw.Validate()) {
            const std::string message = "BuildAnimationSet: clip \"" + clip.name + "\" has keys ozz cannot use";
            FatalError(message.c_str());
        }
        ozz::unique_ptr<ozz::animation::Animation> built = ozz::animation::offline::AnimationBuilder()(raw);
        if (!built) {
            const std::string message = "BuildAnimationSet: cannot build clip \"" + clip.name + "\"";
            FatalError(message.c_str());
        }
        impl.clips.push_back(std::move(built));
        impl.clip_names.push_back(clip.name);
    }
    return set;
}

} // namespace poligon

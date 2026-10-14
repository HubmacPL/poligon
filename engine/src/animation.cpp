#include "poligon/animation.h"

#include <cmath>
#include <cstring>

#include <ozz/animation/offline/animation_builder.h>
#include <ozz/animation/offline/raw_animation.h>
#include <ozz/animation/offline/raw_skeleton.h>
#include <ozz/animation/offline/skeleton_builder.h>
#include <ozz/animation/runtime/animation.h>
#include <ozz/animation/runtime/blending_job.h>
#include <ozz/animation/runtime/local_to_model_job.h>
#include <ozz/animation/runtime/sampling_job.h>
#include <ozz/animation/runtime/skeleton.h>
#include <ozz/base/maths/simd_math.h>
#include <ozz/base/maths/soa_transform.h>
#include <ozz/base/memory/unique_ptr.h>

#include "poligon/fatal_error.h"

namespace poligon {

glm::mat4 JointTransform::ToMatrix() const {
    glm::mat4 matrix = glm::mat4_cast(rotation);
    matrix[0] *= scale.x;
    matrix[1] *= scale.y;
    matrix[2] *= scale.z;
    matrix[3] = glm::vec4(translation, 1.0f);
    return matrix;
}

int Skeleton::FindJoint(const char* name) const {
    for (size_t i = 0; i < joints.size(); ++i) {
        if (joints[i].name == name) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

std::vector<JointTransform> GetRestPose(const Skeleton& skeleton) {
    std::vector<JointTransform> pose;
    pose.reserve(skeleton.joints.size());
    for (const Joint& joint : skeleton.joints) {
        pose.push_back(joint.rest);
    }
    return pose;
}

std::vector<glm::mat4> ComputeJointMatrices(const Skeleton& skeleton, const std::vector<JointTransform>& pose) {
    if (pose.size() != skeleton.joints.size()) {
        FatalError("ComputeJointMatrices: the pose must have one entry per joint of the skeleton");
    }
    // First where each joint is in the model's space (parents come before children, so one
    // pass is enough), then combined with the joint's inverse bind matrix.
    std::vector<glm::mat4> matrices(pose.size());
    for (size_t i = 0; i < pose.size(); ++i) {
        const glm::mat4 local = pose[i].ToMatrix();
        const int parent = skeleton.joints[i].parent;
        matrices[i] = parent >= 0 ? matrices[static_cast<size_t>(parent)] * local : local;
    }
    for (size_t i = 0; i < pose.size(); ++i) {
        matrices[i] = matrices[i] * skeleton.joints[i].inverse_bind;
    }
    return matrices;
}

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

struct AnimationSet::Impl {
    ozz::unique_ptr<ozz::animation::Skeleton> skeleton;
    std::vector<ozz::unique_ptr<ozz::animation::Animation>> clips;
    std::vector<std::string> clip_names;
    std::vector<glm::mat4> inverse_binds;
    // Used by players before their first Play.
    std::vector<glm::mat4> rest_matrices;
};

AnimationSet::AnimationSet(const Skeleton& skeleton, const std::vector<AnimationClip>& clips)
    : impl_(std::make_unique<Impl>()) {
    if (skeleton.joints.empty()) {
        FatalError("AnimationSet: the skeleton has no joints");
    }
    const size_t joint_count = skeleton.joints.size();

    ozz::animation::offline::RawSkeleton raw_skeleton;
    AddChildren(skeleton, -1, raw_skeleton.roots);
    if (!raw_skeleton.Validate() || raw_skeleton.num_joints() != static_cast<int>(joint_count)) {
        FatalError("AnimationSet: the skeleton is not a valid tree (a joint's parent must come before it)");
    }
    impl_->skeleton = ozz::animation::offline::SkeletonBuilder()(raw_skeleton);
    if (!impl_->skeleton) {
        FatalError("AnimationSet: cannot build the skeleton");
    }
    for (size_t i = 0; i < joint_count; ++i) {
        const std::string expected = std::to_string(i) + ":" + skeleton.joints[i].name;
        if (expected != impl_->skeleton->joint_names()[i]) {
            FatalError("AnimationSet: joints came out in a different order than in the skeleton");
        }
        impl_->inverse_binds.push_back(skeleton.joints[i].inverse_bind);
    }
    impl_->rest_matrices = ComputeJointMatrices(skeleton, GetRestPose(skeleton));

    for (const AnimationClip& clip : clips) {
        ozz::animation::offline::RawAnimation raw;
        raw.name = clip.name.c_str();
        // ozz needs a length above zero; a clip of a single pose gets a short one.
        raw.duration = clip.duration_seconds > 0.0f ? clip.duration_seconds : 1.0f / 30.0f;
        raw.tracks.resize(joint_count);
        for (const AnimationChannel& channel : clip.channels) {
            if (channel.joint < 0 || channel.joint >= static_cast<int>(joint_count)) {
                FatalError("AnimationSet: a clip moves a joint the skeleton does not have");
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
            const std::string message = "AnimationSet: clip \"" + clip.name + "\" has keys ozz cannot use";
            FatalError(message.c_str());
        }
        ozz::unique_ptr<ozz::animation::Animation> built = ozz::animation::offline::AnimationBuilder()(raw);
        if (!built) {
            const std::string message = "AnimationSet: cannot build clip \"" + clip.name + "\"";
            FatalError(message.c_str());
        }
        impl_->clips.push_back(std::move(built));
        impl_->clip_names.push_back(clip.name);
    }
}

AnimationSet::~AnimationSet() = default;

int AnimationSet::GetJointCount() const {
    return impl_->skeleton->num_joints();
}

int AnimationSet::GetClipCount() const {
    return static_cast<int>(impl_->clips.size());
}

const char* AnimationSet::GetClipName(int clip) const {
    if (clip < 0 || clip >= GetClipCount()) {
        FatalError("AnimationSet::GetClipName: no such clip");
    }
    return impl_->clip_names[static_cast<size_t>(clip)].c_str();
}

float AnimationSet::GetClipDuration(int clip) const {
    if (clip < 0 || clip >= GetClipCount()) {
        FatalError("AnimationSet::GetClipDuration: no such clip");
    }
    return impl_->clips[static_cast<size_t>(clip)]->duration();
}

int AnimationSet::FindClip(const char* name) const {
    for (size_t i = 0; i < impl_->clip_names.size(); ++i) {
        if (impl_->clip_names[i] == name) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

struct AnimationPlayer::Impl {
    // One clip being played: the current one, or the one fading out.
    struct Layer {
        int clip = -1;
        float time = 0.0f;
        float speed = 1.0f;
        bool loop = true;
        std::unique_ptr<ozz::animation::SamplingJob::Context> context;
        std::vector<ozz::math::SoaTransform> locals;
    };

    const AnimationSet::Impl* set = nullptr;
    Layer current;
    Layer previous;
    float fade_seconds = 0.0f;
    float fade_elapsed = 0.0f;
    std::vector<ozz::math::SoaTransform> blended;
    std::vector<ozz::math::Float4x4> models;
    std::vector<glm::mat4> matrices;
    bool matrices_are_current = false;

    void Advance(Layer& layer, float delta_seconds) const {
        if (layer.clip < 0) {
            return;
        }
        const float duration = set->clips[static_cast<size_t>(layer.clip)]->duration();
        layer.time += delta_seconds * layer.speed;
        if (layer.loop) {
            layer.time = std::fmod(layer.time, duration);
            if (layer.time < 0.0f) {
                layer.time += duration;
            }
        } else {
            layer.time = layer.time < 0.0f ? 0.0f : layer.time > duration ? duration : layer.time;
        }
    }

    void Sample(Layer& layer) const {
        const ozz::animation::Animation& animation = *set->clips[static_cast<size_t>(layer.clip)];
        ozz::animation::SamplingJob job;
        job.animation = &animation;
        job.context = layer.context.get();
        job.ratio = layer.time / animation.duration();
        job.output = ozz::make_span(layer.locals);
        if (!job.Run()) {
            FatalError("AnimationPlayer: sampling a clip failed");
        }
    }
};

AnimationPlayer::AnimationPlayer(const AnimationSet& set) : impl_(std::make_unique<Impl>()) {
    impl_->set = set.impl_.get();
    const ozz::animation::Skeleton& skeleton = *impl_->set->skeleton;
    for (Impl::Layer* layer : {&impl_->current, &impl_->previous}) {
        layer->context = std::make_unique<ozz::animation::SamplingJob::Context>(skeleton.num_joints());
        layer->locals.resize(static_cast<size_t>(skeleton.num_soa_joints()));
    }
    impl_->blended.resize(static_cast<size_t>(skeleton.num_soa_joints()));
    impl_->models.resize(static_cast<size_t>(skeleton.num_joints()));
    impl_->matrices = impl_->set->rest_matrices;
    impl_->matrices_are_current = true;
}

AnimationPlayer::~AnimationPlayer() = default;

void AnimationPlayer::Play(int clip, float fade_seconds, bool loop) {
    if (clip < 0 || clip >= static_cast<int>(impl_->set->clips.size())) {
        FatalError("AnimationPlayer::Play: no such clip");
    }
    // What is playing now becomes the clip that fades out. If a fade was still under way,
    // its older clip is dropped; the change shows as a small jump only when clips are
    // switched faster than they fade.
    std::swap(impl_->current, impl_->previous);
    impl_->current.clip = clip;
    impl_->current.time = 0.0f;
    impl_->current.speed = 1.0f;
    impl_->current.loop = loop;
    impl_->current.context->Invalidate();
    impl_->fade_seconds = impl_->previous.clip >= 0 && fade_seconds > 0.0f ? fade_seconds : 0.0f;
    impl_->fade_elapsed = 0.0f;
    impl_->matrices_are_current = false;
}

void AnimationPlayer::SetSpeed(float speed) {
    impl_->current.speed = speed;
}

void AnimationPlayer::Update(float delta_seconds) {
    impl_->Advance(impl_->current, delta_seconds);
    if (impl_->fade_elapsed < impl_->fade_seconds) {
        impl_->Advance(impl_->previous, delta_seconds);
        impl_->fade_elapsed += delta_seconds;
    }
    impl_->matrices_are_current = false;
}

int AnimationPlayer::GetClip() const {
    return impl_->current.clip;
}

float AnimationPlayer::GetTime() const {
    return impl_->current.time;
}

bool AnimationPlayer::IsFinished() const {
    if (impl_->current.clip < 0 || impl_->current.loop) {
        return false;
    }
    return impl_->current.time >= impl_->set->clips[static_cast<size_t>(impl_->current.clip)]->duration();
}

const std::vector<glm::mat4>& AnimationPlayer::GetJointMatrices() {
    Impl& impl = *impl_;
    if (impl.matrices_are_current || impl.current.clip < 0) {
        return impl.matrices;
    }
    const ozz::animation::Skeleton& skeleton = *impl.set->skeleton;

    // Where every joint is relative to its parent: from the current clip alone, or mixed
    // with the clip that is fading out.
    impl.Sample(impl.current);
    const std::vector<ozz::math::SoaTransform>* locals = &impl.current.locals;
    if (impl.fade_elapsed < impl.fade_seconds) {
        impl.Sample(impl.previous);
        const float amount = impl.fade_elapsed / impl.fade_seconds;
        ozz::animation::BlendingJob::Layer layers[2];
        layers[0].transform = ozz::make_span(impl.previous.locals);
        layers[0].weight = 1.0f - amount;
        layers[1].transform = ozz::make_span(impl.current.locals);
        layers[1].weight = amount;
        ozz::animation::BlendingJob blend;
        blend.layers = layers;
        blend.rest_pose = skeleton.joint_rest_poses();
        blend.output = ozz::make_span(impl.blended);
        if (!blend.Run()) {
            FatalError("AnimationPlayer: blending two clips failed");
        }
        locals = &impl.blended;
    }

    // Then where every joint is in the model's space, and from that the matrix that moves
    // a vertex from where it was modeled.
    ozz::animation::LocalToModelJob to_model;
    to_model.skeleton = &skeleton;
    to_model.input = ozz::make_span(*locals);
    to_model.output = ozz::make_span(impl.models);
    if (!to_model.Run()) {
        FatalError("AnimationPlayer: computing joint positions failed");
    }
    for (size_t i = 0; i < impl.models.size(); ++i) {
        glm::mat4 model;
        for (int column = 0; column < 4; ++column) {
            ozz::math::StorePtrU(impl.models[i].cols[column], &model[column].x);
        }
        impl.matrices[i] = model * impl.set->inverse_binds[i];
    }
    impl.matrices_are_current = true;
    return impl.matrices;
}

} // namespace poligon

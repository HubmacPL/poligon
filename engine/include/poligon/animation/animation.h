#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "poligon/core/math.h"

namespace poligon {

// Skeletal animation data.
//
// A skeleton is a tree of joints (bones). Each vertex of a skinned mesh follows up to four
// joints (see SkinVertex in renderer.h); when the joints move, the mesh bends with them.
// An animation clip stores how the joints move over time.

// Where a joint is relative to its parent: first scaled, then rotated, then moved.
struct JointTransform {
    glm::vec3 translation{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};  // w, x, y, z: no rotation
    glm::vec3 scale{1.0f};

    glm::mat4 ToMatrix() const;
};

struct Joint {
    std::string name;
    // Index of the parent joint, -1 for a root. A parent always comes before its children.
    int parent = -1;
    // The joint's place relative to its parent when no animation plays.
    JointTransform rest;
    // Takes a vertex from the mesh's space into this joint's space in the pose the mesh
    // was modeled in ("bind pose"). Identity for joints no vertex follows.
    glm::mat4 inverse_bind{1.0f};
};

struct Skeleton {
    std::vector<Joint> joints;

    // Index of the joint with this name, or -1.
    int FindJoint(const char* name) const;
};

// The movement of one joint in one clip. Each of the three parts has its own list of keys
// (a time in seconds and a value); a part without keys keeps the joint's rest value.
struct AnimationChannel {
    int joint = 0;
    std::vector<float> translation_times;
    std::vector<glm::vec3> translations;
    std::vector<float> rotation_times;
    std::vector<glm::quat> rotations;
    std::vector<float> scale_times;
    std::vector<glm::vec3> scales;
};

struct AnimationClip {
    std::string name;
    float duration_seconds = 0.0f;
    std::vector<AnimationChannel> channels;
};

// A pose is one JointTransform per joint of a skeleton, each relative to the joint's parent.

// The pose in which no animation plays.
std::vector<JointTransform> GetRestPose(const Skeleton& skeleton);

// Turns a pose into the matrices the renderer bends a skinned mesh with: for every joint,
// the matrix that takes a vertex from where it was modeled to where the posed joint puts it.
// `pose` must have one entry per joint. Pass the result to Renderer::DrawSkinnedMesh.
std::vector<glm::mat4> ComputeJointMatrices(const Skeleton& skeleton, const std::vector<JointTransform>& pose);

// A skeleton and its clips prepared for playing. Built once per model (it takes a moment)
// and then shared by every character that uses the model. Playing is done by the
// ozz-animation library; this class and AnimationPlayer are all of it that shows.
class AnimationSet {
public:
    // A clip may leave joints out; those stay in their rest pose.
    AnimationSet(const Skeleton& skeleton, const std::vector<AnimationClip>& clips);
    ~AnimationSet();
    AnimationSet(const AnimationSet&) = delete;
    AnimationSet& operator=(const AnimationSet&) = delete;

    int GetJointCount() const;
    int GetClipCount() const;
    const char* GetClipName(int clip) const;
    float GetClipDuration(int clip) const;
    // Index of the clip with this name, or -1.
    int FindClip(const char* name) const;

    // The skeleton and every clip, built, in ozz-animation's own archive format (its file
    // format for built skeletons and animations). The asset cooker stores this, so that a
    // game loads clips ready to play instead of building them (LoadArchive).
    std::vector<std::uint8_t> SaveArchive() const;
    // A set from SaveArchive's bytes, without building anything. `skeleton` must be the one
    // the archive was made from (the same joints in the same order); it gives the bind
    // matrices. nullptr and `error` when the bytes are not such an archive or do not fit.
    static std::unique_ptr<AnimationSet> LoadArchive(const Skeleton& skeleton, const std::vector<std::uint8_t>& archive,
                                                     std::string& error);

private:
    AnimationSet();
    friend class AnimationPlayer;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Plays the clips of an AnimationSet for one character: keeps the time, fades from one
// clip into the next, and gives the joint matrices for the current moment.
//
// Time only moves in Update, by the amount given, so a run with a fixed time step plays
// the same every time. The AnimationSet must outlive the player.
//
//     poligon::AnimationSet animations(model.skeleton, model.animations);
//     poligon::AnimationPlayer player(animations);
//     player.Play(animations.FindClip("Walk"));
//     // every tick:
//     player.Update(tick.delta_seconds);
//     // every frame:
//     poligon::DrawModel(renderer, render_model, transform, player.GetJointMatrices());
class AnimationPlayer {
public:
    explicit AnimationPlayer(const AnimationSet& set);
    ~AnimationPlayer();
    AnimationPlayer(const AnimationPlayer&) = delete;
    AnimationPlayer& operator=(const AnimationPlayer&) = delete;

    // Starts `clip` from its beginning. What was playing fades out over `fade_seconds`
    // while the new clip fades in (0 switches at once). A looping clip starts over when
    // it ends; a clip that does not loop stops on its last moment.
    void Play(int clip, float fade_seconds = 0.25f, bool loop = true);
    // 1 = as recorded, 2 = twice as fast, 0 = frozen. Applies to the clip that Play started last.
    void SetSpeed(float speed);
    // Moves time forward. Call once per tick with the tick's length.
    void Update(float delta_seconds);

    // The clip started by the last Play, -1 before the first one.
    int GetClip() const;
    // Seconds into that clip.
    float GetTime() const;
    // True when a clip that does not loop has reached its end.
    bool IsFinished() const;

    // One matrix per joint for the current moment, for Renderer::DrawSkinnedMesh and
    // DrawModel. Before the first Play: the rest pose.
    const std::vector<glm::mat4>& GetJointMatrices();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace poligon

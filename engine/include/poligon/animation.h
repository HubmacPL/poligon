#pragma once

#include <string>
#include <vector>

#include "poligon/math.h"

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

} // namespace poligon

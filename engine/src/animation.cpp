#include "poligon/animation.h"

#include <cstring>

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

} // namespace poligon

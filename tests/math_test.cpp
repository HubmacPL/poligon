// Checks that GLM is configured to Poligon's conventions (see poligon/core/math.h).
// These are properties of configuration, not of our code: if one fails, a GLM_FORCE_*
// setting changed or is missing, and rendering would silently break.
#include <cmath>

#include "poligon/core/math.h"
#include "test_support.h"

namespace {

bool Near(float a, float b) {
    return std::fabs(a - b) < 1e-5f;
}

bool NearVec(glm::vec3 a, glm::vec3 b) {
    return Near(a.x, b.x) && Near(a.y, b.y) && Near(a.z, b.z);
}

// Transforms a point (w = 1) and divides by the resulting w.
glm::vec3 TransformPoint(const glm::mat4& matrix, glm::vec3 point) {
    const glm::vec4 result = matrix * glm::vec4(point, 1.0f);
    return glm::vec3(result) / result.w;
}

} // namespace

int main() {
    using test::Expect;
    const glm::mat4 identity(1.0f);

    Expect(NearVec(glm::vec3(), {0, 0, 0}), "a default vector is zero");
    Expect(NearVec(TransformPoint(glm::mat4(), {1, 2, 3}), {1, 2, 3}), "a default matrix is identity");
    {
        const glm::quat rotation = glm::quat();
        Expect(NearVec(rotation * glm::vec3(1, 2, 3), {1, 2, 3}), "a default quaternion does not rotate");
    }
    Expect(sizeof(glm::vec3) == 3 * sizeof(float) && sizeof(glm::mat4) == 16 * sizeof(float),
           "vectors and matrices are plain packed floats");

    Expect(NearVec(glm::cross(glm::vec3(1, 0, 0), glm::vec3(0, 1, 0)), {0, 0, 1}), "X cross Y is Z (right-handed)");

    {
        // (a * b) applies b first: scale, then translate.
        const glm::mat4 combined = glm::translate(identity, {10, 0, 0}) * glm::scale(identity, {2, 2, 2});
        Expect(NearVec(TransformPoint(combined, {1, 0, 0}), {12, 0, 0}), "a * b applies b first");
        // Column-major storage: the translation is in the last column.
        Expect(Near(combined[3][0], 10.0f), "matrices are column-major: m[3] is the translation column");
    }
    {
        const glm::mat4 quarter_turn = glm::rotate(identity, glm::radians(90.0f), {0, 1, 0});
        Expect(NearVec(TransformPoint(quarter_turn, {0, 0, 1}), {1, 0, 0}), "positive Y rotation turns +Z to +X");
    }
    {
        // Camera 5 units up the +Z axis, looking at the origin: the origin is 5 units ahead (-Z in view space).
        const glm::mat4 view = glm::lookAt(glm::vec3(0, 0, 5), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
        Expect(NearVec(TransformPoint(view, {0, 0, 0}), {0, 0, -5}), "look-at puts the target on the -Z axis");
        Expect(NearVec(TransformPoint(view, {1, 0, 0}), {1, 0, -5}), "world +X stays on the right");
    }
    {
        const glm::mat4 projection = glm::perspective(glm::radians(90.0f), 1.0f, 1.0f, 100.0f);
        Expect(Near(TransformPoint(projection, {0, 0, -1}).z, 0.0f), "near plane maps to depth 0");
        Expect(Near(TransformPoint(projection, {0, 0, -100}).z, 1.0f), "far plane maps to depth 1");
        Expect(NearVec(TransformPoint(projection, {1, 1, -1}), {1, 1, 0}),
               "with a 90 degree field of view the frustum edge is at 45 degrees");
    }

    return test::ExitCode();
}

#include <cmath>

#include "poligon/math.h"
#include "test_support.h"

namespace {

bool Near(float a, float b) {
    return std::fabs(a - b) < 1e-5f;
}

// Transforms a point (w = 1) and divides by the resulting w.
poligon::Vec3 TransformPoint(const poligon::Mat4& m, poligon::Vec3 p) {
    const float x = m.m[0] * p.x + m.m[4] * p.y + m.m[8] * p.z + m.m[12];
    const float y = m.m[1] * p.x + m.m[5] * p.y + m.m[9] * p.z + m.m[13];
    const float z = m.m[2] * p.x + m.m[6] * p.y + m.m[10] * p.z + m.m[14];
    const float w = m.m[3] * p.x + m.m[7] * p.y + m.m[11] * p.z + m.m[15];
    return poligon::Vec3{x / w, y / w, z / w};
}

bool NearVec(poligon::Vec3 a, poligon::Vec3 b) {
    return Near(a.x, b.x) && Near(a.y, b.y) && Near(a.z, b.z);
}

} // namespace

int main() {
    using namespace poligon;
    using test::Expect;

    Expect(NearVec(Cross({1, 0, 0}, {0, 1, 0}), {0, 0, 1}), "X cross Y is Z (right-handed)");
    Expect(Near(Length(Normalize({3, 4, 0})), 1.0f), "Normalize gives length 1");
    Expect(NearVec(Normalize({0, 0, 0}), {0, 0, 0}), "Normalize of zero is zero");

    Expect(NearVec(TransformPoint(Mat4{}, {1, 2, 3}), {1, 2, 3}), "default Mat4 is identity");
    Expect(NearVec(TransformPoint(MakeTranslation({1, 2, 3}), {1, 1, 1}), {2, 3, 4}), "translation moves points");
    Expect(NearVec(TransformPoint(MakeScale({2, 3, 4}), {1, 1, 1}), {2, 3, 4}), "scale scales points");

    {
        // Multiply(a, b) applies b first: scale, then translate.
        const Mat4 combined = Multiply(MakeTranslation({10, 0, 0}), MakeScale({2, 2, 2}));
        Expect(NearVec(TransformPoint(combined, {1, 0, 0}), {12, 0, 0}), "Multiply applies the right matrix first");
    }
    {
        const Mat4 quarter_turn = MakeRotationY(DegreesToRadians(90.0f));
        Expect(NearVec(TransformPoint(quarter_turn, {0, 0, 1}), {1, 0, 0}), "positive Y rotation turns +Z to +X");
    }
    {
        // Camera 5 units up the +Z axis, looking at the origin: the origin is 5 units ahead (-Z in view space).
        const Mat4 view = MakeLookAt({0, 0, 5}, {0, 0, 0}, {0, 1, 0});
        Expect(NearVec(TransformPoint(view, {0, 0, 0}), {0, 0, -5}), "look-at puts the target on the -Z axis");
        Expect(NearVec(TransformPoint(view, {1, 0, 0}), {1, 0, -5}), "world +X stays on the right");
    }
    {
        const Mat4 projection = MakePerspective(DegreesToRadians(90.0f), 1.0f, 1.0f, 100.0f);
        Expect(Near(TransformPoint(projection, {0, 0, -1}).z, 0.0f), "near plane maps to depth 0");
        Expect(Near(TransformPoint(projection, {0, 0, -100}).z, 1.0f), "far plane maps to depth 1");
        Expect(NearVec(TransformPoint(projection, {1, 1, -1}), {1, 1, 0}),
               "with a 90 degree field of view the frustum edge is at 45 degrees");
    }

    return test::ExitCode();
}

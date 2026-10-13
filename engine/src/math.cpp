#include "poligon/math.h"

#include <cmath>

namespace poligon {

Vec3 Add(Vec3 a, Vec3 b) {
    return Vec3{a.x + b.x, a.y + b.y, a.z + b.z};
}

Vec3 Subtract(Vec3 a, Vec3 b) {
    return Vec3{a.x - b.x, a.y - b.y, a.z - b.z};
}

Vec3 Scale(Vec3 v, float factor) {
    return Vec3{v.x * factor, v.y * factor, v.z * factor};
}

float Dot(Vec3 a, Vec3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec3 Cross(Vec3 a, Vec3 b) {
    return Vec3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

float Length(Vec3 v) {
    return std::sqrt(Dot(v, v));
}

Vec3 Normalize(Vec3 v) {
    const float length = Length(v);
    if (length == 0.0f) {
        return Vec3{};
    }
    return Scale(v, 1.0f / length);
}

Mat4 Multiply(const Mat4& a, const Mat4& b) {
    Mat4 result;
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) {
                sum += a.m[k * 4 + row] * b.m[column * 4 + k];
            }
            result.m[column * 4 + row] = sum;
        }
    }
    return result;
}

Mat4 MakeTranslation(Vec3 offset) {
    Mat4 result;
    result.m[12] = offset.x;
    result.m[13] = offset.y;
    result.m[14] = offset.z;
    return result;
}

Mat4 MakeScale(Vec3 factors) {
    Mat4 result;
    result.m[0] = factors.x;
    result.m[5] = factors.y;
    result.m[10] = factors.z;
    return result;
}

Mat4 MakeRotationY(float angle_radians) {
    const float c = std::cos(angle_radians);
    const float s = std::sin(angle_radians);
    Mat4 result;
    result.m[0] = c;
    result.m[2] = -s;
    result.m[8] = s;
    result.m[10] = c;
    return result;
}

Mat4 MakeLookAt(Vec3 eye, Vec3 target, Vec3 up) {
    const Vec3 forward = Normalize(Subtract(target, eye));
    const Vec3 right = Normalize(Cross(forward, up));
    const Vec3 camera_up = Cross(right, forward);

    Mat4 result;
    result.m[0] = right.x;
    result.m[4] = right.y;
    result.m[8] = right.z;
    result.m[1] = camera_up.x;
    result.m[5] = camera_up.y;
    result.m[9] = camera_up.z;
    result.m[2] = -forward.x;
    result.m[6] = -forward.y;
    result.m[10] = -forward.z;
    result.m[12] = -Dot(right, eye);
    result.m[13] = -Dot(camera_up, eye);
    result.m[14] = Dot(forward, eye);
    return result;
}

Mat4 MakePerspective(float vertical_fov_radians, float aspect_ratio, float near_plane, float far_plane) {
    const float focal = 1.0f / std::tan(vertical_fov_radians * 0.5f);

    Mat4 result;
    result.m[0] = focal / aspect_ratio;
    result.m[5] = focal;
    result.m[10] = far_plane / (near_plane - far_plane);
    result.m[11] = -1.0f;
    result.m[14] = near_plane * far_plane / (near_plane - far_plane);
    result.m[15] = 0.0f;
    return result;
}

float DegreesToRadians(float degrees) {
    return degrees * 0.017453292519943295f;
}

} // namespace poligon

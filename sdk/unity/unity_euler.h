// Copyright (c) 2026 Jadis0x. All rights reserved.
// Euler <-> quaternion conversion in Unity's convention, done natively.
//
// Transform.localEulerAngles is a managed property, and IL2CPP strips the
// accessors a game never calls - a build that only ever assigns localRotation
// loses set_localEulerAngles entirely, and the lookup then reports
// "no overload matched set_localEulerAngles(UnityEngine.Vector3)". localRotation
// survives, because the engine itself uses it, so the euler value is converted
// here and written through the quaternion property instead.
//
// Unity composes euler angles in Z, X, Y order (q = qY * qX * qZ) and measures
// them in degrees, which is what these two functions reproduce.
#pragma once

#include "unity_types.h"

#include <algorithm>
#include <cmath>

namespace URK::Unity {

inline constexpr float kDegreesToRadians = 0.017453292519943295f;
inline constexpr float kRadiansToDegrees = 57.29577951308232f;

inline Quaternion euler_to_quaternion(Vector3 degrees) {
    const float half_x = degrees.x * kDegreesToRadians * 0.5f;
    const float half_y = degrees.y * kDegreesToRadians * 0.5f;
    const float half_z = degrees.z * kDegreesToRadians * 0.5f;
    const float sx = std::sin(half_x), cx = std::cos(half_x);
    const float sy = std::sin(half_y), cy = std::cos(half_y);
    const float sz = std::sin(half_z), cz = std::cos(half_z);

    Quaternion out{};
    out.x = sx * cy * cz + cx * sy * sz;
    out.y = cx * sy * cz - sx * cy * sz;
    out.z = cx * cy * sz - sx * sy * cz;
    out.w = cx * cy * cz + sx * sy * sz;
    return out;
}

// The inverse, wrapped into [0, 360) the way the Inspector shows it. At gimbal
// lock (the X rotation at +/-90 degrees) Z is pinned to zero and the whole turn
// is reported on Y, which is the branch Unity takes too.
inline Vector3 quaternion_to_euler(Quaternion rotation) {
    const float magnitude = std::sqrt(rotation.x * rotation.x + rotation.y * rotation.y +
                                      rotation.z * rotation.z + rotation.w * rotation.w);
    if (magnitude < 1e-6f)
        return {};
    const float x = rotation.x / magnitude, y = rotation.y / magnitude;
    const float z = rotation.z / magnitude, w = rotation.w / magnitude;

    // Rows of the rotation matrix that Ry * Rx * Rz produces.
    const float m12 = 2.0f * (y * z - w * x);
    const float m10 = 2.0f * (x * y + w * z);
    const float m11 = 1.0f - 2.0f * (x * x + z * z);
    const float m02 = 2.0f * (x * z + w * y);
    const float m22 = 1.0f - 2.0f * (x * x + y * y);
    const float m20 = 2.0f * (x * z - w * y);
    const float m00 = 1.0f - 2.0f * (y * y + z * z);

    const float pitch_sine = std::clamp(-m12, -1.0f, 1.0f);
    Vector3 out{};
    out.x = std::asin(pitch_sine);
    if (std::fabs(pitch_sine) > 0.9999f) {
        out.y = std::atan2(-m20, m00);
        out.z = 0.0f;
    } else {
        out.y = std::atan2(m02, m22);
        out.z = std::atan2(m10, m11);
    }

    const auto wrap = [](float radians) {
        float degrees = radians * kRadiansToDegrees;
        degrees = std::fmod(degrees, 360.0f);
        if (degrees < 0.0f)
            degrees += 360.0f;
        return degrees;
    };
    return {wrap(out.x), wrap(out.y), wrap(out.z)};
}

// Rotating a vector by a quaternion, so a caller can place a local point in
// world space without a managed Transform.TransformPoint call.
inline Vector3 rotate(Quaternion rotation, Vector3 value) {
    const Vector3 axis{rotation.x, rotation.y, rotation.z};
    const float axis_dot_value = Vector3::dot(axis, value);
    const float axis_dot_axis = Vector3::dot(axis, axis);
    const Vector3 cross = Vector3::cross(axis, value);
    return axis * (2.0f * axis_dot_value) + value * (rotation.w * rotation.w - axis_dot_axis) +
           cross * (2.0f * rotation.w);
}

} // namespace URK::Unity

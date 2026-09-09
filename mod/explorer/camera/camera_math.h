// Copyright (c) 2026 Jadis0x. All rights reserved.
// Camera orientation maths, kept free of the managed runtime.
//
// Aiming a camera used to call Transform.LookAt(Vector3). That overload is not
// always in the build: IL2CPP managed stripping keeps only the overloads a game
// actually calls, and a game that never looks at a bare position loses it. The
// same result comes from setting Transform.rotation, and the quaternion for it
// is arithmetic - no managed method to be stripped, one fewer interop call per
// frame, and testable off a running game.
#pragma once

#include "sdk/unity/unity_types.h"

#include <cmath>

namespace Explorer::CameraMath {

inline float dot(URK::Unity::Vector3 left, URK::Unity::Vector3 right) {
    return left.x * right.x + left.y * right.y + left.z * right.z;
}

inline URK::Unity::Vector3 cross(URK::Unity::Vector3 left, URK::Unity::Vector3 right) {
    return {left.y * right.z - left.z * right.y, left.z * right.x - left.x * right.z,
            left.x * right.y - left.y * right.x};
}

inline float length(URK::Unity::Vector3 value) { return std::sqrt(dot(value, value)); }

inline URK::Unity::Vector3 normalized(URK::Unity::Vector3 value) {
    const float magnitude = length(value);
    return magnitude > 1e-6f ? URK::Unity::Vector3{value.x / magnitude, value.y / magnitude, value.z / magnitude}
                             : URK::Unity::Vector3{};
}

// Unity's Quaternion.LookRotation: a left-handed, Y-up basis built from the
// forward direction, turned into the rotation that maps +Z onto it.
// Returns identity when the direction has no length or is parallel to `up`,
// which is what Unity does with a degenerate look direction.
inline URK::Unity::Quaternion look_rotation(URK::Unity::Vector3 direction,
                                            URK::Unity::Vector3 up = URK::Unity::Vector3{0.0f, 1.0f, 0.0f}) {
    const URK::Unity::Vector3 forward = normalized(direction);
    if (length(forward) < 0.5f)
        return {0.0f, 0.0f, 0.0f, 1.0f};
    const URK::Unity::Vector3 right = normalized(cross(up, forward));
    if (length(right) < 0.5f)
        return {0.0f, 0.0f, 0.0f, 1.0f};
    const URK::Unity::Vector3 orthogonal_up = cross(forward, right);

    const float m00 = right.x, m01 = right.y, m02 = right.z;
    const float m10 = orthogonal_up.x, m11 = orthogonal_up.y, m12 = orthogonal_up.z;
    const float m20 = forward.x, m21 = forward.y, m22 = forward.z;

    URK::Unity::Quaternion out{};
    const float trace = m00 + m11 + m22;
    if (trace > 0.0f) {
        float scale = std::sqrt(trace + 1.0f);
        out.w = scale * 0.5f;
        scale = 0.5f / scale;
        out.x = (m12 - m21) * scale;
        out.y = (m20 - m02) * scale;
        out.z = (m01 - m10) * scale;
    } else if (m00 >= m11 && m00 >= m22) {
        const float scale = std::sqrt(1.0f + m00 - m11 - m22);
        const float half = 0.5f / scale;
        out.x = 0.5f * scale;
        out.y = (m01 + m10) * half;
        out.z = (m02 + m20) * half;
        out.w = (m12 - m21) * half;
    } else if (m11 > m22) {
        const float scale = std::sqrt(1.0f + m11 - m00 - m22);
        const float half = 0.5f / scale;
        out.x = (m10 + m01) * half;
        out.y = 0.5f * scale;
        out.z = (m21 + m12) * half;
        out.w = (m20 - m02) * half;
    } else {
        const float scale = std::sqrt(1.0f + m22 - m00 - m11);
        const float half = 0.5f / scale;
        out.x = (m20 + m02) * half;
        out.y = (m21 + m12) * half;
        out.z = 0.5f * scale;
        out.w = (m01 + m10) * half;
    }
    return out;
}

// Where a rotation points, so a test can check a look rotation without knowing
// how quaternions are laid out.
inline URK::Unity::Vector3 rotate(URK::Unity::Quaternion rotation, URK::Unity::Vector3 value) {
    const URK::Unity::Vector3 axis{rotation.x, rotation.y, rotation.z};
    const URK::Unity::Vector3 first = cross(axis, value);
    const URK::Unity::Vector3 second = cross(axis, first);
    return {value.x + 2.0f * (rotation.w * first.x + second.x), value.y + 2.0f * (rotation.w * first.y + second.y),
            value.z + 2.0f * (rotation.w * first.z + second.z)};
}

} // namespace Explorer::CameraMath

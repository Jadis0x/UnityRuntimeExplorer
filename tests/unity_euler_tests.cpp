// Copyright (c) 2026 Jadis0x. All rights reserved.
// Euler <-> quaternion conversion in Unity's ZXY order, which is what a build
// with set_localEulerAngles stripped has to fall back on.
#include "sdk/unity/unity_euler.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {

using URK::Unity::euler_to_quaternion;
using URK::Unity::Quaternion;
using URK::Unity::quaternion_to_euler;
using URK::Unity::Vector3;

void require(bool condition, const char *message) {
    if (condition)
        return;
    std::cerr << "FAILED: " << message << '\n';
    std::exit(1);
}

// Rotating a vector by the quaternion is the property that matters: whatever
// the angles round-trip to, the object must end up pointing the same way.
using URK::Unity::rotate;

float distance(Vector3 left, Vector3 right) {
    const float dx = left.x - right.x, dy = left.y - right.y, dz = left.z - right.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

void require_unit(Quaternion q, const char *message) {
    const float magnitude = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    require(std::fabs(magnitude - 1.0f) < 1e-4f, message);
}

void require_round_trip(Vector3 degrees, const char *message) {
    const Quaternion direct = euler_to_quaternion(degrees);
    require_unit(direct, "euler_to_quaternion must produce a unit quaternion");
    const Vector3 recovered = quaternion_to_euler(direct);
    const Quaternion again = euler_to_quaternion(recovered);
    // Two euler triples can name the same orientation, so compare the rotations
    // they produce rather than the angles themselves.
    for (const Vector3 probe : {Vector3{1.0f, 0.0f, 0.0f}, Vector3{0.0f, 1.0f, 0.0f}, Vector3{0.0f, 0.0f, 1.0f}}) {
        if (distance(rotate(direct, probe), rotate(again, probe)) > 1e-3f) {
            std::cerr << "FAILED: " << message << " (" << degrees.x << ", " << degrees.y << ", " << degrees.z
                      << ") round-tripped to (" << recovered.x << ", " << recovered.y << ", " << recovered.z << ")\n";
            std::exit(1);
        }
    }
    require(recovered.x >= 0.0f && recovered.x < 360.0f && recovered.y >= 0.0f && recovered.y < 360.0f &&
                recovered.z >= 0.0f && recovered.z < 360.0f,
            "the Inspector shows angles wrapped into [0, 360)");
}

// Unity applies the rotations in Z, X, Y order. A single-axis turn is the
// simplest way to pin that down: 90 degrees about Y must send +Z to +X.
void axis_conventions() {
    const Quaternion yaw = euler_to_quaternion({0.0f, 90.0f, 0.0f});
    require(distance(rotate(yaw, {0.0f, 0.0f, 1.0f}), {1.0f, 0.0f, 0.0f}) < 1e-4f,
            "90 degrees of yaw must turn forward into right");

    const Quaternion pitch = euler_to_quaternion({90.0f, 0.0f, 0.0f});
    require(distance(rotate(pitch, {0.0f, 0.0f, 1.0f}), {0.0f, -1.0f, 0.0f}) < 1e-4f,
            "90 degrees of pitch must turn forward into down");

    const Quaternion roll = euler_to_quaternion({0.0f, 0.0f, 90.0f});
    require(distance(rotate(roll, {1.0f, 0.0f, 0.0f}), {0.0f, 1.0f, 0.0f}) < 1e-4f,
            "90 degrees of roll must turn right into up");

    // Composition order: Z first, then X, then Y.
    const Quaternion composed = euler_to_quaternion({0.0f, 90.0f, 90.0f});
    require(distance(rotate(composed, {1.0f, 0.0f, 0.0f}), {0.0f, 1.0f, 0.0f}) < 1e-4f,
            "Unity composes euler angles in Z, X, Y order");
}

void identity_and_degenerate_input() {
    const Quaternion identity = euler_to_quaternion({});
    require(std::fabs(identity.w - 1.0f) < 1e-6f && std::fabs(identity.x) < 1e-6f && std::fabs(identity.y) < 1e-6f &&
                std::fabs(identity.z) < 1e-6f,
            "zero angles must produce the identity rotation");
    const Vector3 zero = quaternion_to_euler({0.0f, 0.0f, 0.0f, 1.0f});
    require(std::fabs(zero.x) < 1e-4f && std::fabs(zero.y) < 1e-4f && std::fabs(zero.z) < 1e-4f,
            "the identity rotation must read back as zero angles");
    // A zero quaternion cannot be normalised; reading one must not divide by
    // zero and hand the Inspector a NaN.
    const Vector3 degenerate = quaternion_to_euler({0.0f, 0.0f, 0.0f, 0.0f});
    require(degenerate.x == 0.0f && degenerate.y == 0.0f && degenerate.z == 0.0f,
            "a zero-length rotation must read back as zero, not NaN");
}

void gimbal_lock() {
    // At +/-90 degrees of pitch, Y and Z describe the same turn. Unity pins Z
    // and reports the whole rotation on Y; either way the orientation must
    // survive the round trip.
    require_round_trip({90.0f, 45.0f, 0.0f}, "gimbal lock, pitch up");
    require_round_trip({-90.0f, 30.0f, 0.0f}, "gimbal lock, pitch down");
    const Vector3 locked = quaternion_to_euler(euler_to_quaternion({90.0f, 45.0f, 20.0f}));
    require(std::fabs(locked.z) < 1e-3f || std::fabs(locked.z - 360.0f) < 1e-3f,
            "gimbal lock must pin the roll term to zero");
}

} // namespace

int main() {
    axis_conventions();
    identity_and_degenerate_input();
    gimbal_lock();

    require_round_trip({0.0f, 0.0f, 0.0f}, "identity");
    require_round_trip({30.0f, 0.0f, 0.0f}, "pitch only");
    require_round_trip({0.0f, 120.0f, 0.0f}, "yaw only");
    require_round_trip({0.0f, 0.0f, 200.0f}, "roll only");
    require_round_trip({12.5f, 200.0f, 47.0f}, "all three axes");
    require_round_trip({-33.0f, -170.0f, -95.0f}, "negative angles");
    require_round_trip({359.0f, 359.0f, 359.0f}, "angles just under a full turn");
    require_round_trip({720.0f, -450.0f, 1080.0f}, "angles beyond a full turn");

    std::cout << "unity euler conversion contract passed\n";
    return 0;
}

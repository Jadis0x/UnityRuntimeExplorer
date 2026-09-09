// Copyright (c) 2026 Jadis0x. All rights reserved.
// The look rotation the camera uses when Transform.LookAt(Vector3) is not in
// the build. Checked by where the rotation actually points, since that is the
// property the camera needs.
#include "mod/explorer/camera/camera_math.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {

using Explorer::CameraMath::look_rotation;
using Explorer::CameraMath::rotate;
using URK::Unity::Quaternion;
using URK::Unity::Vector3;

void require(bool condition, const char *message) {
    if (condition)
        return;
    std::cerr << "FAILED: " << message << '\n';
    std::exit(1);
}

void require_direction(Vector3 direction, const char *message) {
    const Quaternion rotation = look_rotation(direction);
    const Vector3 forward = rotate(rotation, Vector3{0.0f, 0.0f, 1.0f});
    const Vector3 expected = Explorer::CameraMath::normalized(direction);
    const float error = Explorer::CameraMath::length(
        Vector3{forward.x - expected.x, forward.y - expected.y, forward.z - expected.z});
    if (error > 1e-4f) {
        std::cerr << "FAILED: " << message << " (points at " << forward.x << ", " << forward.y << ", " << forward.z
                  << " instead of " << expected.x << ", " << expected.y << ", " << expected.z << ")\n";
        std::exit(1);
    }
    const float magnitude =
        std::sqrt(rotation.x * rotation.x + rotation.y * rotation.y + rotation.z * rotation.z + rotation.w * rotation.w);
    require(std::fabs(magnitude - 1.0f) < 1e-4f, "a look rotation must be a unit quaternion");
}

} // namespace

int main() {
    require_direction({0.0f, 0.0f, 1.0f}, "forward");
    require_direction({0.0f, 0.0f, -1.0f}, "backward");
    require_direction({1.0f, 0.0f, 0.0f}, "right");
    require_direction({-1.0f, 0.0f, 0.0f}, "left");
    require_direction({0.0f, 0.5f, 1.0f}, "above and ahead");
    require_direction({-3.0f, -2.0f, 4.0f}, "an arbitrary direction");
    require_direction({0.0f, 0.0f, 12.5f}, "an unnormalised direction");
    // The camera focus path looks down at a target from above, which is the
    // steepest angle it ever asks for.
    require_direction({0.001f, -8.0f, 0.001f}, "almost straight down");

    // The rotation keeps the camera level: with a Y-up reference, the rotated
    // right vector stays in the horizontal plane.
    const Quaternion rotation = look_rotation(Vector3{2.0f, -3.0f, 5.0f});
    const Vector3 right = rotate(rotation, Vector3{1.0f, 0.0f, 0.0f});
    require(std::fabs(right.y) < 1e-4f, "a look rotation must not roll the camera");

    // Degenerate input leaves the camera where it is rather than producing a
    // rotation full of NaNs.
    const Quaternion identity = look_rotation(Vector3{0.0f, 0.0f, 0.0f});
    require(identity.w == 1.0f && identity.x == 0.0f && identity.y == 0.0f && identity.z == 0.0f,
            "a zero direction gives identity");
    const Quaternion straight_up = look_rotation(Vector3{0.0f, 1.0f, 0.0f});
    require(std::isfinite(straight_up.x) && std::isfinite(straight_up.y) && std::isfinite(straight_up.z) &&
                std::isfinite(straight_up.w),
            "a direction parallel to up must not produce NaNs");

    std::cout << "camera math contract passed\n";
    return 0;
}

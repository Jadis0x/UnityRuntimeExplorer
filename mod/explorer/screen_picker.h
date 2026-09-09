// Copyright (c) 2026 Jadis0x. All rights reserved.
#pragma once

// "Click the screen, select the GameObject" - the Unity editor's scene-view
// picking, done from inside a running build.
//
// The editor can pick because it owns the scene view and its render pipeline.
// A mod has neither, so picking is rebuilt from the two things the runtime will
// still answer: where an object's geometry is, and where the camera is looking.
// Every object under the cursor is measured into a screen rectangle and the
// rectangles are ranked by the order Unity draws them.
//
// UI and world objects need separate measurements, which is the whole
// difficulty of the feature:
//   * A world object is a 3D bounding box (Renderer.bounds, or Collider.bounds
//     for things that are invisible but solid), projected through the rendering
//     camera into a screen rectangle.
//   * A Canvas element has no bounding box worth projecting. It is a flat rect
//     in its canvas's space, so its four corners are placed in world space and
//     then either read as screen pixels directly (Screen Space - Overlay, where
//     canvas space *is* screen space) or projected through the canvas's own
//     camera (Screen Space - Camera and World Space, which may not be the
//     camera the world pass uses).
// Once both are screen rectangles they compare directly, and the UI wins ties
// against the world because that is the order they are painted in.
//
// The geometry and the ranking live in pick_geometry.h so they can be tested
// without a game; this file is only the managed measurement pass.

#include "pick_geometry.h"
#include "sdk/unity/unity.h"

#include <cstddef>
#include <string>
#include <vector>

namespace Explorer::ScreenPicker {

struct Options {
    // Height of the surface the click was measured against, in pixels. Screen
    // rectangles are reported top-left origin to match the overlay, and Unity
    // reports its own screen coordinates bottom-left, so the flip needs it.
    float screen_height = 0.0f;
    bool include_ui = true;
    bool include_world = true;
    // Objects that are switched off are invisible, so they are not under the
    // cursor - but a disabled panel is often exactly what a user is hunting for.
    bool include_inactive = false;
    // A single pick walks every renderer and every canvas element in the scene.
    // The caps keep one click from stalling the game on a huge scene.
    std::size_t max_world_objects = 8000;
    std::size_t max_ui_elements = 4000;
};

struct Result {
    // Everything under the cursor, topmost first.
    std::vector<Pick::Candidate> hits;
    std::string camera_name;
    // Says why the result is empty when it is - no camera, nothing scanned, or
    // simply nothing at that point.
    std::string diagnostic;
    std::size_t scanned_ui = 0;
    std::size_t scanned_world = 0;
    bool camera_available = false;
};

// Runs both passes and ranks the result. Must be called on a thread that may
// touch the managed runtime - the model's command pump, not the render thread.
Result pick(URK::Unity::Vector2 screen_point, const Options &options);

} // namespace Explorer::ScreenPicker

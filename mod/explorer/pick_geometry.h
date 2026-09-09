// Copyright (c) 2026 Jadis0x. All rights reserved.
// The geometry and ordering behind "click the screen, select the GameObject".
//
// Screen picking has to answer two questions, and only the first one needs the
// managed runtime: *where* is each object on screen, and *which* of the objects
// under the cursor did the player actually click. This header owns the second
// question plus the arithmetic behind the first, so both can be tested without
// a running game. `screen_picker.cpp` supplies the managed measurements.
//
// UI and world objects are measured differently - a Canvas element is a rect in
// screen space, a world object is a 3D bounding box projected into one - but
// once both are screen rectangles they rank against each other by the rules
// Unity itself draws by: the UI is painted over the scene, canvases stack by
// sorting order, siblings later in the hierarchy paint over earlier ones, and
// in the world the nearest surface wins.
#pragma once

#include "sdk/unity/unity_euler.h"
#include "sdk/unity/unity_types.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include <string>
#include <vector>

namespace Explorer::Pick {

using URK::Unity::Bounds;
using URK::Unity::Quaternion;
using URK::Unity::Rect;
using URK::Unity::Vector2;
using URK::Unity::Vector3;

// A projected point: screen pixels plus the depth Unity's WorldToScreenPoint
// reports. Depth matters because that call mirrors points behind the camera
// through the origin instead of failing, so a corner with depth <= 0 is not a
// position - it is noise that would stretch the rect across the screen.
struct ScreenPoint {
    Vector2 position{};
    float depth = 0.0f;
};

struct ScreenRect {
    float min_x = 0.0f;
    float min_y = 0.0f;
    float max_x = 0.0f;
    float max_y = 0.0f;
    bool valid = false;

    float width() const { return max_x - min_x; }
    float height() const { return max_y - min_y; }
    float area() const { return valid ? width() * height() : 0.0f; }
    Vector2 center() const { return {(min_x + max_x) * 0.5f, (min_y + max_y) * 0.5f}; }
};

inline bool contains(const ScreenRect &rect, Vector2 point) {
    return rect.valid && point.x >= rect.min_x && point.x <= rect.max_x && point.y >= rect.min_y &&
           point.y <= rect.max_y;
}

// The eight corners of a world-space bounding box, in the order a caller can
// project one by one.
inline void aabb_corners(const Bounds &bounds, Vector3 out[8]) {
    const Vector3 min = bounds.min();
    const Vector3 max = bounds.max();
    out[0] = {min.x, min.y, min.z};
    out[1] = {max.x, min.y, min.z};
    out[2] = {min.x, max.y, min.z};
    out[3] = {max.x, max.y, min.z};
    out[4] = {min.x, min.y, max.z};
    out[5] = {max.x, min.y, max.z};
    out[6] = {min.x, max.y, max.z};
    out[7] = {max.x, max.y, max.z};
}

// The four corners of a RectTransform in world space, which is what
// RectTransform.GetWorldCorners returns - computed here instead, because
// GetWorldCorners needs a managed array allocated to write into.
//
// `rect` is the element's local rectangle (already relative to its pivot),
// `lossy_scale` its accumulated world scale. Corners come back bottom-left,
// top-left, top-right, bottom-right, matching Unity's own order.
inline void rect_transform_corners(Vector3 position, Quaternion rotation, Vector3 lossy_scale, const Rect &rect,
                                   Vector3 out[4]) {
    const float left = rect.x * lossy_scale.x;
    const float right = (rect.x + rect.width) * lossy_scale.x;
    const float bottom = rect.y * lossy_scale.y;
    const float top = (rect.y + rect.height) * lossy_scale.y;
    const Vector3 local[4] = {
        {left, bottom, 0.0f}, {left, top, 0.0f}, {right, top, 0.0f}, {right, bottom, 0.0f}};
    for (int index = 0; index < 4; ++index)
        out[index] = position + URK::Unity::rotate(rotation, local[index]);
}

// The screen rectangle enclosing a set of projected corners. Corners behind the
// camera are dropped; if every corner is behind it, the object is not on screen
// and the rect is invalid. `require_all_in_front` is for flat UI quads, where a
// partially-behind rect means the element is clipped by the camera plane and
// its screen rect would be meaningless.
inline ScreenRect enclosing_rect(const ScreenPoint *points, std::size_t count, bool require_all_in_front = false) {
    ScreenRect rect{};
    std::size_t used = 0;
    for (std::size_t index = 0; index < count; ++index) {
        if (points[index].depth <= 0.0f) {
            if (require_all_in_front)
                return {};
            continue;
        }
        const Vector2 position = points[index].position;
        if (used == 0) {
            rect.min_x = rect.max_x = position.x;
            rect.min_y = rect.max_y = position.y;
        } else {
            rect.min_x = std::min(rect.min_x, position.x);
            rect.max_x = std::max(rect.max_x, position.x);
            rect.min_y = std::min(rect.min_y, position.y);
            rect.max_y = std::max(rect.max_y, position.y);
        }
        ++used;
    }
    rect.valid = used > 0;
    return rect;
}

// Rects thinner than this are widened around their centre so a flat wall, an
// edge-on sprite or a zero-size UI element stays clickable.
inline constexpr float kMinimumPickSize = 6.0f;

inline ScreenRect with_minimum_size(ScreenRect rect, float minimum = kMinimumPickSize) {
    if (!rect.valid)
        return rect;
    const Vector2 middle = rect.center();
    if (rect.width() < minimum) {
        rect.min_x = middle.x - minimum * 0.5f;
        rect.max_x = middle.x + minimum * 0.5f;
    }
    if (rect.height() < minimum) {
        rect.min_y = middle.y - minimum * 0.5f;
        rect.max_y = middle.y + minimum * 0.5f;
    }
    return rect;
}

// Where the view ray enters a world-space bounding box, in world units, or -1
// when the ray misses it. This is what makes world picking accurate: the
// screen rectangle of a projected box is far larger than the box itself, so
// testing the cursor against that rectangle picks up everything in the
// neighbourhood, and ordering by the distance to a box's centre puts a large
// object that merely straddles the view in front of the small one actually
// under the cursor. The slab method answers both questions exactly.
inline float ray_aabb_entry(Vector3 origin, Vector3 direction, const Bounds &bounds) {
    const Vector3 min = bounds.min();
    const Vector3 max = bounds.max();
    const float origin_axis[3] = {origin.x, origin.y, origin.z};
    const float direction_axis[3] = {direction.x, direction.y, direction.z};
    const float min_axis[3] = {min.x, min.y, min.z};
    const float max_axis[3] = {max.x, max.y, max.z};

    float enter = 0.0f;
    float exit = 3.4028235e38f;
    for (int axis = 0; axis < 3; ++axis) {
        if (std::fabs(direction_axis[axis]) < 1e-8f) {
            // Parallel to this slab: a miss unless the origin already lies
            // between its planes.
            if (origin_axis[axis] < min_axis[axis] || origin_axis[axis] > max_axis[axis])
                return -1.0f;
            continue;
        }
        const float inverse = 1.0f / direction_axis[axis];
        float near_hit = (min_axis[axis] - origin_axis[axis]) * inverse;
        float far_hit = (max_axis[axis] - origin_axis[axis]) * inverse;
        if (near_hit > far_hit)
            std::swap(near_hit, far_hit);
        enter = std::max(enter, near_hit);
        exit = std::min(exit, far_hit);
        if (enter > exit)
            return -1.0f;
    }
    return enter;
}

// Which pass found the candidate. The order of the enumerators is the order
// they stack on screen: the UI is drawn over the world.
enum class Layer : std::uint8_t {
    UI = 0,
    World = 1,
};

enum class Source : std::uint8_t {
    Unknown,
    CanvasElement,
    Renderer,
    Collider,
};

struct Candidate {
    int instance_id = 0;
    std::uintptr_t object_address = 0;
    std::string name;
    std::string path;
    std::string source_type;
    Layer layer = Layer::World;
    Source source = Source::Unknown;
    // UI ordering: the canvas's sorting order, then the element's position in
    // the canvas's depth-first walk, because later siblings paint over earlier
    // ones. World ordering: distance from the camera.
    int sort_order = 0;
    int draw_index = 0;
    float distance = 0.0f;
    // A UI element large enough to be a background, a vignette or a full-screen
    // container. It is still pickable, but it must not outrank the scene it is
    // painted over - a click aimed at a character is not aimed at the backdrop
    // behind it.
    bool backdrop = false;
    // Volume of the world bounding box. A waterfall, a terrain chunk or a
    // lighting volume can be entered before the house standing inside it, and
    // no depth test can tell those apart - but the tighter box is what a click
    // was aimed at.
    float volume = 0.0f;
    ScreenRect rect{};
};

// World hits closer together than this count as the same depth, and are then
// separated by how tightly they enclose what was clicked.
inline constexpr float kDepthTieAbsolute = 2.0f;
inline constexpr float kDepthTieRelative = 0.15f;

inline bool same_depth(float left, float right) {
    const float tolerance = std::max(kDepthTieAbsolute, kDepthTieRelative * std::min(left, right));
    return std::fabs(left - right) <= tolerance;
}

// Backdrops fall behind the world; everything else keeps the painter's order.
inline int rank_class(const Candidate &candidate) {
    if (candidate.layer != Layer::UI)
        return 1;
    return candidate.backdrop ? 2 : 0;
}

// A UI element covering this much of the screen is a backdrop, not a control.
inline constexpr float kBackdropScreenFraction = 0.55f;

// Strict weak ordering: `left` sits above `right` on screen.
inline bool draws_above(const Candidate &left, const Candidate &right) {
    const int left_class = rank_class(left);
    const int right_class = rank_class(right);
    if (left_class != right_class)
        return left_class < right_class;
    if (left.layer != right.layer)
        return left.layer < right.layer;
    if (left.layer == Layer::UI) {
        if (left.sort_order != right.sort_order)
            return left.sort_order > right.sort_order;
        if (left.draw_index != right.draw_index)
            return left.draw_index > right.draw_index;
    } else if (!same_depth(left.distance, right.distance)) {
        return left.distance < right.distance;
    } else if (left.volume != right.volume && left.volume > 0.0f && right.volume > 0.0f) {
        // Same depth: the tighter box is the object, the looser one is the
        // scenery it stands in.
        return left.volume < right.volume;
    }
    // Same depth: the tighter rectangle is the thing being aimed at, so a button
    // wins over the panel behind it and a prop wins over the room it sits in.
    const float left_area = left.rect.area();
    const float right_area = right.rect.area();
    if (left_area != right_area)
        return left_area < right_area;
    return left.instance_id < right.instance_id;
}

// Everything under the cursor, topmost first - the list Unity shows when you
// right-click a crowded spot and it offers you every object beneath it.
inline std::vector<Candidate> rank(std::vector<Candidate> candidates, Vector2 screen_point) {
    std::vector<Candidate> hits;
    hits.reserve(candidates.size());
    for (Candidate &candidate : candidates) {
        if (contains(candidate.rect, screen_point))
            hits.push_back(std::move(candidate));
    }
    std::stable_sort(hits.begin(), hits.end(), draws_above);
    return hits;
}

} // namespace Explorer::Pick

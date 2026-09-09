// Copyright (c) 2026 Jadis0x. All rights reserved.
// Screen picking: the rectangles objects occupy on screen and the order they
// stack in. Both are decided without the managed runtime, so both are checked
// here against the cases a real scene produces - a UI panel over the world, a
// button over its panel, a box straddling the camera plane.
#include "mod/explorer/pick_geometry.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {

using Explorer::Pick::aabb_corners;
using Explorer::Pick::Candidate;
using Explorer::Pick::contains;
using Explorer::Pick::draws_above;
using Explorer::Pick::enclosing_rect;
using Explorer::Pick::Layer;
using Explorer::Pick::rank;
using Explorer::Pick::ray_aabb_entry;
using Explorer::Pick::rect_transform_corners;
using Explorer::Pick::ScreenPoint;
using Explorer::Pick::ScreenRect;
using Explorer::Pick::with_minimum_size;
using URK::Unity::Bounds;
using URK::Unity::Quaternion;
using URK::Unity::Rect;
using URK::Unity::Vector2;
using URK::Unity::Vector3;

void require(bool condition, const char *message) {
    if (condition)
        return;
    std::cerr << "FAILED: " << message << '\n';
    std::exit(1);
}

bool close_to(float left, float right, float tolerance = 1e-3f) {
    return std::fabs(left - right) <= tolerance;
}

Candidate ui_element(int id, int sort_order, int draw_index, ScreenRect rect) {
    Candidate candidate{};
    candidate.instance_id = id;
    candidate.layer = Layer::UI;
    candidate.sort_order = sort_order;
    candidate.draw_index = draw_index;
    candidate.rect = rect;
    return candidate;
}

Candidate world_object(int id, float distance, ScreenRect rect) {
    Candidate candidate{};
    candidate.instance_id = id;
    candidate.layer = Layer::World;
    candidate.distance = distance;
    candidate.rect = rect;
    return candidate;
}

ScreenRect box(float min_x, float min_y, float max_x, float max_y) {
    return {min_x, min_y, max_x, max_y, true};
}

void bounding_box_corners() {
    Bounds bounds{};
    bounds.center = {1.0f, 2.0f, 3.0f};
    bounds.extents = {0.5f, 1.0f, 1.5f};
    Vector3 corners[8]{};
    aabb_corners(bounds, corners);
    for (const Vector3 &corner : corners) {
        require(close_to(std::fabs(corner.x - 1.0f), 0.5f) && close_to(std::fabs(corner.y - 2.0f), 1.0f) &&
                    close_to(std::fabs(corner.z - 3.0f), 1.5f),
                "every AABB corner sits one extent from the centre on each axis");
    }
    // All eight must be distinct, or the projected rect collapses.
    for (int left = 0; left < 8; ++left)
        for (int right = left + 1; right < 8; ++right)
            require(!(close_to(corners[left].x, corners[right].x) && close_to(corners[left].y, corners[right].y) &&
                      close_to(corners[left].z, corners[right].z)),
                    "the eight AABB corners must be distinct");
}

void rect_transform_geometry() {
    // A 200x100 element whose pivot is centred, unrotated, at the origin.
    Vector3 corners[4]{};
    rect_transform_corners({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 1.0f}, {1.0f, 1.0f, 1.0f},
                           Rect{-100.0f, -50.0f, 200.0f, 100.0f}, corners);
    require(close_to(corners[0].x, -100.0f) && close_to(corners[0].y, -50.0f), "corner 0 is bottom-left");
    require(close_to(corners[1].x, -100.0f) && close_to(corners[1].y, 50.0f), "corner 1 is top-left");
    require(close_to(corners[2].x, 100.0f) && close_to(corners[2].y, 50.0f), "corner 2 is top-right");
    require(close_to(corners[3].x, 100.0f) && close_to(corners[3].y, -50.0f), "corner 3 is bottom-right");

    // Canvas scaling must widen the element, not just move it.
    rect_transform_corners({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 1.0f}, {2.0f, 2.0f, 1.0f},
                           Rect{-100.0f, -50.0f, 200.0f, 100.0f}, corners);
    require(close_to(corners[2].x, 200.0f) && close_to(corners[2].y, 100.0f),
            "a canvas scale factor scales the element's corners");

    // A quarter turn about Z: the top-right corner swings to the top-left.
    const float half = 45.0f * 0.017453292519943295f; // 90 degrees, halved for the quaternion
    const Quaternion turn{0.0f, 0.0f, std::sin(half), std::cos(half)};
    rect_transform_corners({0.0f, 0.0f, 0.0f}, turn, {1.0f, 1.0f, 1.0f}, Rect{-100.0f, -50.0f, 200.0f, 100.0f},
                           corners);
    require(close_to(corners[2].x, -50.0f, 1e-2f) && close_to(corners[2].y, 100.0f, 1e-2f),
            "a rotated element's corners rotate with it");

    // An off-centre pivot: a top-left-pivoted element hangs down and right.
    rect_transform_corners({10.0f, 20.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 1.0f}, {1.0f, 1.0f, 1.0f},
                           Rect{0.0f, -100.0f, 200.0f, 100.0f}, corners);
    require(close_to(corners[0].x, 10.0f) && close_to(corners[0].y, -80.0f),
            "the rect is already relative to the pivot, so it offsets from the position");
}

void projection_to_screen_rects() {
    const ScreenPoint in_front[4] = {
        {{100.0f, 100.0f}, 5.0f}, {{140.0f, 100.0f}, 5.0f}, {{140.0f, 160.0f}, 5.0f}, {{100.0f, 160.0f}, 5.0f}};
    const ScreenRect rect = enclosing_rect(in_front, 4);
    require(rect.valid && close_to(rect.min_x, 100.0f) && close_to(rect.max_x, 140.0f) && close_to(rect.min_y, 100.0f) &&
                close_to(rect.max_y, 160.0f),
            "the rect encloses every corner in front of the camera");
    require(close_to(rect.area(), 40.0f * 60.0f), "the rect's area is its width times its height");

    // WorldToScreenPoint mirrors points behind the camera through the origin.
    // Including one would stretch the rect across the screen, so it is dropped.
    const ScreenPoint straddling[4] = {
        {{100.0f, 100.0f}, 5.0f}, {{140.0f, 160.0f}, 5.0f}, {{-9000.0f, -9000.0f}, -3.0f}, {{9000.0f, 9000.0f}, 0.0f}};
    const ScreenRect clipped = enclosing_rect(straddling, 4);
    require(clipped.valid && close_to(clipped.min_x, 100.0f) && close_to(clipped.max_x, 140.0f),
            "corners behind the camera are dropped, not enclosed");

    const ScreenPoint all_behind[2] = {{{1.0f, 1.0f}, -1.0f}, {{2.0f, 2.0f}, -4.0f}};
    require(!enclosing_rect(all_behind, 2).valid, "an object entirely behind the camera has no screen rect");
    require(!enclosing_rect(nullptr, 0).valid, "no corners means no rect");

    // A flat UI quad crossing the camera plane has no meaningful rect at all.
    require(!enclosing_rect(straddling, 4, true).valid,
            "a partially clipped UI element is rejected rather than guessed at");
}

void tiny_targets_stay_clickable() {
    const ScreenRect edge_on = with_minimum_size(box(200.0f, 100.0f, 200.5f, 300.0f));
    require(edge_on.width() >= Explorer::Pick::kMinimumPickSize, "an edge-on object is widened to a clickable size");
    require(close_to(edge_on.center().x, 200.25f), "widening keeps the object's centre");
    require(close_to(edge_on.min_y, 100.0f) && close_to(edge_on.max_y, 300.0f),
            "an axis that is already big enough is left alone");
    require(!with_minimum_size(ScreenRect{}).valid, "an invalid rect stays invalid");
    require(contains(with_minimum_size(box(200.0f, 200.0f, 200.0f, 200.0f)), Vector2{200.0f, 200.0f}),
            "a zero-size element still catches a click on its centre");
}

void hit_testing() {
    const ScreenRect rect = box(10.0f, 20.0f, 110.0f, 220.0f);
    require(contains(rect, Vector2{60.0f, 120.0f}), "a point inside the rect hits");
    require(contains(rect, Vector2{10.0f, 20.0f}), "the top-left edge counts as inside");
    require(contains(rect, Vector2{110.0f, 220.0f}), "the bottom-right edge counts as inside");
    require(!contains(rect, Vector2{9.0f, 120.0f}), "a point left of the rect misses");
    require(!contains(rect, Vector2{60.0f, 221.0f}), "a point below the rect misses");
    require(!contains(ScreenRect{}, Vector2{0.0f, 0.0f}), "an invalid rect catches nothing");
}

void stacking_order() {
    // The whole point of the feature: a canvas is painted over the scene, so a
    // click on a HUD element must not select the world behind it.
    const Candidate hud = ui_element(1, 0, 0, box(0.0f, 0.0f, 100.0f, 100.0f));
    const Candidate prop = world_object(2, 1.0f, box(0.0f, 0.0f, 10.0f, 10.0f));
    require(draws_above(hud, prop), "UI draws over the world, however near the world object is");
    require(!draws_above(prop, hud), "and the ordering is not symmetric");

    require(draws_above(ui_element(1, 10, 0, box(0, 0, 10, 10)), ui_element(2, 5, 99, box(0, 0, 10, 10))),
            "a canvas with a higher sorting order wins regardless of sibling order");
    require(draws_above(ui_element(1, 0, 7, box(0, 0, 10, 10)), ui_element(2, 0, 3, box(0, 0, 10, 10))),
            "within one canvas, the later sibling is painted on top");
    require(draws_above(world_object(1, 2.0f, box(0, 0, 10, 10)), world_object(2, 9.0f, box(0, 0, 10, 10))),
            "in the world the nearer surface wins");

    // Equal depth: the tighter rect is what the cursor is aimed at.
    require(draws_above(ui_element(1, 0, 0, box(40.0f, 40.0f, 60.0f, 60.0f)),
                        ui_element(2, 0, 0, box(0.0f, 0.0f, 400.0f, 400.0f))),
            "a button wins over the panel it sits on");
    require(draws_above(world_object(1, 5.0f, box(40.0f, 40.0f, 60.0f, 60.0f)),
                        world_object(2, 5.0f, box(0.0f, 0.0f, 400.0f, 400.0f))),
            "a prop wins over the room behind it at the same distance");
}

void ranking_a_crowded_click() {
    std::vector<Candidate> scene;
    scene.push_back(world_object(10, 12.0f, box(0.0f, 0.0f, 800.0f, 600.0f)));   // the level
    scene.push_back(world_object(11, 4.0f, box(180.0f, 180.0f, 260.0f, 260.0f))); // a crate
    scene.push_back(ui_element(20, 0, 0, box(150.0f, 150.0f, 500.0f, 400.0f)));   // a panel
    scene.push_back(ui_element(21, 0, 5, box(190.0f, 190.0f, 240.0f, 220.0f)));   // a button on it
    scene.push_back(ui_element(22, 100, 0, box(600.0f, 0.0f, 800.0f, 60.0f)));    // an overlay elsewhere

    const std::vector<Candidate> hits = rank(scene, Vector2{200.0f, 200.0f});
    require(hits.size() == 4, "everything under the cursor is reported, not just the winner");
    require(hits[0].instance_id == 21, "the button on the panel is picked first");
    require(hits[1].instance_id == 20, "then the panel it sits on");
    require(hits[2].instance_id == 11, "then the nearer world object");
    require(hits[3].instance_id == 10, "then the level behind it");

    const std::vector<Candidate> empty_spot = rank(scene, Vector2{790.0f, 590.0f});
    require(empty_spot.size() == 1 && empty_spot[0].instance_id == 10,
            "a click away from the crowd only reports what is actually there");
    require(rank(scene, Vector2{-5.0f, -5.0f}).empty(), "a click outside everything reports nothing");
    require(rank({}, Vector2{0.0f, 0.0f}).empty(), "an empty scene reports nothing");
}


Bounds box_at(Vector3 center, Vector3 extents) {
    Bounds bounds{};
    bounds.center = center;
    bounds.extents = extents;
    return bounds;
}

// The test that decides whether clicking a house selects the house or the
// waterfall behind it.
void ray_against_boxes() {
    const Vector3 eye{0.0f, 0.0f, 0.0f};
    const Vector3 forward{0.0f, 0.0f, 1.0f};

    const float near_entry = ray_aabb_entry(eye, forward, box_at({0.0f, 0.0f, 10.0f}, {1.0f, 1.0f, 1.0f}));
    require(close_to(near_entry, 9.0f), "the ray enters a box at its near face, not at its centre");

    require(ray_aabb_entry(eye, forward, box_at({5.0f, 0.0f, 10.0f}, {1.0f, 1.0f, 1.0f})) < 0.0f,
            "a box beside the ray is a miss");
    require(ray_aabb_entry(eye, forward, box_at({0.0f, 0.0f, -10.0f}, {1.0f, 1.0f, 1.0f})) < 0.0f,
            "a box behind the camera is a miss");

    // Centre distance and entry distance disagree, and entry is the honest
    // answer: a big box starting at z=5 really is in front of a small one at
    // z=11, however far away its centre sits.
    const Bounds small_prop = box_at({0.0f, 0.0f, 12.0f}, {1.0f, 1.0f, 1.0f});
    const Bounds wide_scenery = box_at({0.0f, 0.0f, 20.0f}, {15.0f, 15.0f, 15.0f});
    require(close_to(ray_aabb_entry(eye, forward, small_prop), 11.0f), "small prop is entered at 11");
    require(close_to(ray_aabb_entry(eye, forward, wide_scenery), 5.0f), "wide scenery is entered at 5");
    require(ray_aabb_entry(eye, forward, wide_scenery) < ray_aabb_entry(eye, forward, small_prop),
            "entry distance orders by nearest surface, not by nearest centre");

    // The camera standing inside a box: the entry distance is zero, never
    // negative, so a room the player is in stays selectable.
    require(close_to(ray_aabb_entry(eye, forward, box_at({0.0f, 0.0f, 0.0f}, {5.0f, 5.0f, 5.0f})), 0.0f),
            "a box the ray starts inside is entered at zero");

    // A ray parallel to a slab still hits when it runs down the inside of it.
    require(ray_aabb_entry({0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, box_at({10.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f})) >
                0.0f,
            "an axis-aligned ray along X still enters the box");
    require(ray_aabb_entry({0.0f, 50.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, box_at({10.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f})) <
                0.0f,
            "a ray parallel to a slab and outside it misses");
}

// A full-screen background image is painted over the scene, but it is never
// what a click on a character was aiming at.
// Depth alone cannot separate a house from the waterfall whose bounding box
// swallows it, because the loose box is entered first. The tighter one wins.
void tight_boxes_beat_loose_ones_at_the_same_depth() {
    Candidate house = world_object(1, 11.0f, box(400.0f, 300.0f, 600.0f, 700.0f));
    house.volume = 8.0f * 8.0f * 8.0f;
    Candidate waterfall = world_object(2, 10.0f, box(0.0f, 0.0f, 1900.0f, 1000.0f));
    waterfall.volume = 60.0f * 60.0f * 60.0f;
    require(draws_above(house, waterfall), "at the same depth the tighter bounding box is the thing clicked");

    // A genuinely nearer object still wins, however small the far one is.
    Candidate near_prop = world_object(3, 2.0f, box(0.0f, 0.0f, 10.0f, 10.0f));
    near_prop.volume = 1000000.0f;
    Candidate far_prop = world_object(4, 40.0f, box(0.0f, 0.0f, 10.0f, 10.0f));
    far_prop.volume = 1.0f;
    require(draws_above(near_prop, far_prop), "a clearly nearer surface still wins regardless of size");
}

void backdrops_fall_behind_the_world() {
    Candidate backdrop = ui_element(1, 0, 0, box(0.0f, 0.0f, 1920.0f, 1080.0f));
    backdrop.backdrop = true;
    const Candidate character = world_object(2, 8.0f, box(800.0f, 400.0f, 1000.0f, 900.0f));
    const Candidate button = ui_element(3, 0, 4, box(850.0f, 500.0f, 950.0f, 540.0f));

    require(draws_above(character, backdrop), "a world object outranks a full-screen UI backdrop");
    require(draws_above(button, character), "a real UI control still outranks the world");
    require(draws_above(button, backdrop), "and it outranks the backdrop too");

    std::vector<Candidate> scene{backdrop, character, button};
    const std::vector<Candidate> hits = rank(scene, Vector2{900.0f, 520.0f});
    require(hits.size() == 3, "the backdrop stays in the list, it just stops winning");
    require(hits[0].instance_id == 3 && hits[1].instance_id == 2 && hits[2].instance_id == 1,
            "control, then world, then backdrop");
}

} // namespace

int main() {
    bounding_box_corners();
    rect_transform_geometry();
    projection_to_screen_rects();
    tiny_targets_stay_clickable();
    hit_testing();
    stacking_order();
    ranking_a_crowded_click();
    ray_against_boxes();
    tight_boxes_beat_loose_ones_at_the_same_depth();
    backdrops_fall_behind_the_world();
    std::cout << "screen pick geometry contract passed\n";
    return 0;
}

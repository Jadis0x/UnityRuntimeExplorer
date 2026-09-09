// Copyright (c) 2026 Jadis0x. All rights reserved.
// The managed half of screen picking: measuring where things are on screen.
#include "screen_picker.h"

#include "model_shared.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace Explorer::ScreenPicker {
namespace {

// Keeps the SDK's error slot from carrying a stale message into the probe.
inline void detail_clear_error() { URK::Unity::detail::clear_error(); }

using URK::Unity::Camera;
using URK::Unity::Canvas;
using URK::Unity::CanvasRenderer;
using URK::Unity::CanvasRenderMode;
using URK::Unity::Collider;
using URK::Unity::GameObject;
using URK::Unity::Object;
using URK::Unity::RectTransform;
using URK::Unity::Renderer;
using URK::Unity::Transform;
using URK::Unity::Vector2;
using URK::Unity::Vector3;

// How far outside the cursor a bounding sphere may fall and still be measured
// exactly. Generous, because the sphere estimate is only a broad phase.
constexpr float kBroadPhaseMargin = 32.0f;

// The camera the player is actually looking through. Camera.main is right in
// the usual case; an untagged game falls back to the first enabled camera that
// renders to the screen rather than to a texture.
Camera resolve_render_camera() {
    Camera camera = Camera::main();
    if (safe_object_alive(camera) && camera.enabled())
        return camera;

    Camera fallback{};
    float best_depth = 0.0f;
    const auto cameras = Object::FindObjectsOfTypeAllRooted<Camera>();
    for (const Camera &candidate : cameras) {
        if (!safe_object_alive(candidate) || !candidate.enabled())
            continue;
        // A camera with a target texture draws into a render texture, not onto
        // the screen the user just clicked.
        if (safe_object_alive(candidate.targetTexture()))
            continue;
        // Unity composites cameras in depth order, so the last one drawn is the
        // one whose image is on top.
        const float depth = candidate.depth();
        if (!fallback || depth >= best_depth) {
            fallback = candidate;
            best_depth = depth;
        }
    }
    return fallback;
}

struct ScreenSpace {
    Camera camera{};
    float height = 0.0f;
    float width = 0.0f;
    // The ray through the clicked pixel, in world space. World picking is a 3D
    // question and this is what answers it.
    URK::Unity::Ray view_ray{};
    bool view_ray_valid = false;
    // Pixels per world unit at one unit of distance, for the broad phase.
    float focal = 0.0f;
    bool orthographic = false;
    float orthographic_focal = 0.0f;
    Vector3 camera_position{};
};

// Unity reports screen coordinates from the bottom-left; the overlay draws from
// the top-left. One place does the flip.
Pick::ScreenPoint to_overlay(const ScreenSpace &space, Vector3 screen_point) {
    return {{screen_point.x, space.height - screen_point.y}, screen_point.z};
}

Pick::ScreenPoint project(const ScreenSpace &space, Camera camera, Vector3 world) {
    if (!camera)
        return {};
    return to_overlay(space, camera.WorldToScreenPoint(world));
}

std::string object_path(GameObject object) {
    if (!safe_object_alive(object))
        return {};
    std::string path = object.name();
    Transform transform = object.transform();
    if (!transform)
        return path;
    // Deep hierarchies exist; a path is a label, so stop before it becomes one.
    for (int depth = 0; depth < 24; ++depth) {
        transform = transform.parent();
        if (!safe_object_alive(transform))
            break;
        const GameObject parent = transform.gameObject();
        if (!safe_object_alive(parent))
            break;
        path = parent.name() + "/" + path;
    }
    return path;
}

bool object_visible(GameObject object, bool include_inactive) {
    if (!safe_object_alive(object))
        return false;
    return include_inactive || object.activeInHierarchy();
}

// ---------------------------------------------------------------------------
// World pass
// ---------------------------------------------------------------------------

// The screen rectangle of a world-space bounding box. Projecting all eight
// corners is what makes a long thin object pick correctly instead of behaving
// like the sphere around it.
Pick::ScreenRect bounds_screen_rect(const ScreenSpace &space, const URK::Unity::Bounds &bounds) {
    Vector3 corners[8]{};
    Pick::aabb_corners(bounds, corners);
    Pick::ScreenPoint points[8]{};
    for (int index = 0; index < 8; ++index)
        points[index] = project(space, space.camera, corners[index]);
    return Pick::with_minimum_size(Pick::enclosing_rect(points, 8));
}

// Cheap rejection before the eight-corner pass: how many pixels the object's
// bounding sphere could possibly cover, and whether the cursor is anywhere near
// it. One projection instead of eight, for every object in the scene.
bool near_cursor(const ScreenSpace &space, const URK::Unity::Bounds &bounds, Vector2 cursor, float *out_distance) {
    const Pick::ScreenPoint centre = project(space, space.camera, bounds.center);
    const float distance = (bounds.center - space.camera_position).magnitude();
    if (out_distance)
        *out_distance = distance;
    if (centre.depth <= 0.0f)
        return false;
    const float radius = bounds.extents.magnitude();
    float pixel_radius = 0.0f;
    if (space.orthographic)
        pixel_radius = radius * space.orthographic_focal;
    else if (distance > 0.001f)
        pixel_radius = radius / distance * space.focal;
    else
        return true;
    const float reach = pixel_radius + kBroadPhaseMargin;
    return std::fabs(centre.position.x - cursor.x) <= reach && std::fabs(centre.position.y - cursor.y) <= reach;
}

void collect_world(const ScreenSpace &space, Vector2 cursor, const Options &options, Result &result,
                   std::vector<Pick::Candidate> &candidates) {
    if (!space.camera)
        return;

    std::unordered_set<int> seen;
    const auto add = [&](GameObject object, const URK::Unity::Bounds &bounds, Pick::Source source,
                         std::string source_type) {
        if (!object_visible(object, options.include_inactive))
            return;
        const int instance_id = object.GetInstanceID();
        if (instance_id == 0 || !seen.insert(instance_id).second)
            return;
        float distance = 0.0f;
        if (!near_cursor(space, bounds, cursor, &distance))
            return;
        // The exact test. Testing the cursor against the projected rectangle
        // instead would accept everything in the neighbourhood of the object,
        // and ordering by distance to the box centre would rank a waterfall
        // that merely spans the view in front of the house under the cursor.
        if (space.view_ray_valid) {
            const float entry = Pick::ray_aabb_entry(space.view_ray.origin, space.view_ray.direction, bounds);
            if (entry < 0.0f)
                return;
            distance = entry;
        }
        const Pick::ScreenRect rect = bounds_screen_rect(space, bounds);
        // Without a ray there is nothing better than the projected rectangle.
        if (!space.view_ray_valid && !Pick::contains(rect, cursor))
            return;
        Pick::Candidate candidate{};
        candidate.instance_id = instance_id;
        candidate.object_address = reinterpret_cast<std::uintptr_t>(object.handle());
        candidate.name = object.name();
        candidate.layer = Pick::Layer::World;
        candidate.source = source;
        candidate.source_type = std::move(source_type);
        candidate.distance = distance;
        const Vector3 size = bounds.size();
        candidate.volume = std::fabs(size.x * size.y * size.z);
        candidate.rect = rect;
        candidates.push_back(std::move(candidate));
    };

    const auto renderers = Object::FindObjectsOfTypeAllRooted<Renderer>();
    for (const Renderer &renderer : renderers) {
        if (result.scanned_world >= options.max_world_objects)
            break;
        ++result.scanned_world;
        if (!safe_object_alive(renderer) || !renderer.enabled())
            continue;
        add(renderer.gameObject(), renderer.bounds(), Pick::Source::Renderer, safe_runtime_class_name(renderer));
    }

    // Colliders catch what renderers miss: triggers, volumes and any object
    // whose visible mesh lives on a child that was already claimed above.
    const auto colliders = Object::FindObjectsOfTypeAllRooted<Collider>();
    for (const Collider &collider : colliders) {
        if (result.scanned_world >= options.max_world_objects)
            break;
        ++result.scanned_world;
        if (!safe_object_alive(collider) || !collider.enabled())
            continue;
        add(collider.gameObject(), collider.bounds(), Pick::Source::Collider, safe_runtime_class_name(collider));
    }
}

// ---------------------------------------------------------------------------
// UI pass
// ---------------------------------------------------------------------------

// A canvas element's four corners in screen space. An overlay canvas needs no
// camera at all: its world coordinates are already screen pixels, which is why
// the UI cannot simply reuse the world pass.
bool canvas_element_rect(const ScreenSpace &space, Camera canvas_camera, bool overlay, RectTransform element,
                         Pick::ScreenRect *out) {
    const URK::Unity::Rect local = element.rect();
    if (!(local.width > 0.0f) && !(local.height > 0.0f))
        return false;
    Vector3 corners[4]{};
    Pick::rect_transform_corners(element.position(), element.rotation(), element.lossyScale(), local, corners);

    Pick::ScreenPoint points[4]{};
    for (int index = 0; index < 4; ++index) {
        if (overlay)
            points[index] = to_overlay(space, {corners[index].x, corners[index].y, 1.0f});
        else
            points[index] = project(space, canvas_camera, corners[index]);
    }
    // A flat quad that straddles the camera plane projects to nonsense, so it is
    // rejected rather than guessed at.
    const Pick::ScreenRect rect = Pick::enclosing_rect(points, 4, !overlay);
    if (!rect.valid)
        return false;
    *out = Pick::with_minimum_size(rect);
    return true;
}

void collect_canvas(const ScreenSpace &space, Canvas canvas, Vector2 cursor, const Options &options, Result &result,
                    std::vector<Pick::Candidate> &candidates) {
    const Transform root = canvas.transform();
    if (!safe_object_alive(root))
        return;
    const bool overlay = canvas.renderMode() == CanvasRenderMode::ScreenSpaceOverlay;
    Camera canvas_camera = overlay ? Camera{} : canvas.worldCamera();
    if (!overlay && !safe_object_alive(canvas_camera))
        canvas_camera = space.camera;
    if (!overlay && !canvas_camera)
        return;
    const int sort_order = canvas.sortingOrder();

    // Depth-first, in sibling order, so the running index is the order the
    // canvas paints its children: later siblings draw over earlier ones.
    struct Pending {
        Transform transform;
        int depth;
    };
    std::vector<Pending> stack;
    stack.push_back({root, 0});
    int draw_index = 0;
    while (!stack.empty()) {
        const Pending current = stack.back();
        stack.pop_back();
        if (result.scanned_ui >= options.max_ui_elements)
            return;
        if (!safe_object_alive(current.transform))
            continue;
        ++result.scanned_ui;
        const int index = draw_index++;

        const GameObject object = current.transform.gameObject();
        const bool visible = object_visible(object, options.include_inactive);
        // A RectTransform on its own draws nothing. Panels, layout groups and
        // marker objects would otherwise cover the whole screen and, because the
        // UI is painted over the world, swallow every click meant for the scene.
        // A CanvasRenderer is what Unity attaches to an element that actually
        // renders, so it is the honest test for "is this thing on screen".
        const bool draws = visible && safe_object_alive(object.GetComponent<CanvasRenderer>());
        if (draws) {
            Pick::ScreenRect rect{};
            const RectTransform element{current.transform.handle()};
            if (canvas_element_rect(space, canvas_camera, overlay, element, &rect) && Pick::contains(rect, cursor)) {
                Pick::Candidate candidate{};
                candidate.instance_id = object.GetInstanceID();
                candidate.object_address = reinterpret_cast<std::uintptr_t>(object.handle());
                candidate.name = object.name();
                candidate.layer = Pick::Layer::UI;
                candidate.source = Pick::Source::CanvasElement;
                candidate.source_type = overlay                                        ? "Canvas (Screen Space - Overlay)"
                                        : canvas.renderMode() == CanvasRenderMode::WorldSpace
                                            ? "Canvas (World Space)"
                                            : "Canvas (Screen Space - Camera)";
                candidate.sort_order = sort_order;
                candidate.draw_index = index;
                candidate.rect = rect;
                const float screen_area = space.width * space.height;
                candidate.backdrop =
                    screen_area > 1.0f && rect.area() > screen_area * Pick::kBackdropScreenFraction;
                candidates.push_back(std::move(candidate));
            }
        }

        // An inactive parent hides its whole subtree, so there is nothing under
        // the cursor below it unless inactive objects were asked for.
        if (!visible && !options.include_inactive)
            continue;
        const int child_count = current.transform.childCount();
        if (current.depth >= 32)
            continue;
        // Pushed in reverse so the stack pops them in sibling order.
        for (int child = child_count - 1; child >= 0; --child) {
            const Transform next = current.transform.GetChild(child);
            if (safe_object_alive(next))
                stack.push_back({next, current.depth + 1});
        }
    }
}

void collect_ui(const ScreenSpace &space, Vector2 cursor, const Options &options, Result &result,
                std::vector<Pick::Candidate> &candidates) {
    const auto canvases = Object::FindObjectsOfTypeAllRooted<Canvas>();
    for (const Canvas &canvas : canvases) {
        if (!safe_object_alive(canvas) || !canvas.enabled())
            continue;
        // A nested canvas is walked by its root, so starting from it as well
        // would report every child twice under a different sorting order.
        const Transform transform = canvas.transform();
        if (safe_object_alive(transform)) {
            const Transform parent = transform.parent();
            if (safe_object_alive(parent) && safe_object_alive(parent.GetComponentInParent<Canvas>(true)))
                continue;
        }
        collect_canvas(space, canvas, cursor, options, result, candidates);
        if (result.scanned_ui >= options.max_ui_elements)
            break;
    }
}

} // namespace

Result pick(Vector2 screen_point, const Options &options) {
    Result result{};

    ScreenSpace space{};
    space.camera = resolve_render_camera();
    space.height = options.screen_height;
    if (safe_object_alive(space.camera)) {
        result.camera_available = true;
        const GameObject camera_object = space.camera.gameObject();
        result.camera_name = safe_object_alive(camera_object) ? camera_object.name() : std::string("Camera");
        const Transform camera_transform = space.camera.transform();
        if (safe_object_alive(camera_transform))
            space.camera_position = camera_transform.position();
        // The overlay's own height is authoritative: a camera rendering into a
        // letterboxed viewport does not span the window the click came from.
        if (space.height <= 0.0f)
            space.height = static_cast<float>(space.camera.pixelHeight());
        space.width = static_cast<float>(space.camera.pixelWidth());
        space.orthographic = space.camera.orthographic();
        if (space.orthographic) {
            const float size = space.camera.orthographicSize();
            space.orthographic_focal = size > 0.001f ? (space.height * 0.5f) / size : 0.0f;
        } else {
            const float fov = space.camera.fieldOfView();
            const float half = fov * 0.5f * 0.017453292519943295f;
            const float tangent = std::tan(half);
            space.focal = tangent > 0.0001f ? (space.height * 0.5f) / tangent : 0.0f;
        }
        // ScreenPointToRay expects Unity's bottom-left screen space, so the
        // click is flipped back out of overlay coordinates.
        detail_clear_error();
        space.view_ray = space.camera.ScreenPointToRay({screen_point.x, space.height - screen_point.y, 0.0f});
        space.view_ray_valid = space.view_ray.direction.sqr_magnitude() > 1e-6f;
        if (!space.view_ray_valid)
            result.diagnostic = "Camera.ScreenPointToRay is not available in this build; world picking falls back "
                                "to bounding-box outlines and may be less precise.";
    } else {
        space.camera = Camera{};
    }

    std::vector<Pick::Candidate> candidates;
    if (options.include_ui)
        collect_ui(space, screen_point, options, result, candidates);
    if (options.include_world) {
        if (result.camera_available)
            collect_world(space, screen_point, options, result, candidates);
        else if (options.include_ui)
            result.diagnostic = "No rendering camera was found, so only Canvas elements could be picked.";
        else
            result.diagnostic = "No rendering camera was found, so nothing in the world could be picked.";
    }

    result.hits = Pick::rank(std::move(candidates), screen_point);
    for (Pick::Candidate &hit : result.hits)
        hit.path = object_path(GameObject{reinterpret_cast<void *>(hit.object_address)});

    if (result.hits.empty() && result.diagnostic.empty()) {
        result.diagnostic = result.scanned_ui == 0 && result.scanned_world == 0
                                ? "Nothing in the scene could be measured; the runtime reported no renderers or canvases."
                                : "Nothing is under that point.";
    }
    return result;
}

} // namespace Explorer::ScreenPicker

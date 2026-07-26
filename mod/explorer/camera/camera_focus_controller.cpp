// Copyright (c) 2026 Jadis0x. All rights reserved.
#include "camera_focus_controller.h"

#include "sdk/unity/unity.h"
#include "sdk/unity/unity_inspect.h"
#include "support/mod_log.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <type_traits>
#include <utility>

namespace Explorer::CameraFocus {
namespace {

using namespace URK::Unity;
namespace Inspect = URK::Unity::Inspect;
using Clock = std::chrono::steady_clock;

bool finite(Vector3 value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

bool finite(Quaternion value) {
    if (!std::isfinite(value.x) || !std::isfinite(value.y) ||
        !std::isfinite(value.z) || !std::isfinite(value.w))
        return false;
    const float squared_length =
        value.x * value.x + value.y * value.y + value.z * value.z + value.w * value.w;
    return std::isfinite(squared_length) && squared_length > 0.0001f;
}

Vector3 normalized_or(Vector3 value, Vector3 fallback) {
    const float length = value.magnitude();
    return std::isfinite(length) && length > 0.0001f ? value / length : fallback;
}

#if defined(_WIN32)
int camera_exception_filter(unsigned long code) noexcept {
    return code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_IN_PAGE_ERROR
        ? EXCEPTION_EXECUTE_HANDLER
        : EXCEPTION_CONTINUE_SEARCH;
}

bool invoke_native_guarded(void (*operation)(void*), void* context) noexcept {
    __try {
        operation(context);
        return true;
    }
    __except (camera_exception_filter(GetExceptionCode())) {
        return false;
    }
}
#endif

template <class Operation>
bool guarded(const char* stage, std::string& error, Operation&& operation) {
    URK::Unity::clear_error();
#if defined(_WIN32)
    auto invoke = [](void* context) {
        (*static_cast<std::remove_reference_t<Operation>*>(context))();
    };
    if (!invoke_native_guarded(invoke, &operation)) {
        error = std::string("Camera focus native fault during ") + stage;
        ModLog::error("%s", error.c_str());
        URK::Unity::clear_error();
        return false;
    }
#else
    operation();
#endif
    if (const char* runtime_error = URK::Unity::last_error();
        runtime_error && runtime_error[0]) {
        error = std::string("Camera focus failed during ") + stage + ": " + runtime_error;
        ModLog::error("%s", error.c_str());
        URK::Unity::clear_error();
        return false;
    }
    return true;
}

template <class Value, class Reader>
bool read_optional(const char* stage, Value& value, Reader&& reader) {
    std::string error;
    Value candidate = value;
    if (!guarded(stage, error, [&] { candidate = reader(); }))
        return false;
    value = std::move(candidate);
    return true;
}

bool alive(Object object) {
    if (!object)
        return false;
    std::string ignored;
    bool result = false;
    return guarded("object lifetime check", ignored, [&] { result = object.alive(); }) && result;
}

struct HandleOwner {
    Inspect::ObjectHandle value{};

    HandleOwner() = default;
    explicit HandleOwner(Inspect::ObjectHandle handle) : value(handle) {}
    HandleOwner(const HandleOwner&) = delete;
    HandleOwner& operator=(const HandleOwner&) = delete;
    HandleOwner(HandleOwner&& other) noexcept : value(std::exchange(other.value, {})) {}
    HandleOwner& operator=(HandleOwner&& other) noexcept {
        if (this != &other) {
            reset();
            value = std::exchange(other.value, {});
        }
        return *this;
    }
    ~HandleOwner() { reset(); }

    void reset() {
        if (value.handle)
            Inspect::FreeObjectHandle(value);
        value = {};
    }

    Inspect::ObjectHandle release() {
        return std::exchange(value, {});
    }
};

struct FocusBounds {
    Vector3 center{};
    float radius = 1.0f;
};

FocusBounds measure_target(GameObject target, Vector3 target_position) {
    FocusBounds result{target_position, 1.0f};
    std::string ignored;
    detail::RootedObjectArray<Renderer> renderers;
    if (!guarded("renderer discovery", ignored, [&] {
            renderers = target.GetComponentsInChildrenRooted<Renderer>(true);
        }))
        return result;

    bool has_bounds = false;
    Vector3 minimum{};
    Vector3 maximum{};
    for (const Renderer& renderer : renderers) {
        if (!alive(renderer))
            continue;
        Bounds bounds{};
        if (!guarded("renderer bounds", ignored, [&] { bounds = renderer.bounds(); }))
            continue;
        const Vector3 current_min = bounds.min();
        const Vector3 current_max = bounds.max();
        if (!finite(current_min) || !finite(current_max))
            continue;
        if (!has_bounds) {
            minimum = current_min;
            maximum = current_max;
            has_bounds = true;
        } else {
            minimum.x = std::min(minimum.x, current_min.x);
            minimum.y = std::min(minimum.y, current_min.y);
            minimum.z = std::min(minimum.z, current_min.z);
            maximum.x = std::max(maximum.x, current_max.x);
            maximum.y = std::max(maximum.y, current_max.y);
            maximum.z = std::max(maximum.z, current_max.z);
        }
    }
    if (!has_bounds)
        return result;

    result.center = (minimum + maximum) * 0.5f;
    result.radius = std::max(0.25f, (maximum - minimum).magnitude() * 0.5f);
    return result;
}

double camera_score(Camera camera, int target_layer, bool target_layer_known,
                    std::string& error) {
    bool enabled = false;
    GameObject game_object{};
    int mask = -1;
    int width = 0;
    int height = 0;
    int display = 0;
    float depth = 0.0f;
    Object target_texture{};
    bool active_in_hierarchy = false;
    if (!guarded("camera availability", error, [&] {
            enabled = camera.enabled();
            game_object = camera.gameObject();
            active_in_hierarchy =
                game_object && game_object.alive() && game_object.activeInHierarchy();
        }))
        return -std::numeric_limits<double>::infinity();
    if (!enabled || !active_in_hierarchy)
        return -std::numeric_limits<double>::infinity();

    const bool mask_known =
        read_optional("camera culling mask", mask, [&] { return camera.cullingMask(); });
    read_optional("camera pixel width", width, [&] { return camera.pixelWidth(); });
    read_optional("camera pixel height", height, [&] { return camera.pixelHeight(); });
    read_optional("camera target display", display, [&] { return camera.targetDisplay(); });
    read_optional("camera depth", depth, [&] { return camera.depth(); });
    read_optional("camera target texture", target_texture, [&] { return camera.targetTexture(); });

    if (target_layer_known && mask_known && target_layer >= 0 && target_layer < 32 &&
        (static_cast<unsigned>(mask) & (1u << static_cast<unsigned>(target_layer))) == 0)
        return -std::numeric_limits<double>::infinity();

    const double area = static_cast<double>(std::max(1, width)) *
                        static_cast<double>(std::max(1, height));
    const double display_bonus = display == 0 ? 1.0e12 : 0.0;
    const double screen_bonus = target_texture ? 0.0 : 5.0e11;
    return display_bonus + screen_bonus + area * 100.0 + static_cast<double>(depth);
}

Camera select_camera(GameObject target, HandleOwner& camera_owner, std::string& error) {
    int target_layer = 0;
    std::string layer_error;
    const bool target_layer_known = guarded(
        "target layer lookup", layer_error,
        [&] { target_layer = target.GetProperty<int>("layer"); });

    Camera main{};
    std::string main_error;
    if (guarded("Camera.main lookup", main_error, [&] { main = Camera::main(); }) &&
        alive(main)) {
        std::string score_error;
        if (std::isfinite(camera_score(
                main, target_layer, target_layer_known, score_error))) {
            camera_owner = HandleOwner{Inspect::PinObject(Object{main.handle()})};
            if (camera_owner.value.handle) {
                Camera rooted{
                    Inspect::ResolveObjectHandle(camera_owner.value).handle()};
                if (alive(rooted)) {
                    error.clear();
                    return rooted;
                }
            }
            camera_owner.reset();
        }
    }

    detail::RootedObjectArray<Camera> candidates;
    if (!guarded("camera discovery", error, [&] {
            candidates = Object::FindObjectsOfTypeRooted<Camera>();
        }))
        return {};

    Camera best{};
    double best_score = -std::numeric_limits<double>::infinity();
    for (const Camera& candidate : candidates) {
        if (!alive(candidate))
            continue;
        std::string score_error;
        const double score = camera_score(
            candidate, target_layer, target_layer_known, score_error);
        if (score > best_score) {
            best = candidate;
            best_score = score;
        }
    }
    if (!best) {
        error = "Camera focus failed: no enabled gameplay camera renders the target layer";
        return {};
    }
    camera_owner = HandleOwner{Inspect::PinObject(Object{best.handle()})};
    if (!camera_owner.value.handle) {
        error = "Camera focus failed: selected camera could not be rooted";
        return {};
    }
    Camera rooted{Inspect::ResolveObjectHandle(camera_owner.value).handle()};
    if (!alive(rooted)) {
        error = "Camera focus failed: rooted camera became unavailable";
        camera_owner.reset();
        return {};
    }
    error.clear();
    return rooted;
}

} // namespace

struct Controller::Impl {
    Settings settings{};
    Inspect::ObjectHandle camera_handle{};
    Inspect::ObjectHandle target_handle{};
    Inspect::ObjectHandle original_parent_handle{};
    Vector3 saved_world_position{};
    Quaternion saved_world_rotation{};
    Vector3 saved_local_position{};
    Quaternion saved_local_rotation{};
    float saved_orthographic_size = 0.0f;
    bool saved_orthographic = false;
    Vector3 target_center_offset{};
    float target_radius = 1.0f;
    Vector3 view_back{0.0f, 0.0f, -1.0f};
    Vector3 horizontal_back{0.0f, 0.0f, -1.0f};
    Vector3 transition_start{};
    Clock::time_point transition_started{};
    bool is_active = false;

    void release_handles() {
        Inspect::FreeObjectHandle(original_parent_handle);
        original_parent_handle = {};
        Inspect::FreeObjectHandle(camera_handle);
        camera_handle = {};
        Inspect::FreeObjectHandle(target_handle);
        target_handle = {};
    }

    Vector3 focus_offset(Camera camera) const {
        if (settings.top_down)
            return horizontal_back * settings.top_down_tilt +
                   Vector3{0.0f, settings.distance, 0.0f};

        float fitted_distance = settings.distance;
        if (!saved_orthographic) {
            std::string ignored;
            float field_of_view = 60.0f;
            float aspect = 1.0f;
            float near_clip = 0.3f;
            guarded("camera framing properties", ignored, [&] {
                field_of_view = camera.fieldOfView();
                aspect = camera.aspect();
                near_clip = camera.nearClipPlane();
            });
            constexpr float pi = 3.14159265358979323846f;
            const float vertical_half = std::clamp(field_of_view, 1.0f, 179.0f) * pi / 360.0f;
            const float horizontal_half = std::atan(std::tan(vertical_half) * std::max(0.1f, aspect));
            const float limiting_half = std::max(0.01f, std::min(vertical_half, horizontal_half));
            const float bounds_distance = target_radius / std::tan(limiting_half);
            fitted_distance = std::max(fitted_distance,
                                       bounds_distance * 1.2f + target_radius + std::max(0.0f, near_clip));
        }
        return view_back * fitted_distance;
    }
};

Controller::Controller() : impl_(std::make_unique<Impl>()) {}

Controller::~Controller() {
    if (impl_ && impl_->is_active) {
        std::string ignored;
        stop(ignored);
    }
}

bool Controller::start(GameObject target, std::string& error) {
    error.clear();
    if (!alive(target)) {
        error = "Camera focus failed: target is no longer available";
        return false;
    }

    Transform target_transform{};
    Vector3 target_position{};
    if (!guarded("target transform lookup", error, [&] {
            target_transform = target.transform();
            target_position = target_transform.position();
        }) || !alive(target_transform) || !finite(target_position)) {
        if (error.empty())
            error = "Camera focus failed: target Transform is unavailable";
        return false;
    }

    HandleOwner camera_owner;
    Camera camera = select_camera(target, camera_owner, error);
    if (!alive(camera))
        return false;

    HandleOwner target_owner{Inspect::PinObject(Object{target.handle()})};
    if (!target_owner.value.handle) {
        error = "Camera focus failed: target could not be rooted";
        return false;
    }
    GameObject rooted_target{Inspect::ResolveObjectHandle(target_owner.value).handle()};
    Camera rooted_camera{Inspect::ResolveObjectHandle(camera_owner.value).handle()};
    Transform camera_transform{};
    Transform rooted_target_transform{};
    if (!guarded("rooted focus objects", error, [&] {
            rooted_target_transform = rooted_target.transform();
            camera_transform = rooted_camera.transform();
        }) || !alive(rooted_target_transform) || !alive(camera_transform)) {
        if (error.empty())
            error = "Camera focus failed: rooted camera or target Transform is unavailable";
        return false;
    }

    if (impl_->is_active) {
        std::string restore_error;
        if (!stop(restore_error)) {
            error = "Camera focus failed: previous camera session could not be restored: " + restore_error;
            return false;
        }
    }

    Vector3 world_position{};
    Quaternion world_rotation{};
    Vector3 local_position{};
    Quaternion local_rotation{};
    Vector3 forward{};
    Transform parent{};
    bool orthographic = false;
    float orthographic_size = 0.0f;
    if (!guarded("camera pose capture", error, [&] {
            world_position = camera_transform.position();
            world_rotation = camera_transform.rotation();
            local_position = camera_transform.localPosition();
            local_rotation = camera_transform.localRotation();
            forward = camera_transform.forward();
            parent = camera_transform.parent();
            orthographic = rooted_camera.orthographic();
            orthographic_size = orthographic ? rooted_camera.orthographicSize() : 0.0f;
        }) || !finite(world_position) || !finite(world_rotation) ||
             !finite(local_position) || !finite(local_rotation)) {
        if (error.empty())
            error = "Camera focus failed: camera pose is invalid";
        return false;
    }

    HandleOwner parent_owner;
    if (alive(parent)) {
        parent_owner = HandleOwner{Inspect::PinObject(Object{parent.handle()})};
        if (!parent_owner.value.handle) {
            error = "Camera focus failed: camera parent could not be rooted";
            return false;
        }
    }

    const FocusBounds bounds = measure_target(rooted_target, target_position);
    impl_->camera_handle = camera_owner.release();
    impl_->target_handle = target_owner.release();
    impl_->original_parent_handle = parent_owner.release();
    impl_->saved_world_position = world_position;
    impl_->saved_world_rotation = world_rotation;
    impl_->saved_local_position = local_position;
    impl_->saved_local_rotation = local_rotation;
    impl_->saved_orthographic = orthographic;
    impl_->saved_orthographic_size = orthographic_size;
    impl_->target_center_offset = bounds.center - target_position;
    impl_->target_radius = bounds.radius;
    impl_->view_back = normalized_or(-forward, Vector3{0.0f, 0.0f, -1.0f});
    impl_->horizontal_back = normalized_or(
        Vector3{impl_->view_back.x, 0.0f, impl_->view_back.z},
        Vector3{0.0f, 0.0f, -1.0f});
    impl_->transition_start = world_position;
    impl_->transition_started = Clock::now();
    impl_->is_active = true;

    if (orthographic) {
        const float desired_size = std::max(bounds.radius * 1.2f, impl_->settings.distance * 0.5f);
        if (!guarded("orthographic framing", error,
                     [&] { rooted_camera.set_orthographicSize(desired_size); })) {
            std::string restore_error;
            stop(restore_error);
            return false;
        }
    }

    if (!update(error)) {
        std::string restore_error;
        stop(restore_error);
        return false;
    }
    return true;
}

bool Controller::update(std::string& error) {
    error.clear();
    if (!impl_->is_active)
        return true;

    Camera camera{Inspect::ResolveObjectHandle(impl_->camera_handle).handle()};
    GameObject target{Inspect::ResolveObjectHandle(impl_->target_handle).handle()};
    const bool camera_alive = alive(camera);
    const bool target_alive = alive(target);
    if (!camera_alive || !target_alive) {
        const std::string reason = !camera_alive
            ? "Camera focus ended: camera was destroyed"
            : "Camera focus ended: target was destroyed";
        std::string restore_error;
        stop(restore_error);
        error = restore_error.empty() ? reason : reason + "; " + restore_error;
        return false;
    }

    Transform camera_transform{};
    Transform target_transform{};
    Vector3 target_position{};
    if (!guarded("focus update lookup", error, [&] {
            camera_transform = camera.transform();
            target_transform = target.transform();
            target_position = target_transform.position();
        }) || !alive(camera_transform) || !alive(target_transform) || !finite(target_position)) {
        if (error.empty())
            error = "Camera focus update failed: camera or target Transform is unavailable";
        return false;
    }

    const Vector3 center = target_position + impl_->target_center_offset;
    const Vector3 desired = center + impl_->focus_offset(camera);
    const float duration = std::max(0.0f, impl_->settings.transition_seconds);
    const float elapsed = std::chrono::duration<float>(Clock::now() - impl_->transition_started).count();
    const float linear = duration > 0.0f ? std::clamp(elapsed / duration, 0.0f, 1.0f) : 1.0f;
    const float smooth = linear * linear * (3.0f - 2.0f * linear);
    const Vector3 position = impl_->transition_start * (1.0f - smooth) + desired * smooth;

    return guarded("camera pose update", error, [&] {
        camera_transform.set_position(position);
        camera_transform.LookAt(center);
        if (impl_->saved_orthographic) {
            const float desired_size =
                std::max(impl_->target_radius * 1.2f, impl_->settings.distance * 0.5f);
            camera.set_orthographicSize(desired_size);
        }
    });
}

bool Controller::stop(std::string& error) {
    error.clear();
    if (!impl_->is_active && !impl_->camera_handle.handle) {
        impl_->release_handles();
        return true;
    }

    bool restored = false;
    Camera camera{Inspect::ResolveObjectHandle(impl_->camera_handle).handle()};
    if (alive(camera)) {
        Transform transform{};
        if (guarded("camera restore lookup", error, [&] { transform = camera.transform(); }) &&
            alive(transform)) {
            Transform current_parent{};
            guarded("camera parent restore check", error, [&] { current_parent = transform.parent(); });
            Transform original_parent{};
            if (impl_->original_parent_handle.handle)
                original_parent = Transform{
                    Inspect::ResolveObjectHandle(impl_->original_parent_handle).handle()};
            const bool same_parent =
                (!current_parent && !original_parent) ||
                (current_parent && original_parent &&
                 current_parent.handle() == original_parent.handle());
            if (same_parent) {
                restored = guarded("camera local pose restore", error, [&] {
                    transform.set_localPosition(impl_->saved_local_position);
                    transform.set_localRotation(impl_->saved_local_rotation);
                });
            }
            if (!restored) {
                error.clear();
                restored = guarded("camera world pose restore", error, [&] {
                    transform.set_position(impl_->saved_world_position);
                    transform.set_rotation(impl_->saved_world_rotation);
                });
            }
            if (impl_->saved_orthographic) {
                std::string orthographic_error;
                const bool size_restored = guarded("orthographic size restore", orthographic_error, [&] {
                    camera.set_orthographicSize(impl_->saved_orthographic_size);
                });
                restored = restored && size_restored;
                if (!size_restored && error.empty())
                    error = orthographic_error;
            }
        }
    }
    if (!restored && error.empty())
        error = "original camera is unavailable";

    impl_->is_active = false;
    impl_->release_handles();
    return restored;
}

bool Controller::active() const {
    return impl_->is_active;
}

const Settings& Controller::settings() const {
    return impl_->settings;
}

void Controller::set_settings(Settings settings) {
    settings.distance = std::clamp(settings.distance, 1.0f, 100.0f);
    settings.top_down_tilt = std::clamp(settings.top_down_tilt, 0.0f, 100.0f);
    settings.transition_seconds = std::clamp(settings.transition_seconds, 0.0f, 5.0f);
    impl_->settings = settings;
}

void Controller::abandon_after_native_fault() {
    impl_->camera_handle = {};
    impl_->target_handle = {};
    impl_->original_parent_handle = {};
    impl_->is_active = false;
}

} // namespace Explorer::CameraFocus

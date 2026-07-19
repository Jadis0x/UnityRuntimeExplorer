#pragma once
#include "unity_invoke.h"

namespace URK::Unity {
// URK_UNITY_SHORTCUTS_BEGIN
namespace Debug {
inline void Log(std::string_view message) { detail::InvokeStatic<void>(DebugType, "Log", message); }
inline void LogWarning(std::string_view message) { detail::InvokeStatic<void>(DebugType, "LogWarning", message); }
inline void LogError(std::string_view message) { detail::InvokeStatic<void>(DebugType, "LogError", message); }
}
namespace Screen {
inline int width() { return detail::InvokeStatic<int>(ScreenType, "get_width"); }
inline int height() { return detail::InvokeStatic<int>(ScreenType, "get_height"); }
inline float dpi() { return detail::InvokeStatic<float>(ScreenType, "get_dpi"); }
}
namespace ObjectFilter {
inline constexpr int kHideFlagsHierarchyMask = 1;
inline constexpr int kHideFlagsImplicitRuntimeMask = 4 | 16 | 32;
inline bool has(std::uint32_t flags, ObjectFilterFlags flag) {
    return (flags & static_cast<std::uint32_t>(flag)) != 0;
}
inline bool include_scene(Scene scene, std::uint32_t flags = static_cast<std::uint32_t>(ObjectFilterFlags::None)) {
    if (!scene || !scene.IsValid() || !scene.isLoaded())
        return false;
    if (!has(flags, ObjectFilterFlags::IncludeDontDestroyOnLoad) && scene.isDontDestroyOnLoad())
        return false;
    return true;
}
inline bool include_game_object(GameObject object, std::uint32_t flags = static_cast<std::uint32_t>(ObjectFilterFlags::None)) {
    if (!object || !object.alive())
        return false;
    if (!has(flags, ObjectFilterFlags::IncludeInactive) && !object.activeInHierarchy())
        return false;
    const int hide_flags = object.hideFlags();
    if (!has(flags, ObjectFilterFlags::IncludeHidden) &&
        (hide_flags & (kHideFlagsHierarchyMask | kHideFlagsImplicitRuntimeMask)) != 0) {
        return false;
    }
    return include_scene(object.scene(), flags);
}
inline std::vector<GameObject> canonicalize_game_objects(const std::vector<GameObject>& objects,
    std::uint32_t flags = static_cast<std::uint32_t>(ObjectFilterFlags::None)) {
    std::vector<GameObject> filtered;
    filtered.reserve(objects.size());
    std::unordered_set<void*> seen;
    for (const GameObject& object : objects) {
        void* handle = object.handle();
        if (!handle || !seen.insert(handle).second)
            continue;
        if (include_game_object(object, flags))
            filtered.push_back(object);
    }
    return filtered;
}
}
namespace SceneManager {
inline TypeRef type() { return {"", "UnityEngine.SceneManagement", "SceneManager"}; }
inline Scene GetActiveScene() {
    return Scene{detail::InvokeStatic<void*>(type(), "GetActiveScene")};
}
inline int sceneCount() {
    return detail::InvokeStatic<int>(type(), "get_sceneCount");
}
inline Scene GetSceneAt(int index) {
    return Scene{detail::InvokeStatic<void*>(type(), "GetSceneAt", index)};
}
inline std::vector<Scene> GetLoadedScenes() {
    std::vector<Scene> scenes;
    const int count = sceneCount();
    if (count <= 0)
        return scenes;
    scenes.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        Scene scene = GetSceneAt(i);
        if (scene && scene.IsValid())
            scenes.push_back(scene);
    }
    return scenes;
}
inline std::vector<GameObject> GetLoadedSceneRootsFiltered(
    std::uint32_t filterFlags = static_cast<std::uint32_t>(ObjectFilterFlags::None)) {
    std::vector<GameObject> roots;
    for (const Scene& scene : GetLoadedScenes()) {
        if (!ObjectFilter::include_scene(scene, filterFlags))
            continue;
        std::vector<GameObject> sceneRoots = scene.GetRootGameObjects();
        roots.insert(roots.end(), sceneRoots.begin(), sceneRoots.end());
    }
    return ObjectFilter::canonicalize_game_objects(roots, filterFlags);
}
inline std::vector<GameObject> GetLoadedSceneRoots() {
    return GetLoadedSceneRootsFiltered();
}
inline std::vector<GameObject> FindSceneGameObjectsFiltered(
    std::uint32_t filterFlags = static_cast<std::uint32_t>(ObjectFilterFlags::None)) {
    const bool includeInactive = ObjectFilter::has(filterFlags, ObjectFilterFlags::IncludeInactive);
    std::vector<GameObject> objects;
    if (!includeInactive)
        objects = Object::FindObjectsByType<GameObject>("", "UnityEngine", "GameObject", FindObjectsSortMode::None);
    else
        objects = Object::FindObjectsOfTypeAll<GameObject>("", "UnityEngine", "GameObject");
    return ObjectFilter::canonicalize_game_objects(objects, filterFlags);
}
inline std::vector<GameObject> FindSceneGameObjects(bool includeInactive = true) {
    return FindSceneGameObjectsFiltered(
        includeInactive ? static_cast<std::uint32_t>(ObjectFilterFlags::IncludeInactive)
                        : static_cast<std::uint32_t>(ObjectFilterFlags::None));
}
}
namespace Time {
inline float time() { return detail::InvokeStatic<float>(TimeType, "get_time"); }
inline float deltaTime() { return detail::InvokeStatic<float>(TimeType, "get_deltaTime"); }
inline float unscaledDeltaTime() { return detail::InvokeStatic<float>(TimeType, "get_unscaledDeltaTime"); }
inline float timeScale() { return detail::InvokeStatic<float>(TimeType, "get_timeScale"); }
inline void set_timeScale(float value) { detail::InvokeStatic<void>(TimeType, "set_timeScale", value); }
}
namespace Input {
inline bool available() { return URK::has_input(); }
inline bool GetKey(int keyCode) { return URK::input_get_key(keyCode); }
inline bool GetKeyDown(int keyCode) { return URK::input_get_key_down(keyCode); }
inline bool GetKeyUp(int keyCode) { return URK::input_get_key_up(keyCode); }
inline bool GetMouseButton(int button) { return URK::input_get_mouse_button(button); }
inline bool GetMouseButtonDown(int button) { return URK::input_get_mouse_button_down(button); }
inline bool GetMouseButtonUp(int button) { return URK::input_get_mouse_button_up(button); }
inline bool GetKey(KeyCode keyCode) { return GetKey(static_cast<int>(keyCode)); }
inline bool GetKeyDown(KeyCode keyCode) { return GetKeyDown(static_cast<int>(keyCode)); }
inline bool GetKeyUp(KeyCode keyCode) { return GetKeyUp(static_cast<int>(keyCode)); }
inline bool GetMouseButton(MouseButton button) { return GetMouseButton(static_cast<int>(button)); }
inline bool GetMouseButtonDown(MouseButton button) { return GetMouseButtonDown(static_cast<int>(button)); }
inline bool GetMouseButtonUp(MouseButton button) { return GetMouseButtonUp(static_cast<int>(button)); }
}
inline Vector2 screen_size(Camera camera = {}) {
    int width_value = Screen::width();
    int height_value = Screen::height();
    if ((width_value <= 0 || height_value <= 0) && static_cast<bool>(camera)) {
        const int pixel_width = camera.pixelWidth();
        const int pixel_height = camera.pixelHeight();
        if (pixel_width > 0) width_value = pixel_width;
        if (pixel_height > 0) height_value = pixel_height;
    }
    return {
        static_cast<float>(width_value > 0 ? width_value : 0),
        static_cast<float>(height_value > 0 ? height_value : 0)
    };
}
inline Vector2 screen_center(Camera camera = {}) {
    const Vector2 size = screen_size(camera);
    return {size.x * 0.5f, size.y * 0.5f};
}
inline bool screen_contains(Vector2 point, float padding = 0.0f, Camera camera = {}) {
    const Vector2 size = screen_size(camera);
    return size.x > 0.0f && size.y > 0.0f &&
           point.x >= padding && point.y >= padding &&
           point.x <= (size.x - padding) &&
           point.y <= (size.y - padding);
}
inline Vector2 clamp_to_screen(Vector2 point, float padding = 0.0f, Camera camera = {}) {
    const Vector2 size = screen_size(camera);
    const float max_x = size.x > 0.0f ? std::max(padding, size.x - padding) : padding;
    const float max_y = size.y > 0.0f ? std::max(padding, size.y - padding) : padding;
    return {
        std::clamp(point.x, padding, max_x),
        std::clamp(point.y, padding, max_y)
    };
}
inline Vector2 direction_to_screen_edge(Vector2 direction, float padding = 24.0f, Camera camera = {}) {
    const Vector2 size = screen_size(camera);
    const Vector2 center = screen_center(camera);
    if (size.x <= 0.0f || size.y <= 0.0f)
        return center;

    Vector2 dir = direction.normalized();
    if (dir.nearly_zero())
        dir = {0.0f, -1.0f};

    const float half_w = std::max(1.0f, size.x * 0.5f - padding);
    const float half_h = std::max(1.0f, size.y * 0.5f - padding);
    float tx = std::numeric_limits<float>::infinity();
    float ty = std::numeric_limits<float>::infinity();
    if (std::fabs(dir.x) > 0.000001f)
        tx = half_w / std::fabs(dir.x);
    if (std::fabs(dir.y) > 0.000001f)
        ty = half_h / std::fabs(dir.y);

    const float scale = std::min(tx, ty);
    return {center.x + dir.x * scale, center.y + dir.y * scale};
}
inline ProjectionResult project_world(Camera camera, Vector3 world, float edge_padding = 24.0f) {
    ProjectionResult result{};
    result.world = world;
    result.screen_center = screen_center(camera);
    if (!static_cast<bool>(camera))
        return result;

    const Vector2 size = screen_size(camera);
    if (size.x <= 0.0f || size.y <= 0.0f)
        return result;

    const Transform camera_transform = camera.transform();
    Vector3 camera_position{};
    Vector3 camera_forward{0.0f, 0.0f, 1.0f};
    Vector3 camera_right{1.0f, 0.0f, 0.0f};
    Vector3 camera_up{0.0f, 1.0f, 0.0f};
    bool have_basis = false;
    if (static_cast<bool>(camera_transform)) {
        camera_position = camera_transform.position();
        camera_forward = camera_transform.forward().normalized();
        camera_right = camera_transform.right().normalized();
        camera_up = camera_transform.up().normalized();
        have_basis = true;
    }

    const Vector3 offset = world - camera_position;
    result.distance = offset.magnitude();
    if (have_basis && result.distance > 0.000001f)
        result.facing = Vector3::dot(offset / result.distance, camera_forward);

    result.screen3 = camera.WorldToScreenPoint(world);
    result.viewport = camera.WorldToViewportPoint(world);
    result.depth = result.screen3.z;
    result.in_front = result.depth > 0.01f;
    result.screen = {result.screen3.x, size.y - result.screen3.y};
    result.on_screen = result.in_front &&
                       result.viewport.x >= 0.0f && result.viewport.x <= 1.0f &&
                       result.viewport.y >= 0.0f && result.viewport.y <= 1.0f;

    Vector2 direction = result.screen - result.screen_center;
    if (!result.on_screen && have_basis && result.distance > 0.000001f) {
        const Vector3 offset_dir = offset / result.distance;
        direction = {
            Vector3::dot(offset_dir, camera_right),
            -Vector3::dot(offset_dir, camera_up)
        };
        if (!result.in_front)
            direction *= -1.0f;
    } else if (!result.in_front) {
        direction *= -1.0f;
    }

    if (direction.nearly_zero())
        direction = {0.0f, -1.0f};

    result.direction = direction.normalized();
    result.clamped_screen = direction_to_screen_edge(result.direction, edge_padding, camera);
    result.valid = true;
    return result;
}
inline ProjectionResult project_world(Vector3 world, float edge_padding = 24.0f) {
    return project_world(Camera::main(), world, edge_padding);
}
inline ProjectionResult project_transform(Camera camera, Transform transform, float edge_padding = 24.0f) {
    return static_cast<bool>(transform)
        ? project_world(camera, transform.position(), edge_padding)
        : ProjectionResult{};
}
inline ProjectionResult project_transform(Transform transform, float edge_padding = 24.0f) {
    return project_transform(Camera::main(), transform, edge_padding);
}
inline bool world_to_overlay(Camera camera, Vector3 world, Vector2* out) {
    if (!out)
        return false;
    const ProjectionResult projection = project_world(camera, world, 0.0f);
    if (!projection.on_screen)
        return false;
    *out = projection.screen;
    return true;
}
inline bool world_to_overlay(Vector3 world, Vector2* out) {
    return world_to_overlay(Camera::main(), world, out);
}
inline bool world_visible(Camera camera, Vector3 world, float min_facing = 0.01f) {
    const ProjectionResult projection = project_world(camera, world, 0.0f);
    return projection.on_screen && projection.facing >= min_facing;
}
inline bool world_visible(Vector3 world, float min_facing = 0.01f) {
    return world_visible(Camera::main(), world, min_facing);
}

}
// URK_UNITY_ALIASES_BEGIN
namespace Unity {
using AudioSource = URK::Unity::AudioSource;
using AssetBundle = URK::Unity::AssetBundle;
using Behaviour = URK::Unity::Behaviour;
using Button = URK::Unity::Button;
using ButtonTransition = URK::Unity::ButtonTransition;
using Animator = URK::Unity::Animator;
using AnimatorCullingMode = URK::Unity::AnimatorCullingMode;
using AnimatorUpdateMode = URK::Unity::AnimatorUpdateMode;
using Bounds = URK::Unity::Bounds;
using Camera = URK::Unity::Camera;
using Canvas = URK::Unity::Canvas;
using CanvasRoot = URK::Unity::CanvasRoot;
using CanvasRenderMode = URK::Unity::CanvasRenderMode;
using CanvasRenderer = URK::Unity::CanvasRenderer;
using CanvasGroup = URK::Unity::CanvasGroup;
using CanvasScaler = URK::Unity::CanvasScaler;
using CanvasScaleMode = URK::Unity::CanvasScaleMode;
using CanvasScreenMatchMode = URK::Unity::CanvasScreenMatchMode;
using Collider = URK::Unity::Collider;
using Color = URK::Unity::Color;
using Color32 = URK::Unity::Color32;
using Component = URK::Unity::Component;
using ContentSizeFitter = URK::Unity::ContentSizeFitter;
using ContentSizeFitterFitMode = URK::Unity::ContentSizeFitterFitMode;
using CursorLockState = URK::CursorLockState;
using CursorState = URK::CursorState;
using DiagnosticSink = URK::Unity::DiagnosticSink;
using Dropdown = URK::Unity::Dropdown;
using EventSystem = URK::Unity::EventSystem;
using FindObjectsSortMode = URK::Unity::FindObjectsSortMode;
using FontStyle = URK::Unity::FontStyle;
using GameObject = URK::Unity::GameObject;
using Graphic = URK::Unity::Graphic;
using GraphicRaycaster = URK::Unity::GraphicRaycaster;
using GraphicRaycasterBlockingObjects = URK::Unity::GraphicRaycasterBlockingObjects;
using GridLayoutAxis = URK::Unity::GridLayoutAxis;
using GridLayoutConstraint = URK::Unity::GridLayoutConstraint;
using GridLayoutCorner = URK::Unity::GridLayoutCorner;
using GridLayoutGroup = URK::Unity::GridLayoutGroup;
using HorizontalLayoutGroup = URK::Unity::HorizontalLayoutGroup;
using Image = URK::Unity::Image;
using ImageFillMethod = URK::Unity::ImageFillMethod;
using ImageType = URK::Unity::ImageType;
using InputField = URK::Unity::InputField;
using InputFieldContentType = URK::Unity::InputFieldContentType;
using InputFieldLineType = URK::Unity::InputFieldLineType;
using InputSystemUIInputModule = URK::Unity::InputSystemUIInputModule;
using KeyCode = URK::Unity::KeyCode;
using LightProbeUsage = URK::Unity::LightProbeUsage;
using Light = URK::Unity::Light;
using LightRenderMode = URK::Unity::LightRenderMode;
using LightShadowResolution = URK::Unity::LightShadowResolution;
using LightShadows = URK::Unity::LightShadows;
using LightType = URK::Unity::LightType;
using LayoutElement = URK::Unity::LayoutElement;
using LayoutRebuilder = URK::Unity::LayoutRebuilder;
using Material = URK::Unity::Material;
using Mask = URK::Unity::Mask;
using AspectRatioFitter = URK::Unity::AspectRatioFitter;
using AspectRatioFitterMode = URK::Unity::AspectRatioFitterMode;
using Mesh = URK::Unity::Mesh;
using MeshCollider = URK::Unity::MeshCollider;
using MeshFilter = URK::Unity::MeshFilter;
using MeshRenderer = URK::Unity::MeshRenderer;
using MonoBehaviour = URK::Unity::MonoBehaviour;
using MouseButton = URK::Unity::MouseButton;
using MotionVectorGenerationMode = URK::Unity::MotionVectorGenerationMode;
using Object = URK::Unity::Object;
using ObjectFilterFlags = URK::Unity::ObjectFilterFlags;
using ProjectionResult = URK::Unity::ProjectionResult;
using Quaternion = URK::Unity::Quaternion;
using Ray = URK::Unity::Ray;
using Rect = URK::Unity::Rect;
using RectTransform = URK::Unity::RectTransform;
using RectTransformAxis = URK::Unity::RectTransformAxis;
using RectTransformEdge = URK::Unity::RectTransformEdge;
using RectMask2D = URK::Unity::RectMask2D;
using RawImage = URK::Unity::RawImage;
using ReflectionProbeUsage = URK::Unity::ReflectionProbeUsage;
using Renderer = URK::Unity::Renderer;
using Rigidbody = URK::Unity::Rigidbody;
using Rigidbody2D = URK::Unity::Rigidbody2D;
using Scene = URK::Unity::Scene;
using Scrollbar = URK::Unity::Scrollbar;
using ScrollbarDirection = URK::Unity::ScrollbarDirection;
using ScrollRect = URK::Unity::ScrollRect;
using ScrollRectMovementType = URK::Unity::ScrollRectMovementType;
using ScriptableObject = URK::Unity::ScriptableObject;
using Selectable = URK::Unity::Selectable;
using SelectableTransition = URK::Unity::SelectableTransition;
using ShadowCastingMode = URK::Unity::ShadowCastingMode;
using SkinQuality = URK::Unity::SkinQuality;
using SkinnedMeshRenderer = URK::Unity::SkinnedMeshRenderer;
using Sprite = URK::Unity::Sprite;
using Shader = URK::Unity::Shader;
using Slider = URK::Unity::Slider;
using SliderDirection = URK::Unity::SliderDirection;
using Text = URK::Unity::Text;
using TextAnchor = URK::Unity::TextAnchor;
using Texture = URK::Unity::Texture;
using Texture2D = URK::Unity::Texture2D;
using TextMeshProUGUI = URK::Unity::TextMeshProUGUI;
using TmpInputField = URK::Unity::TmpInputField;
using TmpInputFieldContentType = URK::Unity::TmpInputFieldContentType;
using TmpInputFieldLineType = URK::Unity::TmpInputFieldLineType;
using TmpDropdown = URK::Unity::TmpDropdown;
using TmpFontStyles = URK::Unity::TmpFontStyles;
using BaseInputModule = URK::Unity::BaseInputModule;
using StandaloneInputModule = URK::Unity::StandaloneInputModule;
using Toggle = URK::Unity::Toggle;
using VerticalLayoutGroup = URK::Unity::VerticalLayoutGroup;
using Transform = URK::Unity::Transform;
using TypeObject = URK::Unity::TypeObject;
using TypeRef = URK::Unity::TypeRef;
using Vector2 = URK::Unity::Vector2;
using Vector2Int = URK::Unity::Vector2Int;
using Vector3 = URK::Unity::Vector3;
using Vector3Int = URK::Unity::Vector3Int;
using Vector4 = URK::Unity::Vector4;

inline const char* last_error() { return URK::Unity::last_error(); }
inline void clear_error() { URK::Unity::clear_error(); }
inline CanvasRoot CreateOverlayCanvas(std::string_view name, bool addRaycaster = true) { return URK::Unity::CreateOverlayCanvas(name, addRaycaster); }

namespace Debug {
inline void Log(std::string_view message) { URK::Unity::Debug::Log(message); }
inline void LogWarning(std::string_view message) { URK::Unity::Debug::LogWarning(message); }
inline void LogError(std::string_view message) { URK::Unity::Debug::LogError(message); }
}

namespace Screen {
inline int width() { return URK::Unity::Screen::width(); }
inline int height() { return URK::Unity::Screen::height(); }
inline float dpi() { return URK::Unity::Screen::dpi(); }
inline Vector2 size(Camera camera = {}) { return URK::Unity::screen_size(camera); }
inline Vector2 center(Camera camera = {}) { return URK::Unity::screen_center(camera); }
inline bool contains(Vector2 point, float padding = 0.0f, Camera camera = {}) { return URK::Unity::screen_contains(point, padding, camera); }
inline Vector2 clamp(Vector2 point, float padding = 0.0f, Camera camera = {}) { return URK::Unity::clamp_to_screen(point, padding, camera); }
}

namespace SceneManager {
inline Scene GetActiveScene() { return URK::Unity::SceneManager::GetActiveScene(); }
inline int sceneCount() { return URK::Unity::SceneManager::sceneCount(); }
inline Scene GetSceneAt(int index) { return URK::Unity::SceneManager::GetSceneAt(index); }
inline std::vector<Scene> GetLoadedScenes() { return URK::Unity::SceneManager::GetLoadedScenes(); }
inline std::vector<GameObject> GetLoadedSceneRootsFiltered(std::uint32_t filterFlags = static_cast<std::uint32_t>(ObjectFilterFlags::None)) { return URK::Unity::SceneManager::GetLoadedSceneRootsFiltered(filterFlags); }
inline std::vector<GameObject> GetLoadedSceneRoots() { return URK::Unity::SceneManager::GetLoadedSceneRoots(); }
inline std::vector<GameObject> FindSceneGameObjectsFiltered(std::uint32_t filterFlags = static_cast<std::uint32_t>(ObjectFilterFlags::None)) { return URK::Unity::SceneManager::FindSceneGameObjectsFiltered(filterFlags); }
inline std::vector<GameObject> FindSceneGameObjects(bool includeInactive = true) { return URK::Unity::SceneManager::FindSceneGameObjects(includeInactive); }
}

namespace Time {
inline float time() { return URK::Unity::Time::time(); }
inline float deltaTime() { return URK::Unity::Time::deltaTime(); }
inline float unscaledDeltaTime() { return URK::Unity::Time::unscaledDeltaTime(); }
inline float timeScale() { return URK::Unity::Time::timeScale(); }
inline void set_timeScale(float value) { URK::Unity::Time::set_timeScale(value); }
}

namespace Input {
inline bool available() { return URK::Unity::Input::available(); }
inline bool GetKey(int keyCode) { return URK::Unity::Input::GetKey(keyCode); }
inline bool GetKeyDown(int keyCode) { return URK::Unity::Input::GetKeyDown(keyCode); }
inline bool GetKeyUp(int keyCode) { return URK::Unity::Input::GetKeyUp(keyCode); }
inline bool GetMouseButton(int button) { return URK::Unity::Input::GetMouseButton(button); }
inline bool GetMouseButtonDown(int button) { return URK::Unity::Input::GetMouseButtonDown(button); }
inline bool GetMouseButtonUp(int button) { return URK::Unity::Input::GetMouseButtonUp(button); }
inline bool GetKey(KeyCode keyCode) { return URK::Unity::Input::GetKey(keyCode); }
inline bool GetKeyDown(KeyCode keyCode) { return URK::Unity::Input::GetKeyDown(keyCode); }
inline bool GetKeyUp(KeyCode keyCode) { return URK::Unity::Input::GetKeyUp(keyCode); }
inline bool GetMouseButton(MouseButton button) { return URK::Unity::Input::GetMouseButton(button); }
inline bool GetMouseButtonDown(MouseButton button) { return URK::Unity::Input::GetMouseButtonDown(button); }
inline bool GetMouseButtonUp(MouseButton button) { return URK::Unity::Input::GetMouseButtonUp(button); }
}

inline Vector2 direction_to_screen_edge(Vector2 direction, float padding = 24.0f, Camera camera = {}) {
    return URK::Unity::direction_to_screen_edge(direction, padding, camera);
}
inline ProjectionResult project_world(Camera camera, Vector3 world, float edge_padding = 24.0f) {
    return URK::Unity::project_world(camera, world, edge_padding);
}
inline ProjectionResult project_world(Vector3 world, float edge_padding = 24.0f) {
    return URK::Unity::project_world(world, edge_padding);
}
inline ProjectionResult project_transform(Camera camera, Transform transform, float edge_padding = 24.0f) {
    return URK::Unity::project_transform(camera, transform, edge_padding);
}
inline ProjectionResult project_transform(Transform transform, float edge_padding = 24.0f) {
    return URK::Unity::project_transform(transform, edge_padding);
}
inline bool world_to_overlay(Camera camera, Vector3 world, Vector2* out) {
    return URK::Unity::world_to_overlay(camera, world, out);
}
inline bool world_to_overlay(Vector3 world, Vector2* out) {
    return URK::Unity::world_to_overlay(world, out);
}
inline bool world_visible(Camera camera, Vector3 world, float min_facing = 0.01f) {
    return URK::Unity::world_visible(camera, world, min_facing);
}
inline bool world_visible(Vector3 world, float min_facing = 0.01f) {
    return URK::Unity::world_visible(world, min_facing);
}


}

#pragma once

#include "../runtime_api.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <limits>
#include <mutex>
#include <string>
#include <string_view>
#include <optional>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include "../runtime/managed_runtime.h"
#include "detail/managed_backend.h"

#if defined(_WIN32)
#include <excpt.h>
#endif

// URK_UNITY_NAMESPACE_BEGIN
namespace URK::Unity {
struct Object;
struct TypeObject;
struct Component;
struct Behaviour;
struct MonoBehaviour;
struct GameObject;
struct Scene;
struct ScriptableObject;
struct Transform;
struct Camera;
struct Light;
struct Renderer;
struct SkinnedMeshRenderer;
struct Collider;
struct RectTransform;
struct Rigidbody;
struct Rigidbody2D;
struct AudioSource;
struct Animator;
struct Canvas;
struct CanvasRenderer;
struct CanvasGroup;
struct CanvasScaler;
struct Graphic;
struct GraphicRaycaster;
struct Selectable;
struct Image;
struct RawImage;
struct Text;
struct TextMeshProUGUI;
struct TmpInputField;
struct TmpDropdown;
struct Button;
struct Toggle;
struct Slider;
struct Scrollbar;
struct Dropdown;
struct InputField;
struct Mask;
struct RectMask2D;
struct ScrollRect;
struct LayoutElement;
struct HorizontalLayoutGroup;
struct VerticalLayoutGroup;
struct GridLayoutGroup;
struct ContentSizeFitter;
struct AspectRatioFitter;
struct EventSystem;
struct BaseInputModule;
struct StandaloneInputModule;
struct InputSystemUIInputModule;
struct MeshRenderer;
struct MeshFilter;
struct MeshCollider;
struct Mesh;
struct Material;
struct Texture;
struct Texture2D;
struct Shader;
struct Sprite;
struct AssetBundle;

struct Vector2 {
    float x{}; float y{};
    constexpr Vector2() = default;
    constexpr Vector2(float x_, float y_) : x(x_), y(y_) {}
    constexpr Vector2 operator-() const { return {-x, -y}; }
    constexpr Vector2 operator+(Vector2 rhs) const { return {x + rhs.x, y + rhs.y}; }
    constexpr Vector2 operator-(Vector2 rhs) const { return {x - rhs.x, y - rhs.y}; }
    constexpr Vector2 operator*(float s) const { return {x * s, y * s}; }
    constexpr Vector2 operator/(float s) const { return s != 0.0f ? Vector2{x / s, y / s} : Vector2{}; }
    Vector2& operator+=(Vector2 rhs) { x += rhs.x; y += rhs.y; return *this; }
    Vector2& operator-=(Vector2 rhs) { x -= rhs.x; y -= rhs.y; return *this; }
    Vector2& operator*=(float s) { x *= s; y *= s; return *this; }
    Vector2& operator/=(float s) { if (s != 0.0f) { x /= s; y /= s; } else { x = 0.0f; y = 0.0f; } return *this; }
    float sqr_magnitude() const { return x * x + y * y; }
    float magnitude() const { return std::sqrt(sqr_magnitude()); }
    Vector2 normalized() const { const float m = magnitude(); return m > 0.000001f ? (*this / m) : Vector2{}; }
    Vector2& normalize() { const float m = magnitude(); if (m > 0.000001f) { x /= m; y /= m; } else { x = 0.0f; y = 0.0f; } return *this; }
    bool nearly_zero(float epsilon = 0.000001f) const { return sqr_magnitude() <= epsilon * epsilon; }
    static Vector2 normalize(Vector2 value) { return value.normalized(); }
    static float dot(Vector2 a, Vector2 b) { return a.x * b.x + a.y * b.y; }
    static float distance(Vector2 a, Vector2 b) { return (a - b).magnitude(); }
};
struct Vector3 {
    float x{}; float y{}; float z{};
    constexpr Vector3() = default;
    constexpr Vector3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
    constexpr Vector3 operator-() const { return {-x, -y, -z}; }
    constexpr Vector3 operator+(Vector3 rhs) const { return {x + rhs.x, y + rhs.y, z + rhs.z}; }
    constexpr Vector3 operator-(Vector3 rhs) const { return {x - rhs.x, y - rhs.y, z - rhs.z}; }
    constexpr Vector3 operator*(float s) const { return {x * s, y * s, z * s}; }
    constexpr Vector3 operator/(float s) const { return s != 0.0f ? Vector3{x / s, y / s, z / s} : Vector3{}; }
    Vector3& operator+=(Vector3 rhs) { x += rhs.x; y += rhs.y; z += rhs.z; return *this; }
    Vector3& operator-=(Vector3 rhs) { x -= rhs.x; y -= rhs.y; z -= rhs.z; return *this; }
    Vector3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }
    Vector3& operator/=(float s) { if (s != 0.0f) { x /= s; y /= s; z /= s; } else { x = 0.0f; y = 0.0f; z = 0.0f; } return *this; }
    float sqr_magnitude() const { return x * x + y * y + z * z; }
    float magnitude() const { return std::sqrt(sqr_magnitude()); }
    Vector3 normalized() const { const float m = magnitude(); return m > 0.000001f ? (*this / m) : Vector3{}; }
    Vector3& normalize() { const float m = magnitude(); if (m > 0.000001f) { x /= m; y /= m; z /= m; } else { x = 0.0f; y = 0.0f; z = 0.0f; } return *this; }
    bool nearly_zero(float epsilon = 0.000001f) const { return sqr_magnitude() <= epsilon * epsilon; }
    static Vector3 normalize(Vector3 value) { return value.normalized(); }
    static float dot(Vector3 a, Vector3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
    static Vector3 cross(Vector3 a, Vector3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
    static float distance(Vector3 a, Vector3 b) { return (a - b).magnitude(); }
};
struct Quaternion { float x{}; float y{}; float z{}; float w{}; };
struct Vector4 { float x{}; float y{}; float z{}; float w{}; };
struct Color { float r{}; float g{}; float b{}; float a{1.0f}; };
struct Color32 { std::uint8_t r{}; std::uint8_t g{}; std::uint8_t b{}; std::uint8_t a{255}; };
struct Vector2Int { int x{}; int y{}; };
struct Vector3Int { int x{}; int y{}; int z{}; };
struct Rect { float x{}; float y{}; float width{}; float height{}; };
struct Bounds { Vector3 center{}; Vector3 extents{}; Vector3 size() const { return extents * 2.0f; } Vector3 min() const { return center - extents; } Vector3 max() const { return center + extents; } };
struct Ray { Vector3 origin{}; Vector3 direction{}; };
struct ProjectionResult {
    bool valid = false;
    bool in_front = false;
    bool on_screen = false;
    Vector2 screen{};
    Vector2 clamped_screen{};
    Vector2 screen_center{};
    Vector2 direction{};
    Vector3 world{};
    Vector3 screen3{};
    Vector3 viewport{};
    float depth = 0.0f;
    float distance = 0.0f;
    float facing = 0.0f;
};
enum class FindObjectsSortMode : int { None = 0, InstanceID = 1 };
enum class ObjectFilterFlags : std::uint32_t {
    None = 0,
    IncludeInactive = 1u << 0,
    IncludeHidden = 1u << 1,
    IncludeDontDestroyOnLoad = 1u << 2
};
enum class MouseButton : int { Left = 0, Right = 1, Middle = 2 };
enum class ShadowCastingMode : int { Off = 0, On = 1, TwoSided = 2, ShadowsOnly = 3 };
enum class MotionVectorGenerationMode : int { Camera = 0, Object = 1, ForceNoMotion = 2 };
enum class LightProbeUsage : int { Off = 0, BlendProbes = 1, UseProxyVolume = 2, CustomProvided = 3 };
enum class ReflectionProbeUsage : int { Off = 0, BlendProbes = 1, BlendProbesAndSkybox = 2, Simple = 3 };
enum class SkinQuality : int { Auto = 0, Bone1 = 1, Bone2 = 2, Bone4 = 4 };
enum class AnimatorCullingMode : int { AlwaysAnimate = 0, CullUpdateTransforms = 1, CullCompletely = 2 };
enum class AnimatorUpdateMode : int { Normal = 0, AnimatePhysics = 1, UnscaledTime = 2 };
enum class LightType : int { Spot = 0, Directional = 1, Point = 2, Area = 3, Rectangle = 3, Disc = 4 };
enum class LightShadows : int { None = 0, Hard = 1, Soft = 2 };
enum class LightRenderMode : int { Auto = 0, Important = 1, NotImportant = 2 };
enum class LightShadowResolution : int { FromQualitySettings = -1, Low = 0, Medium = 1, High = 2, VeryHigh = 3 };
enum class FontStyle : int { Normal = 0, Bold = 1, Italic = 2, BoldAndItalic = 3 };
enum class TextAnchor : int { UpperLeft = 0, UpperCenter = 1, UpperRight = 2, MiddleLeft = 3, MiddleCenter = 4, MiddleRight = 5, LowerLeft = 6, LowerCenter = 7, LowerRight = 8 };
enum class ImageType : int { Simple = 0, Sliced = 1, Tiled = 2, Filled = 3 };
enum class ImageFillMethod : int { Horizontal = 0, Vertical = 1, Radial90 = 2, Radial180 = 3, Radial360 = 4 };
enum class ButtonTransition : int { None = 0, ColorTint = 1, SpriteSwap = 2, Animation = 3 };
enum class SelectableTransition : int { None = 0, ColorTint = 1, SpriteSwap = 2, Animation = 3 };
enum class CanvasRenderMode : int { ScreenSpaceOverlay = 0, ScreenSpaceCamera = 1, WorldSpace = 2 };
enum class RectTransformAxis : int { Horizontal = 0, Vertical = 1 };
enum class RectTransformEdge : int { Left = 0, Right = 1, Top = 2, Bottom = 3 };
enum class ContentSizeFitterFitMode : int { Unconstrained = 0, MinSize = 1, PreferredSize = 2 };
enum class AspectRatioFitterMode : int { None = 0, WidthControlsHeight = 1, HeightControlsWidth = 2, FitInParent = 3, EnvelopeParent = 4 };
enum class GraphicRaycasterBlockingObjects : int { None = 0, TwoD = 1, ThreeD = 2, All = 3 };
enum class SliderDirection : int { LeftToRight = 0, RightToLeft = 1, BottomToTop = 2, TopToBottom = 3 };
enum class ScrollbarDirection : int { LeftToRight = 0, RightToLeft = 1, BottomToTop = 2, TopToBottom = 3 };
enum class InputFieldContentType : int { Standard = 0, Autocorrected = 1, IntegerNumber = 2, DecimalNumber = 3, Alphanumeric = 4, Name = 5, EmailAddress = 6, Password = 7, Pin = 8, Custom = 9 };
enum class InputFieldLineType : int { SingleLine = 0, MultiLineSubmit = 1, MultiLineNewline = 2 };
enum class CanvasScaleMode : int { ConstantPixelSize = 0, ScaleWithScreenSize = 1, ConstantPhysicalSize = 2 };
enum class CanvasScreenMatchMode : int { MatchWidthOrHeight = 0, Expand = 1, Shrink = 2 };
enum class ScrollRectMovementType : int { Unrestricted = 0, Elastic = 1, Clamped = 2 };
enum class GridLayoutConstraint : int { Flexible = 0, FixedColumnCount = 1, FixedRowCount = 2 };
enum class GridLayoutAxis : int { Horizontal = 0, Vertical = 1 };
enum class GridLayoutCorner : int { UpperLeft = 0, UpperRight = 1, LowerLeft = 2, LowerRight = 3 };
enum class TmpFontStyles : int { Normal = 0, Bold = 1, Italic = 2, Underline = 4, LowerCase = 8, UpperCase = 16, SmallCaps = 32, Strikethrough = 64, Superscript = 128, Subscript = 256, Highlight = 512 };
enum class TmpInputFieldContentType : int { Standard = 0, Autocorrected = 1, IntegerNumber = 2, DecimalNumber = 3, Alphanumeric = 4, Name = 5, EmailAddress = 6, Password = 7, Pin = 8, Custom = 9 };
enum class TmpInputFieldLineType : int { SingleLine = 0, MultiLineSubmit = 1, MultiLineNewline = 2 };
enum class KeyCode : int {
    None = 0, Backspace = 8, Tab = 9, Return = 13, Escape = 27, Space = 32,
    Alpha0 = 48, Alpha1 = 49, Alpha2 = 50, Alpha3 = 51, Alpha4 = 52,
    Alpha5 = 53, Alpha6 = 54, Alpha7 = 55, Alpha8 = 56, Alpha9 = 57,
    A = 97, B = 98, C = 99, D = 100, E = 101, F = 102, G = 103, H = 104,
    I = 105, J = 106, K = 107, L = 108, M = 109, N = 110, O = 111, P = 112,
    Q = 113, R = 114, S = 115, T = 116, U = 117, V = 118, W = 119, X = 120,
    Y = 121, Z = 122,
    Delete = 127,
    UpArrow = 273, DownArrow = 274, RightArrow = 275, LeftArrow = 276,
    Insert = 277, Home = 278, End = 279, PageUp = 280, PageDown = 281,
    F1 = 282, F2 = 283, F3 = 284, F4 = 285, F5 = 286, F6 = 287, F7 = 288,
    F8 = 289, F9 = 290, F10 = 291, F11 = 292, F12 = 293,
    LeftShift = 304, RightShift = 303, LeftControl = 306, RightControl = 305,
    LeftAlt = 308, RightAlt = 307,
    Mouse0 = 323, Mouse1 = 324, Mouse2 = 325, Mouse3 = 326, Mouse4 = 327,
    Mouse5 = 328, Mouse6 = 329
};
using DiagnosticSink = void(*)(const char*);

namespace detail {
inline void append_backend_error();

// Owns a strong GC handle to a managed reference array while exposing its
// elements as the SDK's lightweight wrappers.  The wrappers deliberately do
// not own their targets; the managed array does for the lifetime of this
// lease.  This is intended for multi-call scans where returning a plain vector
// of raw managed pointers would otherwise introduce a GC race.
template<class T> class RootedObjectArray {
  public:
    RootedObjectArray() = default;
    RootedObjectArray(const RootedObjectArray&) = delete;
    RootedObjectArray& operator=(const RootedObjectArray&) = delete;

    RootedObjectArray(RootedObjectArray&& other) noexcept
        : items_(std::move(other.items_)), root_(std::exchange(other.root_, 0)) {}
    RootedObjectArray& operator=(RootedObjectArray&& other) noexcept {
        if (this != &other) {
            reset();
            items_ = std::move(other.items_);
            root_ = std::exchange(other.root_, 0);
        }
        return *this;
    }
    ~RootedObjectArray() { reset(); }

    explicit operator bool() const { return root_ != 0; }
    bool empty() const { return items_.empty(); }
    std::size_t size() const { return items_.size(); }
    const T& operator[](std::size_t index) const { return items_[index]; }
    auto begin() const { return items_.begin(); }
    auto end() const { return items_.end(); }
    std::vector<T> copy_items() const { return items_; }

    void reset() {
        items_.clear();
        if (!root_)
            return;
        const URK::managed::GCHandle root =
            std::exchange(root_, URK::managed::GCHandle{});
#if defined(_WIN32)
        __try {
            Backend::gchandle_free(root);
        } __except (native_access_exception_filter(_exception_code())) {
            quarantined_gchandle_counter().fetch_add(1, std::memory_order_relaxed);
            set_error("Unity rooted array release raised a native access fault; one array handle was quarantined");
        }
#else
        Backend::gchandle_free(root);
#endif
    }

    static RootedObjectArray from_managed_array(void* array, std::string_view operation) {
        RootedObjectArray out;
        if (!array) {
            if (!fallback_error()) set_error(std::string(operation) + " returned a null managed array");
            return out;
        }
        out.root_ = Backend::gchandle_new(array, 0);
        if (!out.root_) {
            set_error(std::string(operation) + " could not root the managed result array");
            append_backend_error();
            return out;
        }
        if (!Backend::has_array_length() || !Backend::has_array_ref_at()) {
            set_error(std::string(operation) + " requires array_length and array_ref_at exports");
            append_backend_error();
            out.reset();
            return out;
        }
        constexpr std::size_t kMaxManagedReferenceArrayElements = 1u << 20;
        const std::size_t reported_count = Backend::array_length(array);
        const std::size_t count = std::min(reported_count, kMaxManagedReferenceArrayElements);
        if (reported_count > kMaxManagedReferenceArrayElements)
            set_error(std::string(operation) + " truncated an excessive managed array length");
        out.items_.reserve(count);
        for (std::size_t index = 0; index < count; ++index) {
            if (void* item = Backend::array_ref_at(array, index))
                out.items_.emplace_back(item);
        }
        return out;
    }

  private:
    std::vector<T> items_;
    URK::managed::GCHandle root_ = 0;
};
inline void append_backend_error() { const char* e = Backend::backend_last_error(); if (e && e[0]) { if (!error_slot().empty()) error_slot() += "; backend: "; error_slot() += e; } }
inline std::string managed_string_to_utf8(void* value) {
    if (!value) return {};
    constexpr std::int64_t kMaxInspectorStringUnits = 1024 * 1024;
    const std::int64_t rawLength = Backend::string_length(value);
    if (rawLength >= 0 && rawLength <= kMaxInspectorStringUnits) {
        const std::size_t capacity = static_cast<std::size_t>(rawLength) * 4 + 1;
        std::string buffer(capacity, '\0');
        if (Backend::string_to_utf8(value, buffer.data(), buffer.size())) {
            buffer.resize(std::strlen(buffer.c_str()));
            return buffer;
        }
    }
    if (!fallback_error()) set_error(rawLength > kMaxInspectorStringUnits
        ? "Unity string conversion rejected an oversized managed string"
        : "Unity string conversion failed: invalid length or backend conversion failure");
    append_backend_error();
    return {};
}
inline std::string class_display_name(const void* klass) {
    if (!klass) return {};
    const char* ns = Backend::class_get_namespace(klass);
    const char* name = Backend::class_get_name(klass);
    if (ns && ns[0]) return std::string(ns) + "." + (name ? name : "<unnamed>");
    return name && name[0] ? std::string(name) : std::string{};
}

template<class T> struct is_wrapper : std::false_type {};
template<class T> inline constexpr bool is_wrapper_v = is_wrapper<std::remove_cvref_t<T>>::value;
}

inline const char* last_error() { return detail::Backend::last_error(); }
inline void clear_error() { detail::clear_error(); }

inline constexpr std::array<std::string_view, 16> common_type_images{
    "UnityEngine.CoreModule.dll",
    "UnityEngine.PhysicsModule.dll",
    "UnityEngine.Physics2DModule.dll",
    "UnityEngine.AudioModule.dll",
    "UnityEngine.AnimationModule.dll",
    "UnityEngine.UIModule.dll",
    "UnityEngine.UI.dll",
    "UnityEngine.ImageConversionModule.dll",
    "UnityEngine.TextRenderingModule.dll",
    "Unity.TextMeshPro.dll",
    "Unity.InputSystem.dll",
    "UnityEngine.AssetBundleModule.dll",
    "UnityEngine.dll",
    "mscorlib.dll",
    "System.Private.CoreLib.dll",
    "netstandard.dll",
};

struct TypeRef {
    std::string_view image;
    std::string_view namespc;
    std::string_view name;
    const void* resolve_class() const {
        const std::string key = detail::type_cache_key(image, namespc, name);
        {
            std::lock_guard<std::mutex> lock(detail::cache_mutex());
            if (const auto found = detail::class_cache().find(key); found != detail::class_cache().end()) return found->second;
        }
        const void* resolved = nullptr;
        if (image.empty()) {
            for (const std::string_view candidate : common_type_images) {
                if (auto* klass = detail::Backend::find_class(candidate, namespc, name)) {
                    resolved = klass;
                    break;
                }
            }
        }
        else {
            resolved = detail::Backend::find_class(image, namespc, name);
        }
        if (resolved) {
            std::lock_guard<std::mutex> lock(detail::cache_mutex());
            detail::class_cache()[key] = resolved;
        }
        return resolved;
    }
    void* resolve_type_object() const {
        const std::string key = detail::type_cache_key(image, namespc, name);
        {
            std::lock_guard<std::mutex> lock(detail::cache_mutex());
            if (const auto found = detail::type_cache().find(key); found != detail::type_cache().end()) return found->second;
        }
        void* resolved = nullptr;
        if (image.empty()) {
            for (const std::string_view candidate : common_type_images) {
                if (void* type = detail::Backend::type_object_for_class(candidate, namespc, name)) {
                    resolved = type;
                    break;
                }
            }
        }
        else {
            resolved = detail::Backend::type_object_for_class(image, namespc, name);
        }
        if (resolved) {
            std::lock_guard<std::mutex> lock(detail::cache_mutex());
            detail::type_cache()[key] = resolved;
        }
        return resolved;
    }
};

inline constexpr TypeRef UnityObjectType{"", "UnityEngine", "Object"};
inline constexpr TypeRef GameObjectType{"", "UnityEngine", "GameObject"};
inline constexpr TypeRef ComponentType{"", "UnityEngine", "Component"};
inline constexpr TypeRef BehaviourType{"", "UnityEngine", "Behaviour"};
inline constexpr TypeRef MonoBehaviourType{"", "UnityEngine", "MonoBehaviour"};
inline constexpr TypeRef ScriptableObjectType{"", "UnityEngine", "ScriptableObject"};
inline constexpr TypeRef TransformType{"", "UnityEngine", "Transform"};
inline constexpr TypeRef CameraType{"", "UnityEngine", "Camera"};
inline constexpr TypeRef LightTypeRef{"", "UnityEngine", "Light"};
inline constexpr TypeRef RendererType{"", "UnityEngine", "Renderer"};
inline constexpr TypeRef SkinnedMeshRendererType{"", "UnityEngine", "SkinnedMeshRenderer"};
inline constexpr TypeRef ColliderType{"", "UnityEngine", "Collider"};
inline constexpr TypeRef RectTransformType{"", "UnityEngine", "RectTransform"};
inline constexpr TypeRef RigidbodyType{"", "UnityEngine", "Rigidbody"};
inline constexpr TypeRef Rigidbody2DType{"", "UnityEngine", "Rigidbody2D"};
inline constexpr TypeRef AudioSourceType{"", "UnityEngine", "AudioSource"};
inline constexpr TypeRef AnimatorType{"", "UnityEngine", "Animator"};
inline constexpr TypeRef CanvasType{"", "UnityEngine", "Canvas"};
inline constexpr TypeRef CanvasRendererType{"", "UnityEngine", "CanvasRenderer"};
inline constexpr TypeRef CanvasGroupType{"", "UnityEngine", "CanvasGroup"};
inline constexpr TypeRef CanvasScalerType{"", "UnityEngine.UI", "CanvasScaler"};
inline constexpr TypeRef GraphicType{"", "UnityEngine.UI", "Graphic"};
inline constexpr TypeRef GraphicRaycasterType{"", "UnityEngine.UI", "GraphicRaycaster"};
inline constexpr TypeRef SelectableType{"", "UnityEngine.UI", "Selectable"};
inline constexpr TypeRef ImageTypeRef{"", "UnityEngine.UI", "Image"};
inline constexpr TypeRef RawImageType{"", "UnityEngine.UI", "RawImage"};
inline constexpr TypeRef TextType{"", "UnityEngine.UI", "Text"};
inline constexpr TypeRef TextMeshProUGUIType{"", "TMPro", "TextMeshProUGUI"};
inline constexpr TypeRef TmpInputFieldType{"", "TMPro", "TMP_InputField"};
inline constexpr TypeRef TmpDropdownType{"", "TMPro", "TMP_Dropdown"};
inline constexpr TypeRef ButtonType{"", "UnityEngine.UI", "Button"};
inline constexpr TypeRef ToggleType{"", "UnityEngine.UI", "Toggle"};
inline constexpr TypeRef SliderType{"", "UnityEngine.UI", "Slider"};
inline constexpr TypeRef ScrollbarType{"", "UnityEngine.UI", "Scrollbar"};
inline constexpr TypeRef DropdownType{"", "UnityEngine.UI", "Dropdown"};
inline constexpr TypeRef InputFieldType{"", "UnityEngine.UI", "InputField"};
inline constexpr TypeRef MaskType{"", "UnityEngine.UI", "Mask"};
inline constexpr TypeRef RectMask2DType{"", "UnityEngine.UI", "RectMask2D"};
inline constexpr TypeRef ScrollRectType{"", "UnityEngine.UI", "ScrollRect"};
inline constexpr TypeRef LayoutElementType{"", "UnityEngine.UI", "LayoutElement"};
inline constexpr TypeRef HorizontalLayoutGroupType{"", "UnityEngine.UI", "HorizontalLayoutGroup"};
inline constexpr TypeRef VerticalLayoutGroupType{"", "UnityEngine.UI", "VerticalLayoutGroup"};
inline constexpr TypeRef GridLayoutGroupType{"", "UnityEngine.UI", "GridLayoutGroup"};
inline constexpr TypeRef ContentSizeFitterType{"", "UnityEngine.UI", "ContentSizeFitter"};
inline constexpr TypeRef AspectRatioFitterType{"", "UnityEngine.UI", "AspectRatioFitter"};
inline constexpr TypeRef EventSystemType{"", "UnityEngine.EventSystems", "EventSystem"};
inline constexpr TypeRef BaseInputModuleType{"", "UnityEngine.EventSystems", "BaseInputModule"};
inline constexpr TypeRef StandaloneInputModuleType{"", "UnityEngine.EventSystems", "StandaloneInputModule"};
inline constexpr TypeRef InputSystemUIInputModuleType{"Unity.InputSystem.dll", "UnityEngine.InputSystem.UI", "InputSystemUIInputModule"};
inline constexpr TypeRef MeshRendererType{"", "UnityEngine", "MeshRenderer"};
inline constexpr TypeRef MeshFilterType{"", "UnityEngine", "MeshFilter"};
inline constexpr TypeRef MeshColliderType{"", "UnityEngine", "MeshCollider"};
inline constexpr TypeRef MeshType{"", "UnityEngine", "Mesh"};
inline constexpr TypeRef MaterialType{"", "UnityEngine", "Material"};
inline constexpr TypeRef TextureType{"", "UnityEngine", "Texture"};
inline constexpr TypeRef Texture2DType{"", "UnityEngine", "Texture2D"};
inline constexpr TypeRef ShaderType{"", "UnityEngine", "Shader"};
inline constexpr TypeRef SpriteType{"", "UnityEngine", "Sprite"};
inline constexpr TypeRef AssetBundleType{"", "UnityEngine", "AssetBundle"};
inline constexpr TypeRef ScreenType{"", "UnityEngine", "Screen"};
inline constexpr TypeRef TimeType{"", "UnityEngine", "Time"};
inline constexpr TypeRef ResourcesType{"", "UnityEngine", "Resources"};
inline constexpr TypeRef DebugType{"", "UnityEngine", "Debug"};


namespace detail {
// IL2CPP field setters take the address of raw value-type storage, but take a
// managed object directly for reference fields. Field getters still write a
// reference into an output slot, so FieldOut keeps its pointer-to-pointer form.
template<class T> struct FieldArg { T storage; void* ptr; FieldArg(T v):storage(v),ptr(&storage){} };
template<class T> requires is_wrapper_v<T> struct FieldArg<T> { void* storage; void* ptr; FieldArg(T v):storage(v.handle()),ptr(Backend::field_reference_write_pointer(storage)){} };
template<> struct FieldArg<void*> { void* storage; void* ptr; FieldArg(void* v):storage(v),ptr(Backend::field_reference_write_pointer(storage)){} };
template<> struct FieldArg<const char*> { void* storage; void* ptr; FieldArg(const char* v):storage(Backend::new_string(v ? std::string_view(v) : std::string_view{})),ptr(Backend::field_reference_write_pointer(storage)){} };
template<std::size_t N> struct FieldArg<char[N]> { void* storage; void* ptr; FieldArg(const char (&v)[N]):storage(Backend::new_string(std::string_view(v, N > 0 && v[N - 1] == '\0' ? N - 1 : N))),ptr(Backend::field_reference_write_pointer(storage)){} };
template<std::size_t N> struct FieldArg<const char[N]> { void* storage; void* ptr; FieldArg(const char (&v)[N]):storage(Backend::new_string(std::string_view(v, N > 0 && v[N - 1] == '\0' ? N - 1 : N))),ptr(Backend::field_reference_write_pointer(storage)){} };
template<> struct FieldArg<std::string> { void* storage; void* ptr; FieldArg(const std::string& v):storage(Backend::new_string(v)),ptr(Backend::field_reference_write_pointer(storage)){} };
template<> struct FieldArg<std::string_view> { void* storage; void* ptr; FieldArg(std::string_view v):storage(Backend::new_string(v)),ptr(Backend::field_reference_write_pointer(storage)){} };
template<class T> struct FieldOut { T value{}; void* ptr(){ return &value; } T get(){ return value; } };
template<class T> requires is_wrapper_v<T> struct FieldOut<T> { void* value=nullptr; void* ptr(){ return &value; } T get(){ return T{value}; } };
template<> struct FieldOut<void*> { void* value=nullptr; void* ptr(){ return &value; } void* get(){ return value; } };
template<> struct FieldOut<std::string> { void* value=nullptr; void* ptr(){ return &value; } std::string get(){ return managed_string_to_utf8(value); } };
template<class T> void* field_value(T& v) { FieldArg<std::remove_cvref_t<T>> a(v); return a.ptr; }
template<class Ret, class... Args> Ret InvokeStatic(TypeRef type, std::string_view methodName, Args&&... args);
template<class T, class... Args> std::vector<T> StaticArrayCall(TypeRef type, std::string_view methodName, Args&&... args);
template<class T, class... ExtraArgs> std::vector<T> FindObjectsUsing(TypeRef owner, std::string_view methodName, std::string_view image, std::string_view namespc, std::string_view className, ExtraArgs&&... extraArgs);
template<class T, class... ExtraArgs> RootedObjectArray<T> FindObjectsUsingRooted(TypeRef owner, std::string_view methodName, std::string_view image, std::string_view namespc, std::string_view className, ExtraArgs&&... extraArgs);
}
struct TypeObject { void* handle_ = nullptr; explicit TypeObject(void* h=nullptr) : handle_(h) {} void* handle() const { return handle_; } explicit operator bool() const { return handle_ != nullptr; } };

struct Object {
    void* handle_ = nullptr;
    Object() = default;
    explicit Object(void* h) : handle_(h) {}
    void* handle() const { return handle_; }
    explicit operator bool() const { return handle_ != nullptr; }
    static constexpr TypeRef unity_type() { return UnityObjectType; }
    bool alive() const { return handle_ ? detail::InvokeStatic<bool>(UnityObjectType, "op_Implicit", *this) : false; }
    std::string name() const;
    std::string ToString() const;
    std::string runtime_class_name() const;
    int hideFlags() const { return GetProperty<int>("hideFlags"); }
    int GetInstanceID() const { return Call<int>("GetInstanceID"); }

    template<class T = Object> static std::vector<T> FindObjectsOfType(std::string_view image, std::string_view namespc, std::string_view className) { return detail::FindObjectsUsing<T>(UnityObjectType, "FindObjectsOfType", image, namespc, className); }
    template<class T = Object> static std::vector<T> FindObjectsByType(std::string_view image, std::string_view namespc, std::string_view className, FindObjectsSortMode sortMode = FindObjectsSortMode::None) { return detail::FindObjectsUsing<T>(UnityObjectType, "FindObjectsByType", image, namespc, className, sortMode); }
    template<class T = Object> static std::vector<T> FindObjectsOfTypeAll(std::string_view image, std::string_view namespc, std::string_view className) { return detail::FindObjectsUsing<T>(ResourcesType, "FindObjectsOfTypeAll", image, namespc, className); }
    template<class T = Object> static T FindObjectOfType(std::string_view image, std::string_view namespc, std::string_view className) { auto all = FindObjectsOfType<T>(image, namespc, className); return all.empty() ? T{} : all.front(); }
    template<class T = Object> static T FindObjectOfTypeAll(std::string_view image, std::string_view namespc, std::string_view className) { auto all = FindObjectsOfTypeAll<T>(image, namespc, className); return all.empty() ? T{} : all.front(); }
    template<class T = Object> static T FindObject(std::string_view image, std::string_view namespc, std::string_view className) { return FindObjectOfType<T>(image, namespc, className); }
    template<class T = Object> static T FindInstance(std::string_view image, std::string_view namespc, std::string_view className) { return FindObjectOfType<T>(image, namespc, className); }
    template<class T = Object> static std::vector<T> FindInstances(std::string_view image, std::string_view namespc, std::string_view className) { return FindObjectsOfType<T>(image, namespc, className); }
    template<class T = Object> static std::vector<T> FindAllInstances(std::string_view image, std::string_view namespc, std::string_view className) { return FindObjectsOfTypeAll<T>(image, namespc, className); }
    template<class T> static std::vector<T> FindObjectsOfType() { const TypeRef type = T::unity_type(); return FindObjectsOfType<T>(type.image, type.namespc, type.name); }
    template<class T> static std::vector<T> FindObjectsByType(FindObjectsSortMode sortMode = FindObjectsSortMode::None) { const TypeRef type = T::unity_type(); return FindObjectsByType<T>(type.image, type.namespc, type.name, sortMode); }
    template<class T> static std::vector<T> FindObjectsOfTypeAll() { const TypeRef type = T::unity_type(); return FindObjectsOfTypeAll<T>(type.image, type.namespc, type.name); }
    template<class T> static detail::RootedObjectArray<T> FindObjectsOfTypeRooted() { const TypeRef type = T::unity_type(); return detail::FindObjectsUsingRooted<T>(UnityObjectType, "FindObjectsOfType", type.image, type.namespc, type.name); }
    template<class T> static detail::RootedObjectArray<T> FindObjectsOfTypeAllRooted() { const TypeRef type = T::unity_type(); return detail::FindObjectsUsingRooted<T>(ResourcesType, "FindObjectsOfTypeAll", type.image, type.namespc, type.name); }
    template<class T> static T FindObjectOfType() { auto all = FindObjectsOfType<T>(); return all.empty() ? T{} : all.front(); }
    template<class T> static T FindObjectOfTypeAll() { auto all = FindObjectsOfTypeAll<T>(); return all.empty() ? T{} : all.front(); }
    template<class T> static T FindObject() { return FindObjectOfType<T>(); }
    template<class T> static T FindInstance() { return FindObjectOfType<T>(); }
    template<class T> static std::vector<T> FindInstances() { return FindObjectsOfType<T>(); }
    template<class T> static std::vector<T> FindAllInstances() { return FindObjectsOfTypeAll<T>(); }
    template<class T = Object> static T Instantiate(const T& original) { return T{detail::InvokeStatic<void*>(UnityObjectType, "Instantiate", original)}; }
    template<class T = Object> static T Instantiate(const T& original, const Transform& parent) { return T{detail::InvokeStatic<void*>(UnityObjectType, "Instantiate", original, parent)}; }
    template<class T = Object> static T Instantiate(const T& original, const Transform& parent, bool instantiateInWorldSpace) { return T{detail::InvokeStatic<void*>(UnityObjectType, "Instantiate", original, parent, instantiateInWorldSpace)}; }
    template<class T = Object> static T Instantiate(const T& original, Vector3 position, Quaternion rotation) { return T{detail::InvokeStatic<void*>(UnityObjectType, "Instantiate", original, position, rotation)}; }
    template<class T = Object> static T Instantiate(const T& original, Vector3 position, Quaternion rotation, const Transform& parent) { return T{detail::InvokeStatic<void*>(UnityObjectType, "Instantiate", original, position, rotation, parent)}; }
    static void Destroy(const Object& object) { detail::InvokeStatic<void>(UnityObjectType, "Destroy", object); }
    static void Destroy(const Object& object, float delaySeconds) { detail::InvokeStatic<void>(UnityObjectType, "Destroy", object, delaySeconds); }
    static void DestroyImmediate(const Object& object, bool allowDestroyingAssets = false) { detail::InvokeStatic<void>(UnityObjectType, "DestroyImmediate", object, allowDestroyingAssets); }
    static void DontDestroyOnLoad(const Object& object) { detail::InvokeStatic<void>(UnityObjectType, "DontDestroyOnLoad", object); }

    template<class Ret = void, class... Args> Ret Call(std::string_view methodName, Args&&... args) const;
    template<class T> T GetField(std::string_view fieldName) const;
    template<class T> void SetField(std::string_view fieldName, T value) const;
    template<class T> static T StaticGetField(TypeRef type, std::string_view fieldName);
    template<class T> static void StaticSetField(TypeRef type, std::string_view fieldName, T value);
    template<class Ret = void> Ret CallExact(std::string_view methodName, const std::vector<const char*>& parameterTypeNames, void** rawArgs) const;
    template<class Ret = void, class... Args> Ret CallExact(std::string_view methodName, const std::vector<const char*>& parameterTypeNames, Args&&... args) const;
    template<class Ret = void, class... Args> Ret InvokeGeneric(std::string_view methodName, const std::vector<TypeObject>& genericTypes, Args&&... args) const;
    template<class T = Object> std::vector<T> CallArrayExact(std::string_view methodName, const std::vector<const char*>& parameterTypeNames, void** rawArgs) const;
    template<class T = Object, class... Args> std::vector<T> CallArrayExact(std::string_view methodName, const std::vector<const char*>& parameterTypeNames, Args&&... args) const;
    template<class T = Object, class... Args> detail::RootedObjectArray<T> CallArrayExactRooted(std::string_view methodName, const std::vector<const char*>& parameterTypeNames, Args&&... args) const;
    std::vector<std::string> CallStringArrayExact(std::string_view methodName, const std::vector<const char*>& parameterTypeNames) const;
    template<class T> void SetReferenceArrayProperty(std::string_view propertyName, const std::vector<T>& values) const;
    template<class T> T GetProperty(std::string_view propertyName) const { return Call<T>(std::string("get_") + std::string(propertyName)); }
    template<class T> void SetProperty(std::string_view propertyName, T value) const { Call<void>(std::string("set_") + std::string(propertyName), value); }
};

struct GameObject;
struct Scene;
struct Transform;
struct Camera;
struct Light;
struct Renderer;
struct SkinnedMeshRenderer;
struct Collider;
struct RectTransform;
struct Rigidbody;
struct Rigidbody2D;
struct Animator;
struct Canvas;
struct CanvasGroup;
struct CanvasScaler;
struct Graphic;
struct Image;
struct RawImage;
struct Text;
struct TextMeshProUGUI;
struct Button;
struct Toggle;
struct Slider;
struct Scrollbar;
struct Dropdown;
struct InputField;
struct Mask;
struct ScrollRect;
struct LayoutElement;
struct HorizontalLayoutGroup;
struct VerticalLayoutGroup;
struct GridLayoutGroup;
struct MeshRenderer;
struct MeshFilter;
struct MeshCollider;
struct Mesh;
struct Material;
struct Texture;
struct Texture2D;
struct Shader;
struct Sprite;

}

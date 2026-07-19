#pragma once

#include "../runtime_api.h"

#include <algorithm>
#include <array>
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
#include "../il2cpp/il2cpp_runtime.h"


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
inline std::string& error_slot() { static std::string value; return value; }
inline std::mutex& cache_mutex() { static std::mutex value; return value; }
inline std::unordered_map<std::string, const void*>& class_cache() { static std::unordered_map<std::string, const void*> value; return value; }
inline std::unordered_map<std::string, void*>& type_cache() { static std::unordered_map<std::string, void*> value; return value; }
inline std::unordered_map<std::string, const void*>& method_cache() { static std::unordered_map<std::string, const void*> value; return value; }
inline std::unordered_map<std::string, const void*>& field_cache() { static std::unordered_map<std::string, const void*> value; return value; }
inline void clear_error() { error_slot().clear(); }
inline void set_error(std::string_view text) { error_slot() = std::string(text); }
inline const char* fallback_error() { return error_slot().empty() ? nullptr : error_slot().c_str(); }
inline std::string z(std::string_view v) { return std::string(v); }
inline std::string signature_text(std::string_view methodName, const std::vector<const char*>& parameterTypeNames) { std::string s(methodName); s += "("; for (std::size_t i=0; i<parameterTypeNames.size(); ++i) { if (i) s += ", "; s += parameterTypeNames[i] ? parameterTypeNames[i] : "<unknown>"; } s += ")"; return s; }
inline std::string type_cache_key(std::string_view image, std::string_view namespc, std::string_view name) {
    std::string key(image);
    key.push_back('|');
    key.append(namespc);
    key.push_back('|');
    key.append(name);
    return key;
}
inline std::string member_cache_key(const void* klass, std::string_view name, int argc) {
    std::string key = std::to_string(reinterpret_cast<std::uintptr_t>(klass));
    key.push_back('|');
    key.append(name);
    key.push_back('|');
    key.append(std::to_string(argc));
    return key;
}
inline std::string member_cache_key(const void* klass, std::string_view name, const std::vector<const char*>& parameterTypeNames) {
    std::string key = std::to_string(reinterpret_cast<std::uintptr_t>(klass));
    key.push_back('|');
    key.append(name);
    key.push_back('(');
    for (std::size_t i = 0; i < parameterTypeNames.size(); ++i) {
        if (i) key.push_back(',');
        key.append(parameterTypeNames[i] ? parameterTypeNames[i] : "<unknown>");
    }
    key.push_back(')');
    return key;
}
inline std::string normalized_type_name(std::string_view type) {
    std::string out(type);
    if (out.rfind("class ", 0) == 0) out.erase(0, 6);
    if (out.rfind("struct ", 0) == 0) out.erase(0, 7);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
    if (out == "bool" || out == "boolean" || out == "system.boolean") return "system.boolean";
    if (out == "int" || out == "int32" || out == "system.int32") return "system.int32";
    if (out == "uint" || out == "uint32" || out == "system.uint32") return "system.uint32";
    if (out == "short" || out == "int16" || out == "system.int16") return "system.int16";
    if (out == "ushort" || out == "uint16" || out == "system.uint16") return "system.uint16";
    if (out == "long" || out == "int64" || out == "system.int64") return "system.int64";
    if (out == "ulong" || out == "uint64" || out == "system.uint64") return "system.uint64";
    if (out == "float" || out == "single" || out == "system.single") return "system.single";
    if (out == "double" || out == "system.double") return "system.double";
    if (out == "byte" || out == "system.byte") return "system.byte";
    if (out == "sbyte" || out == "system.sbyte") return "system.sbyte";
    if (out == "char" || out == "system.char") return "system.char";
    if (out == "string" || out == "system.string") return "system.string";
    if (out == "object" || out == "system.object") return "system.object";
    if (out == "type" || out == "system.type") return "system.type";
    if (out == "void" || out == "system.void") return "system.void";
    return out;
}
inline bool type_name_matches(std::string_view actual, const char* requested) { return requested && normalized_type_name(actual) == normalized_type_name(requested); }
struct Backend {
    static bool available() { return URK::il2cpp::available(); }
    static const char* backend_last_error() { return URK::il2cpp::last_error(); }
    static const char* last_error() {
        // Backend diagnostics are copied into the local slot at the failure
        // site. Returning that slot prevents a stale backend error from
        // turning a successful false/null Unity result into a failure.
        thread_local std::string snapshot;
        snapshot = fallback_error() ? fallback_error() : "";
        return snapshot.empty() ? nullptr : snapshot.c_str();
    }
    static const void* find_class(std::string_view image, std::string_view ns, std::string_view name) { auto i=z(image), n=z(ns), c=z(name); return URK::il2cpp::find_class(i.c_str(), n.c_str(), c.c_str()); }
    static const void* object_get_class(void* object) { return object ? URK::il2cpp::object_get_class(static_cast<URK::il2cpp::Object*>(object)) : nullptr; }
    static const char* class_get_name(const void* klass) { return klass ? URK::il2cpp::class_get_name(static_cast<const URK::il2cpp::Class*>(klass)) : nullptr; }
    static const char* class_get_namespace(const void* klass) { return klass ? URK::il2cpp::class_get_namespace(static_cast<const URK::il2cpp::Class*>(klass)) : nullptr; }
    static const void* find_method(const void* klass, std::string_view name, int argc) {
        if (!klass) { set_error("Unity method lookup failed: class is null"); return nullptr; }
        const std::string cacheKey = member_cache_key(klass, name, argc);
        { std::lock_guard<std::mutex> lock(cache_mutex()); const auto found = method_cache().find(cacheKey); if (found != method_cache().end()) return found->second; }
        auto n=z(name); const void* current=klass;
        while (current) {
            void* it=nullptr; const void* match=nullptr; int matches=0;
            while (const auto* m = URK::il2cpp::class_get_methods(static_cast<const URK::il2cpp::Class*>(current), &it)) {
                const char* mn = URK::il2cpp::method_get_name(m); if (!mn || n != mn) continue;
                if (argc < 0 || static_cast<int>(URK::il2cpp::method_get_param_count(m)) == argc) { match=m; ++matches; }
            }
            if (matches > 1) { set_error(std::string("Unity method lookup failed: ambiguous overload by name/argc: ")+std::string(name)); return nullptr; }
            if (match) { std::lock_guard<std::mutex> lock(cache_mutex()); method_cache()[cacheKey] = match; return match; }
            current = URK::il2cpp::class_get_parent(static_cast<const URK::il2cpp::Class*>(current));
        }
        return nullptr;
    }
    static const void* find_method_exact(const void* klass, std::string_view name, const std::vector<const char*>& parameterTypeNames) {
        if (!klass) { set_error("Unity exact method lookup failed: class is null"); return nullptr; }
        const std::string cacheKey = member_cache_key(klass, name, parameterTypeNames);
        { std::lock_guard<std::mutex> lock(cache_mutex()); const auto found = method_cache().find(cacheKey); if (found != method_cache().end()) return found->second; }
        auto n=z(name); int same_arity=0; std::string first_mismatch; const void* current=klass;
        while (current) {
            void* it=nullptr; const void* match=nullptr; int matches=0;
            while (const auto* m = URK::il2cpp::class_get_methods(static_cast<const URK::il2cpp::Class*>(current), &it)) {
                const char* mn = URK::il2cpp::method_get_name(m); if (!mn || n != mn || URK::il2cpp::method_get_param_count(m) != parameterTypeNames.size()) continue; ++same_arity;
                bool ok=true; for (std::uint32_t i=0; i<parameterTypeNames.size(); ++i) { char buf[256]{}; const auto* pt=URK::il2cpp::method_get_param(m,i); const char* want=parameterTypeNames[i]; const bool named=URK::il2cpp::type_get_name(pt, buf, sizeof(buf)); std::string_view got(named ? buf : ""); if (!want || !named || (!type_name_matches(got, want))) { if (first_mismatch.empty()) first_mismatch=std::string("; first mismatch param=")+std::to_string(i)+" requested="+(want?want:"<null>")+" actual="+(got.empty()?"<unavailable>":std::string(got)); ok=false; break; } }
                if (ok) { match=m; ++matches; }
            }
            if (matches > 1) { set_error(std::string("Unity exact method lookup failed: ambiguous exact overload: ")+signature_text(name, parameterTypeNames)); return nullptr; }
            if (match) { std::lock_guard<std::mutex> lock(cache_mutex()); method_cache()[cacheKey] = match; return match; }
            current = URK::il2cpp::class_get_parent(static_cast<const URK::il2cpp::Class*>(current));
        }
        set_error(std::string("Unity exact method lookup failed: no overload matched ")+signature_text(name, parameterTypeNames)+"; same-arity candidates="+std::to_string(same_arity)+first_mismatch);
        return nullptr;
    }
    static int runtime_invoke(const void* method, void* object, void** params, void** result, void** exception) { return URK::il2cpp::runtime_invoke(static_cast<const URK::il2cpp::Method*>(method), static_cast<URK::il2cpp::Object*>(object), params, result, exception); }
    static void* object_unbox(void* object) { return object ? URK::il2cpp::object_unbox(static_cast<URK::il2cpp::Object*>(object)) : nullptr; }
    static void* object_new(const void* klass) { return URK::il2cpp::object_new(static_cast<const URK::il2cpp::Class*>(klass)); }
    static void runtime_object_init(void* object) { URK::il2cpp::runtime_object_init(static_cast<URK::il2cpp::Object*>(object)); }
    static void* new_string(std::string_view text) {
        if (text.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
            set_error("Unity managed string exceeds the IL2CPP uint32 length range");
            return nullptr;
        }
        return URK::il2cpp::string_new_len(text.data(), static_cast<std::uint32_t>(text.size()));
    }
    static bool string_to_utf8(void* string, char* output, std::size_t outputSize) { return URK::il2cpp::string_to_utf8(static_cast<URK::il2cpp::String*>(string), output, outputSize); }
    static const void* find_field(const void* klass, std::string_view name) {
        const std::string cacheKey = member_cache_key(klass, name, -1);
        { std::lock_guard<std::mutex> lock(cache_mutex()); const auto found = field_cache().find(cacheKey); if (found != field_cache().end()) return found->second; }
        auto n=z(name); const void* current=klass;
        while (current) {
            void* it=nullptr;
            while (const auto* f = URK::il2cpp::class_get_fields(static_cast<const URK::il2cpp::Class*>(current), &it)) { const char* fn = URK::il2cpp::field_get_name(f); if (fn && n == fn) { std::lock_guard<std::mutex> lock(cache_mutex()); field_cache()[cacheKey] = f; return f; } }
            current = URK::il2cpp::class_get_parent(static_cast<const URK::il2cpp::Class*>(current));
        }
        return nullptr;
    }
    static bool field_get_value(void* object, const void* field, void* output) { return URK::il2cpp::field_get_value(static_cast<URK::il2cpp::Object*>(object), static_cast<const URK::il2cpp::Field*>(field), output); }
    static void* field_reference_write_pointer(void*& reference) { return reference; }
    static bool field_set_value(void* object, const void* field, void* value) { return URK::il2cpp::field_set_value(static_cast<URK::il2cpp::Object*>(object), static_cast<const URK::il2cpp::Field*>(field), value); }
    static void* field_get_value_object(const void* field, void* object) { return URK::il2cpp::field_get_value_object(field, object); }
    static bool field_static_get_value(const void* klass, const void* field, void* output) { (void)klass; return URK::il2cpp::field_static_get_value(static_cast<const URK::il2cpp::Field*>(field), output); }
    static bool field_static_set_value(const void* klass, const void* field, void* value) { (void)klass; return URK::il2cpp::field_static_set_value(static_cast<const URK::il2cpp::Field*>(field), value); }
    static std::size_t array_length(void* array) { return URK::il2cpp::array_length(static_cast<URK::il2cpp::Array*>(array)); }
    static bool has_array_length() { return URK::il2cpp::has_array_length(); }
    static bool has_array_ref_at() { return URK::il2cpp::has_array_ref_at(); }
    static void* array_ref_at(void* array, std::size_t index) { return URK::il2cpp::array_ref_at(static_cast<URK::il2cpp::Array*>(array), index); }
    static void* array_addr_with_size(void* array, int elementSize, std::size_t index) { return URK::il2cpp::array_addr_with_size(static_cast<URK::il2cpp::Array*>(array), elementSize, index); }
    static std::int32_t class_value_size(const void* klass, std::uint32_t* align) { return URK::il2cpp::class_value_size(klass, align); }
    static bool has_array_set_ref() { return URK::il2cpp::has_array_set_ref(); }
    static bool array_set_ref(void* array, std::size_t index, void* value) { return URK::il2cpp::array_set_ref(static_cast<URK::il2cpp::Array*>(array), index, value); }
    static std::uint32_t gchandle_new(void* object, int pinned) { return URK::il2cpp::gchandle_new(object, pinned); }
    static std::uint32_t gchandle_new_weakref(void* object, int trackResurrection) { return URK::il2cpp::gchandle_new_weakref(object, trackResurrection); }
    static void* gchandle_get_target(std::uint32_t handle) { return URK::il2cpp::gchandle_get_target(handle); }
    static void gchandle_free(std::uint32_t handle) { URK::il2cpp::gchandle_free(handle); }
    static void* type_object_for_class(std::string_view image, std::string_view ns, std::string_view name) { const void* k=find_class(image, ns, name); if (!k) return nullptr; auto* t = URK::il2cpp::class_get_type(static_cast<const URK::il2cpp::Class*>(k)); return t ? URK::il2cpp::type_get_object(t) : nullptr; }
        static void* method_get_object(const void* method, const void* refClass) { return URK::il2cpp::method_get_object(static_cast<const URK::il2cpp::Method*>(method), static_cast<const URK::il2cpp::Class*>(refClass)); }
    static void* value_box(const void* klass, void* data) { return URK::il2cpp::value_box(static_cast<const URK::il2cpp::Class*>(klass), data); }

    static std::int64_t string_length(void* string) { return static_cast<std::int64_t>(URK::il2cpp::string_length(static_cast<URK::il2cpp::String*>(string))); }
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

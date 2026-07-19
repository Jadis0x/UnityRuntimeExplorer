#pragma once
#include "unity_components.h"

namespace URK::Unity {
// URK_UNITY_INVOKE_BEGIN
template<> struct detail::is_wrapper<Object> : std::true_type {}; template<> struct detail::is_wrapper<TypeObject> : std::true_type {}; template<> struct detail::is_wrapper<Component> : std::true_type {}; template<> struct detail::is_wrapper<Behaviour> : std::true_type {}; template<> struct detail::is_wrapper<MonoBehaviour> : std::true_type {}; template<> struct detail::is_wrapper<ScriptableObject> : std::true_type {}; template<> struct detail::is_wrapper<GameObject> : std::true_type {}; template<> struct detail::is_wrapper<Transform> : std::true_type {}; template<> struct detail::is_wrapper<Camera> : std::true_type {}; template<> struct detail::is_wrapper<Light> : std::true_type {}; template<> struct detail::is_wrapper<Renderer> : std::true_type {}; template<> struct detail::is_wrapper<SkinnedMeshRenderer> : std::true_type {}; template<> struct detail::is_wrapper<Collider> : std::true_type {}; template<> struct detail::is_wrapper<RectTransform> : std::true_type {}; template<> struct detail::is_wrapper<Rigidbody> : std::true_type {}; template<> struct detail::is_wrapper<Rigidbody2D> : std::true_type {}; template<> struct detail::is_wrapper<AudioSource> : std::true_type {}; template<> struct detail::is_wrapper<Animator> : std::true_type {}; template<> struct detail::is_wrapper<Canvas> : std::true_type {}; template<> struct detail::is_wrapper<Graphic> : std::true_type {}; template<> struct detail::is_wrapper<Image> : std::true_type {}; template<> struct detail::is_wrapper<Text> : std::true_type {}; template<> struct detail::is_wrapper<Button> : std::true_type {}; template<> struct detail::is_wrapper<Mesh> : std::true_type {}; template<> struct detail::is_wrapper<Material> : std::true_type {}; template<> struct detail::is_wrapper<Texture> : std::true_type {}; template<> struct detail::is_wrapper<Sprite> : std::true_type {}; template<> struct detail::is_wrapper<AssetBundle> : std::true_type {}; template<> struct detail::is_wrapper<Scene> : std::true_type {};
template<> struct detail::is_wrapper<CanvasGroup> : std::true_type {}; template<> struct detail::is_wrapper<CanvasScaler> : std::true_type {}; template<> struct detail::is_wrapper<CanvasRenderer> : std::true_type {}; template<> struct detail::is_wrapper<GraphicRaycaster> : std::true_type {}; template<> struct detail::is_wrapper<Selectable> : std::true_type {}; template<> struct detail::is_wrapper<RawImage> : std::true_type {}; template<> struct detail::is_wrapper<TextMeshProUGUI> : std::true_type {}; template<> struct detail::is_wrapper<TmpInputField> : std::true_type {}; template<> struct detail::is_wrapper<TmpDropdown> : std::true_type {}; template<> struct detail::is_wrapper<Toggle> : std::true_type {}; template<> struct detail::is_wrapper<Slider> : std::true_type {}; template<> struct detail::is_wrapper<Scrollbar> : std::true_type {}; template<> struct detail::is_wrapper<Dropdown> : std::true_type {}; template<> struct detail::is_wrapper<InputField> : std::true_type {}; template<> struct detail::is_wrapper<Mask> : std::true_type {}; template<> struct detail::is_wrapper<RectMask2D> : std::true_type {}; template<> struct detail::is_wrapper<ScrollRect> : std::true_type {}; template<> struct detail::is_wrapper<LayoutElement> : std::true_type {}; template<> struct detail::is_wrapper<HorizontalLayoutGroup> : std::true_type {}; template<> struct detail::is_wrapper<VerticalLayoutGroup> : std::true_type {}; template<> struct detail::is_wrapper<GridLayoutGroup> : std::true_type {}; template<> struct detail::is_wrapper<ContentSizeFitter> : std::true_type {}; template<> struct detail::is_wrapper<AspectRatioFitter> : std::true_type {}; template<> struct detail::is_wrapper<EventSystem> : std::true_type {}; template<> struct detail::is_wrapper<BaseInputModule> : std::true_type {}; template<> struct detail::is_wrapper<StandaloneInputModule> : std::true_type {}; template<> struct detail::is_wrapper<InputSystemUIInputModule> : std::true_type {}; template<> struct detail::is_wrapper<MeshRenderer> : std::true_type {}; template<> struct detail::is_wrapper<MeshFilter> : std::true_type {}; template<> struct detail::is_wrapper<MeshCollider> : std::true_type {}; template<> struct detail::is_wrapper<Texture2D> : std::true_type {}; template<> struct detail::is_wrapper<Shader> : std::true_type {};
inline GameObject Component::gameObject() const { return Call<GameObject>("get_gameObject"); }
inline Transform Component::transform() const { return Call<Transform>("get_transform"); }
template<class T> inline T Component::GetComponent() const { return gameObject().GetComponent<T>(); }
template<class T>
inline T Component::GetComponent(const char* name) const {
    return gameObject().GetComponent<T>(name);
}
inline Object Component::GetComponent(std::string_view image, std::string_view namespc, std::string_view className) const { return gameObject().GetComponent(image, namespc, className); }
template<class T> inline T Component::GetComponentInChildren(bool includeInactive) const { return gameObject().GetComponentInChildren<T>(includeInactive); }
inline Object Component::GetComponentInChildren(std::string_view image, std::string_view namespc, std::string_view className, bool includeInactive) const { return gameObject().GetComponentInChildren(image, namespc, className, includeInactive); }
template<class T> inline T Component::GetComponentInParent(bool includeInactive) const { return gameObject().GetComponentInParent<T>(includeInactive); }
inline Object Component::GetComponentInParent(std::string_view image, std::string_view namespc, std::string_view className, bool includeInactive) const { return gameObject().GetComponentInParent(image, namespc, className, includeInactive); }
template<class T> inline std::vector<T> Component::GetComponents() const { return gameObject().GetComponents<T>(); }
template<class T> inline std::vector<T> Component::GetComponentsInChildren(bool includeInactive) const { return gameObject().GetComponentsInChildren<T>(includeInactive); }
template<class T> inline std::vector<T> Component::GetComponentsInParent(bool includeInactive) const { return gameObject().GetComponentsInParent<T>(includeInactive); }
template<class T> inline T Component::AddComponent() const { return gameObject().AddComponent<T>(); }
inline Object Component::AddComponent(std::string_view image, std::string_view namespc, std::string_view className) const { return gameObject().AddComponent(image, namespc, className); }
template<class T> inline bool Component::HasComponent() const { return gameObject().HasComponent<T>(); }
inline bool Component::HasComponent(std::string_view image, std::string_view namespc, std::string_view className) const { return gameObject().HasComponent(image, namespc, className); }
template<class T> inline T Component::GetOrAddComponent() const { return gameObject().GetOrAddComponent<T>(); }
inline Object Component::GetOrAddComponent(std::string_view image, std::string_view namespc, std::string_view className) const { return gameObject().GetOrAddComponent(image, namespc, className); }
inline std::string Object::runtime_class_name() const { return detail::class_display_name(detail::Backend::object_get_class(handle_)); }
inline std::string Object::ToString() const { return detail::managed_string_to_utf8(Call<void*>("ToString")); }
inline std::string Object::name() const { return detail::managed_string_to_utf8(Call<void*>("get_name")); }
inline std::string GameObject::tag() const { return detail::managed_string_to_utf8(Call<void*>("get_tag")); }

namespace detail {
// runtime_invoke expects value-type params as pointers to local storage, but
// managed reference/object/string params as the managed pointer itself.
template<class T> struct Arg { T storage; void* ptr; bool valid; Arg(T v):storage(v),ptr(&storage),valid(true){} };
template<class T> requires is_wrapper_v<T> struct Arg<T> { void* storage; void* ptr; bool valid; Arg(T v):storage(v.handle()),ptr(storage),valid(true){} };
template<> struct Arg<void*> { void* storage; void* ptr; bool valid; Arg(void* v):storage(v),ptr(storage),valid(true){} };
template<> struct Arg<const char*> { void* storage; void* ptr; bool valid; Arg(const char* v):storage(Backend::new_string(v ? std::string_view(v) : std::string_view{})),ptr(storage),valid(storage != nullptr){} };
template<> struct Arg<char*> { void* storage; void* ptr; bool valid; Arg(char* v):storage(Backend::new_string(v ? std::string_view(v) : std::string_view{})),ptr(storage),valid(storage != nullptr){} };
template<std::size_t N> struct Arg<char[N]> { void* storage; void* ptr; bool valid; Arg(const char (&v)[N]):storage(Backend::new_string(std::string_view(v, N > 0 && v[N - 1] == '\0' ? N - 1 : N))),ptr(storage),valid(storage != nullptr){} };
template<std::size_t N> struct Arg<const char[N]> { void* storage; void* ptr; bool valid; Arg(const char (&v)[N]):storage(Backend::new_string(std::string_view(v, N > 0 && v[N - 1] == '\0' ? N - 1 : N))),ptr(storage),valid(storage != nullptr){} };
template<> struct Arg<std::string> { void* storage; void* ptr; bool valid; Arg(const std::string& v):storage(Backend::new_string(v)),ptr(storage),valid(storage != nullptr){} };
template<> struct Arg<std::string_view> { void* storage; void* ptr; bool valid; Arg(std::string_view v):storage(Backend::new_string(v)),ptr(storage),valid(storage != nullptr){} };
template<class T> inline std::optional<const char*> parameter_type_name() { if constexpr (std::is_array_v<T> && std::is_same_v<std::remove_cv_t<std::remove_extent_t<T>>, char>) return "System.String"; else return std::nullopt; }
template<> inline std::optional<const char*> parameter_type_name<bool>() { return "System.Boolean"; }
template<> inline std::optional<const char*> parameter_type_name<int>() { return "System.Int32"; }
template<> inline std::optional<const char*> parameter_type_name<unsigned int>() { return "System.UInt32"; }
template<> inline std::optional<const char*> parameter_type_name<short>() { return "System.Int16"; }
template<> inline std::optional<const char*> parameter_type_name<unsigned short>() { return "System.UInt16"; }
template<> inline std::optional<const char*> parameter_type_name<long long>() { return "System.Int64"; }
template<> inline std::optional<const char*> parameter_type_name<unsigned long long>() { return "System.UInt64"; }
template<> inline std::optional<const char*> parameter_type_name<float>() { return "System.Single"; }
template<> inline std::optional<const char*> parameter_type_name<double>() { return "System.Double"; }
template<> inline std::optional<const char*> parameter_type_name<void*>() { return "System.Object"; }
template<> inline std::optional<const char*> parameter_type_name<std::string_view>() { return "System.String"; }
template<> inline std::optional<const char*> parameter_type_name<std::string>() { return "System.String"; }
template<> inline std::optional<const char*> parameter_type_name<const char*>() { return "System.String"; }
template<> inline std::optional<const char*> parameter_type_name<char*>() { return "System.String"; }
template<> inline std::optional<const char*> parameter_type_name<Vector2>() { return "UnityEngine.Vector2"; }
template<> inline std::optional<const char*> parameter_type_name<Vector3>() { return "UnityEngine.Vector3"; }
template<> inline std::optional<const char*> parameter_type_name<Quaternion>() { return "UnityEngine.Quaternion"; }
template<> inline std::optional<const char*> parameter_type_name<Color>() { return "UnityEngine.Color"; }
template<> inline std::optional<const char*> parameter_type_name<Rect>() { return "UnityEngine.Rect"; }
template<> inline std::optional<const char*> parameter_type_name<Bounds>() { return "UnityEngine.Bounds"; }
template<> inline std::optional<const char*> parameter_type_name<Ray>() { return "UnityEngine.Ray"; }
template<> inline std::optional<const char*> parameter_type_name<TypeObject>() { return "System.Type"; }
template<> inline std::optional<const char*> parameter_type_name<FindObjectsSortMode>() { return "UnityEngine.FindObjectsSortMode"; }
template<> inline std::optional<const char*> parameter_type_name<Object>() { return "UnityEngine.Object"; }
template<> inline std::optional<const char*> parameter_type_name<Component>() { return "UnityEngine.Component"; }
template<> inline std::optional<const char*> parameter_type_name<Behaviour>() { return "UnityEngine.Behaviour"; }
template<> inline std::optional<const char*> parameter_type_name<MonoBehaviour>() { return "UnityEngine.MonoBehaviour"; }
template<> inline std::optional<const char*> parameter_type_name<ScriptableObject>() { return "UnityEngine.ScriptableObject"; }
template<> inline std::optional<const char*> parameter_type_name<GameObject>() { return "UnityEngine.GameObject"; }
template<> inline std::optional<const char*> parameter_type_name<Transform>() { return "UnityEngine.Transform"; }
template<> inline std::optional<const char*> parameter_type_name<Camera>() { return "UnityEngine.Camera"; }
template<> inline std::optional<const char*> parameter_type_name<Light>() { return "UnityEngine.Light"; }
template<> inline std::optional<const char*> parameter_type_name<SkinnedMeshRenderer>() { return "UnityEngine.SkinnedMeshRenderer"; }
template<> inline std::optional<const char*> parameter_type_name<RectTransform>() { return "UnityEngine.RectTransform"; }
template<> inline std::optional<const char*> parameter_type_name<Rigidbody>() { return "UnityEngine.Rigidbody"; }
template<> inline std::optional<const char*> parameter_type_name<Rigidbody2D>() { return "UnityEngine.Rigidbody2D"; }
template<> inline std::optional<const char*> parameter_type_name<AudioSource>() { return "UnityEngine.AudioSource"; }
template<> inline std::optional<const char*> parameter_type_name<Animator>() { return "UnityEngine.Animator"; }
template<> inline std::optional<const char*> parameter_type_name<Canvas>() { return "UnityEngine.Canvas"; }
template<> inline std::optional<const char*> parameter_type_name<CanvasRenderer>() { return "UnityEngine.CanvasRenderer"; }
template<> inline std::optional<const char*> parameter_type_name<Graphic>() { return "UnityEngine.UI.Graphic"; }
template<> inline std::optional<const char*> parameter_type_name<GraphicRaycaster>() { return "UnityEngine.UI.GraphicRaycaster"; }
template<> inline std::optional<const char*> parameter_type_name<Selectable>() { return "UnityEngine.UI.Selectable"; }
template<> inline std::optional<const char*> parameter_type_name<Image>() { return "UnityEngine.UI.Image"; }
template<> inline std::optional<const char*> parameter_type_name<Text>() { return "UnityEngine.UI.Text"; }
template<> inline std::optional<const char*> parameter_type_name<Button>() { return "UnityEngine.UI.Button"; }
template<> inline std::optional<const char*> parameter_type_name<Mesh>() { return "UnityEngine.Mesh"; }
template<> inline std::optional<const char*> parameter_type_name<Material>() { return "UnityEngine.Material"; }
template<> inline std::optional<const char*> parameter_type_name<Texture>() { return "UnityEngine.Texture"; }
template<> inline std::optional<const char*> parameter_type_name<Sprite>() { return "UnityEngine.Sprite"; }
template<> inline std::optional<const char*> parameter_type_name<AssetBundle>() { return "UnityEngine.AssetBundle"; }
template<> inline std::optional<const char*> parameter_type_name<Renderer>() { return "UnityEngine.Renderer"; }
template<> inline std::optional<const char*> parameter_type_name<Collider>() { return "UnityEngine.Collider"; }
template<> inline std::optional<const char*> parameter_type_name<CanvasGroup>() { return "UnityEngine.CanvasGroup"; }
template<> inline std::optional<const char*> parameter_type_name<CanvasScaler>() { return "UnityEngine.UI.CanvasScaler"; }
template<> inline std::optional<const char*> parameter_type_name<RawImage>() { return "UnityEngine.UI.RawImage"; }
template<> inline std::optional<const char*> parameter_type_name<TextMeshProUGUI>() { return "TMPro.TextMeshProUGUI"; }
template<> inline std::optional<const char*> parameter_type_name<TmpInputField>() { return "TMPro.TMP_InputField"; }
template<> inline std::optional<const char*> parameter_type_name<TmpDropdown>() { return "TMPro.TMP_Dropdown"; }
template<> inline std::optional<const char*> parameter_type_name<Toggle>() { return "UnityEngine.UI.Toggle"; }
template<> inline std::optional<const char*> parameter_type_name<Slider>() { return "UnityEngine.UI.Slider"; }
template<> inline std::optional<const char*> parameter_type_name<Scrollbar>() { return "UnityEngine.UI.Scrollbar"; }
template<> inline std::optional<const char*> parameter_type_name<Dropdown>() { return "UnityEngine.UI.Dropdown"; }
template<> inline std::optional<const char*> parameter_type_name<InputField>() { return "UnityEngine.UI.InputField"; }
template<> inline std::optional<const char*> parameter_type_name<Mask>() { return "UnityEngine.UI.Mask"; }
template<> inline std::optional<const char*> parameter_type_name<RectMask2D>() { return "UnityEngine.UI.RectMask2D"; }
template<> inline std::optional<const char*> parameter_type_name<ScrollRect>() { return "UnityEngine.UI.ScrollRect"; }
template<> inline std::optional<const char*> parameter_type_name<LayoutElement>() { return "UnityEngine.UI.LayoutElement"; }
template<> inline std::optional<const char*> parameter_type_name<HorizontalLayoutGroup>() { return "UnityEngine.UI.HorizontalLayoutGroup"; }
template<> inline std::optional<const char*> parameter_type_name<VerticalLayoutGroup>() { return "UnityEngine.UI.VerticalLayoutGroup"; }
template<> inline std::optional<const char*> parameter_type_name<GridLayoutGroup>() { return "UnityEngine.UI.GridLayoutGroup"; }
template<> inline std::optional<const char*> parameter_type_name<ContentSizeFitter>() { return "UnityEngine.UI.ContentSizeFitter"; }
template<> inline std::optional<const char*> parameter_type_name<AspectRatioFitter>() { return "UnityEngine.UI.AspectRatioFitter"; }
template<> inline std::optional<const char*> parameter_type_name<EventSystem>() { return "UnityEngine.EventSystems.EventSystem"; }
template<> inline std::optional<const char*> parameter_type_name<BaseInputModule>() { return "UnityEngine.EventSystems.BaseInputModule"; }
template<> inline std::optional<const char*> parameter_type_name<StandaloneInputModule>() { return "UnityEngine.EventSystems.StandaloneInputModule"; }
template<> inline std::optional<const char*> parameter_type_name<InputSystemUIInputModule>() { return "UnityEngine.InputSystem.UI.InputSystemUIInputModule"; }
template<> inline std::optional<const char*> parameter_type_name<MeshRenderer>() { return "UnityEngine.MeshRenderer"; }
template<> inline std::optional<const char*> parameter_type_name<MeshFilter>() { return "UnityEngine.MeshFilter"; }
template<> inline std::optional<const char*> parameter_type_name<MeshCollider>() { return "UnityEngine.MeshCollider"; }
template<> inline std::optional<const char*> parameter_type_name<Texture2D>() { return "UnityEngine.Texture2D"; }
template<> inline std::optional<const char*> parameter_type_name<Shader>() { return "UnityEngine.Shader"; }
template<class... Args> std::vector<const char*> inferred_parameter_types() { std::vector<const char*> names; names.reserve(sizeof...(Args)); bool all=true; ( [&]{ auto n=parameter_type_name<std::remove_cvref_t<Args>>(); if (n) names.push_back(*n); else all=false; }(), ... ); if (!all) names.clear(); return names; }
template<class Ret> Ret from_result(void* r) {
    if constexpr (std::is_void_v<Ret>) return;
    else if constexpr (std::is_same_v<std::remove_cvref_t<Ret>, std::string>) return managed_string_to_utf8(r);
    else if constexpr (is_wrapper_v<Ret>) return Ret{r};
    else if constexpr (std::is_pointer_v<Ret>) return static_cast<Ret>(r);
    else {
        if (!r) { if (!fallback_error()) set_error("Unity conversion failed: managed return object is null"); return Ret{}; }
        void* p=Backend::object_unbox(r);
        if (!p) { if (!fallback_error()) set_error("Unity conversion failed: object_unbox failed for value type return"); append_backend_error(); return Ret{}; }
        return *static_cast<Ret*>(p);
    }
}
}

template<class Ret, class... Args> Ret Object::Call(std::string_view methodName, Args&&... args) const {
    detail::clear_error();
    if (!handle_) { detail::set_error("Unity Object::Call failed: target object is null"); return detail::from_result<Ret>(nullptr); }
    const void* k=detail::Backend::object_get_class(handle_); if(!k) { detail::set_error("Unity Object::Call failed: object_get_class failed"); detail::append_backend_error(); return detail::from_result<Ret>(nullptr); }
    auto inferred=detail::inferred_parameter_types<Args...>();
    const bool exact = !inferred.empty() || sizeof...(Args)==0;
    const void* m = exact ? detail::Backend::find_method_exact(k, methodName, inferred) : detail::Backend::find_method(k, methodName, sizeof...(Args));
    if(!m) { const std::string lookupDetail = detail::fallback_error() ? detail::fallback_error() : ""; detail::set_error(std::string("Unity Object::Call failed: method not found or ambiguous: ")+(exact ? detail::signature_text(methodName, inferred) : std::string(methodName)+"/"+std::to_string(sizeof...(Args)))+(lookupDetail.empty() ? std::string{} : std::string("; detail: ")+lookupDetail)); detail::append_backend_error(); return detail::from_result<Ret>(nullptr); }
    auto pack=std::tuple<detail::Arg<std::remove_cvref_t<Args>>...>(detail::Arg<std::remove_cvref_t<Args>>(std::forward<Args>(args))...);
    std::array<void*, sizeof...(Args)> argv{}; std::size_t i=0; bool argsValid=true; std::apply([&](auto&... a){ ((argsValid=argsValid && a.valid, argv[i++]=a.ptr), ...); }, pack);
    if (!argsValid) { detail::set_error(std::string("Unity Object::Call failed: managed string argument allocation failed in ")+std::string(methodName)); detail::append_backend_error(); return detail::from_result<Ret>(nullptr); }
    void* result=nullptr; void* ex=nullptr; if(!detail::Backend::runtime_invoke(m,handle_,argv.data(),&result,&ex) || ex) { detail::set_error(std::string("Unity Object::Call failed: runtime_invoke exception in ")+std::string(methodName)); detail::append_backend_error(); return detail::from_result<Ret>(nullptr); } return detail::from_result<Ret>(result);
}

template<class Ret, class... Args> Ret detail::InvokeStatic(TypeRef type, std::string_view methodName, Args&&... args) {
    clear_error();
    const void* k=type.resolve_class(); if(!k) { set_error(std::string("Unity static call failed: class not found for ")+std::string(type.namespc)+"."+std::string(type.name)); append_backend_error(); return from_result<Ret>(nullptr); }
    auto inferred=inferred_parameter_types<Args...>();
    const bool exact = !inferred.empty() || sizeof...(Args)==0;
    const void* m = exact ? Backend::find_method_exact(k, methodName, inferred) : Backend::find_method(k, methodName, sizeof...(Args));
    if(!m) { const std::string lookupDetail = fallback_error() ? fallback_error() : ""; set_error(std::string("Unity static call failed: Unity method not found or ambiguous: ")+std::string(type.namespc)+"."+std::string(type.name)+"."+(exact ? signature_text(methodName, inferred) : std::string(methodName)+"/"+std::to_string(sizeof...(Args)))+(lookupDetail.empty() ? std::string{} : std::string("; detail: ")+lookupDetail)); append_backend_error(); return from_result<Ret>(nullptr); }
    auto pack=std::tuple<Arg<std::remove_cvref_t<Args>>...>(Arg<std::remove_cvref_t<Args>>(std::forward<Args>(args))...);
    std::array<void*, sizeof...(Args)> argv{}; std::size_t i=0; bool argsValid=true; std::apply([&](auto&... a){ ((argsValid=argsValid && a.valid, argv[i++]=a.ptr), ...); }, pack);
    if (!argsValid) { set_error(std::string("Unity static call failed: managed string argument allocation failed in ")+std::string(methodName)); append_backend_error(); return from_result<Ret>(nullptr); }
    void* result=nullptr; void* ex=nullptr; if(!Backend::runtime_invoke(m,nullptr,argv.data(),&result,&ex) || ex) { set_error(std::string("Unity static call failed: runtime_invoke exception in ")+std::string(methodName)); append_backend_error(); return from_result<Ret>(nullptr); } return from_result<Ret>(result);
}

template<class T, class... Args> std::vector<T> detail::StaticArrayCall(TypeRef type, std::string_view methodName, Args&&... args) {
    std::vector<T> out; void* array = InvokeStatic<void*>(type, methodName, std::forward<Args>(args)...);
    if (!array) { if (!fallback_error()) set_error(std::string("Unity array call returned null: ")+std::string(methodName)); return out; }
    if (!Backend::has_array_length()) { set_error("Unity array length unavailable: backend array_length API is unavailable"); append_backend_error(); return out; }
    const std::size_t count = Backend::array_length(array); if (count == 0) { append_backend_error(); return out; }
    if (!Backend::has_array_ref_at()) { set_error("Unity array element access unavailable: backend array_ref_at API is unavailable"); append_backend_error(); return out; }
    out.reserve(count); for (std::size_t i=0; i<count; ++i) { void* item=Backend::array_ref_at(array, i); if (item) out.emplace_back(item); } return out;
}

template<class T, class... ExtraArgs> std::vector<T> detail::FindObjectsUsing(TypeRef owner, std::string_view methodName, std::string_view image, std::string_view namespc, std::string_view className, ExtraArgs&&... extraArgs) {
    std::vector<T> out; clear_error(); TypeRef target{image, namespc, className}; void* type = target.resolve_type_object(); if (!type) { set_error(std::string("Unity object finding failed: class not found: ")+std::string(image)+":"+std::string(namespc)+"."+std::string(className)); append_backend_error(); return out; } void* array = InvokeStatic<void*>(owner, methodName, TypeObject{type}, std::forward<ExtraArgs>(extraArgs)...);
    if (!array && methodName == "FindObjectsOfType" && sizeof...(ExtraArgs) == 0) { clear_error(); array = InvokeStatic<void*>(owner, "FindObjectsByType", TypeObject{type}, FindObjectsSortMode::None); }
    if (!array) { if (!fallback_error()) set_error(std::string("Unity object finding failed: returned array was null: ")+std::string(methodName)); return out; }
    if (!Backend::has_array_length()) { set_error("Unity object finding failed: backend array_length API is unavailable"); append_backend_error(); return out; }
    const std::size_t count = Backend::array_length(array); if (count == 0) { append_backend_error(); return out; }
    if (!Backend::has_array_ref_at()) { set_error("Unity object finding failed: Unity array element access unavailable: backend array_ref_at API is unavailable"); append_backend_error(); return out; }
    out.reserve(count); for (std::size_t i=0; i<count; ++i) { void* item=Backend::array_ref_at(array, i); if (item) out.emplace_back(item); } if(out.empty()) set_error(std::string("Unity object finding failed: no non-null instances found: ")+std::string(image)+":"+std::string(namespc)+"."+std::string(className)); return out;
}

template<class T> T Object::GetField(std::string_view fieldName) const { detail::clear_error(); detail::FieldOut<std::remove_cvref_t<T>> out{}; if(!handle_) { detail::set_error("Unity Object::GetField failed: target object is null"); return out.get(); } const void* k=detail::Backend::object_get_class(handle_); if(!k) { detail::set_error("Unity Object::GetField failed: object_get_class failed"); detail::append_backend_error(); return out.get(); } const void* f=detail::Backend::find_field(k, fieldName); if(!f) { detail::set_error(std::string("Unity Object::GetField failed: field not found: ")+std::string(fieldName)); detail::append_backend_error(); return out.get(); } if(!detail::Backend::field_get_value(handle_, f, out.ptr())) { detail::set_error(std::string("Unity Object::GetField failed: field read failed: ")+std::string(fieldName)); detail::append_backend_error(); } return out.get(); }
template<class T> void Object::SetField(std::string_view fieldName, T value) const { detail::clear_error(); if(!handle_) { detail::set_error("Unity Object::SetField failed: target object is null"); return; } const void* k=detail::Backend::object_get_class(handle_); if(!k) { detail::set_error("Unity Object::SetField failed: object_get_class failed"); detail::append_backend_error(); return; } const void* f=detail::Backend::find_field(k, fieldName); if(!f) { detail::set_error(std::string("Unity Object::SetField failed: field not found: ")+std::string(fieldName)); detail::append_backend_error(); return; } detail::FieldArg<std::remove_cvref_t<T>> arg(value); if(!detail::Backend::field_set_value(handle_, f, arg.ptr)) { detail::set_error(std::string("Unity Object::SetField failed: field write failed: ")+std::string(fieldName)); detail::append_backend_error(); } }
template<class T> T Object::StaticGetField(TypeRef type, std::string_view fieldName) { detail::clear_error(); detail::FieldOut<std::remove_cvref_t<T>> out{}; const void* k=type.resolve_class(); if(!k) { detail::set_error(std::string("Unity Object::StaticGetField failed: class not found for ")+std::string(type.namespc)+"."+std::string(type.name)); detail::append_backend_error(); return out.get(); } const void* f=detail::Backend::find_field(k, fieldName); if(!f) { detail::set_error(std::string("Unity Object::StaticGetField failed: field not found: ")+std::string(fieldName)); detail::append_backend_error(); return out.get(); } if(!detail::Backend::field_static_get_value(k, f, out.ptr())) { detail::set_error(std::string("Unity Object::StaticGetField failed: field read failed: ")+std::string(fieldName)); detail::append_backend_error(); } return out.get(); }
template<class T> void Object::StaticSetField(TypeRef type, std::string_view fieldName, T value) { detail::clear_error(); const void* k=type.resolve_class(); if(!k) { detail::set_error(std::string("Unity Object::StaticSetField failed: class not found for ")+std::string(type.namespc)+"."+std::string(type.name)); detail::append_backend_error(); return; } const void* f=detail::Backend::find_field(k, fieldName); if(!f) { detail::set_error(std::string("Unity Object::StaticSetField failed: field not found: ")+std::string(fieldName)); detail::append_backend_error(); return; } detail::FieldArg<std::remove_cvref_t<T>> arg(value); if(!detail::Backend::field_static_set_value(k, f, arg.ptr)) { detail::set_error(std::string("Unity Object::StaticSetField failed: field write failed: ")+std::string(fieldName)); detail::append_backend_error(); } }
template<class Ret> Ret Object::CallExact(std::string_view methodName, const std::vector<const char*>& parameterTypeNames, void** rawArgs) const { detail::clear_error(); if(!handle_) { detail::set_error("Unity Object::CallExact failed: target object is null"); return detail::from_result<Ret>(nullptr); } const void* k=detail::Backend::object_get_class(handle_); if(!k) { detail::set_error("Unity Object::CallExact failed: object_get_class failed"); detail::append_backend_error(); return detail::from_result<Ret>(nullptr); } const void* m=detail::Backend::find_method_exact(k, methodName, parameterTypeNames); if(!m) { const std::string lookupDetail = detail::fallback_error() ? detail::fallback_error() : ""; detail::set_error(std::string("Unity Object::CallExact failed: exact method not found: ")+detail::signature_text(methodName, parameterTypeNames)+(lookupDetail.empty() ? std::string{} : std::string("; detail: ")+lookupDetail)); detail::append_backend_error(); return detail::from_result<Ret>(nullptr); } void* result=nullptr; void* ex=nullptr; if(!detail::Backend::runtime_invoke(m,handle_,rawArgs,&result,&ex) || ex) { detail::set_error(std::string("Unity Object::CallExact failed: runtime_invoke exception in ")+detail::signature_text(methodName, parameterTypeNames)); detail::append_backend_error(); return detail::from_result<Ret>(nullptr); } return detail::from_result<Ret>(result); }
template<class Ret, class... Args> Ret Object::CallExact(std::string_view methodName, const std::vector<const char*>& parameterTypeNames, Args&&... args) const { auto pack=std::tuple<detail::Arg<std::remove_cvref_t<Args>>...>(detail::Arg<std::remove_cvref_t<Args>>(std::forward<Args>(args))...); std::array<void*, sizeof...(Args)> argv{}; std::size_t i=0; bool argsValid=true; std::apply([&](auto&... a){ ((argsValid=argsValid && a.valid, argv[i++]=a.ptr), ...); }, pack); if (!argsValid) { detail::clear_error(); detail::set_error(std::string("Unity Object::CallExact failed: managed string argument allocation failed in ")+std::string(methodName)); detail::append_backend_error(); return detail::from_result<Ret>(nullptr); } return CallExact<Ret>(methodName, parameterTypeNames, argv.data()); }
namespace detail {
inline TypeRef reflection_type_ref(std::string_view typeName) {
    const std::size_t separator = typeName.rfind('.');
    if (separator == std::string_view::npos)
        return {"", "", typeName};
    return {"", typeName.substr(0, separator), typeName.substr(separator + 1)};
}
template<class T> void* reflection_argument(Arg<T>& argument) {
    using Value = std::remove_cvref_t<T>;
    constexpr bool charArray = std::is_array_v<Value> &&
        std::is_same_v<std::remove_cv_t<std::remove_extent_t<Value>>, char>;
    if constexpr (is_wrapper_v<Value> || std::is_pointer_v<Value> ||
                  std::is_same_v<Value, std::string> ||
                  std::is_same_v<Value, std::string_view> || charArray) {
        return argument.ptr;
    } else {
        const auto typeName = parameter_type_name<Value>();
        if (!typeName) {
            set_error("Unity Object::InvokeGeneric failed: no managed type mapping exists for a value-type argument");
            return nullptr;
        }
        const TypeRef type = reflection_type_ref(*typeName);
        const void* klass = type.resolve_class();
        if (!klass) {
            set_error(std::string("Unity Object::InvokeGeneric failed: value-type class not found: ") + *typeName);
            append_backend_error();
            return nullptr;
        }
        void* boxed = Backend::value_box(klass, argument.ptr);
        if (!boxed) {
            set_error(std::string("Unity Object::InvokeGeneric failed: value-type boxing failed: ") + *typeName);
            append_backend_error();
        }
        return boxed;
    }
}
inline void* make_reflection_array(void* elementType, const std::vector<void*>& values) {
    if (!elementType) { set_error("Unity reflection array creation failed: element type is null"); return nullptr; }
    Object elementTypeObject{elementType};
    void* arrayType = elementTypeObject.CallExact<void*>("MakeArrayType", {});
    if (!arrayType) { if (!fallback_error()) set_error("Unity reflection array creation failed: Type.MakeArrayType returned null"); return nullptr; }
    TypeRef arrayTypeRef{"mscorlib", "System", "Array"};
    void* array = InvokeStatic<void*>(arrayTypeRef, "CreateInstance", TypeObject{arrayType}, static_cast<int>(values.size()));
    if (!array) { if (!fallback_error()) set_error("Unity reflection array creation failed: Array.CreateInstance returned null"); return nullptr; }
    if (!Backend::has_array_set_ref()) { set_error("Unity reflection array creation failed: backend array_set_ref API is unavailable"); append_backend_error(); return nullptr; }
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (!Backend::array_set_ref(array, i, values[i])) {
            set_error("Unity reflection array creation failed: array_set_ref rejected an element");
            append_backend_error();
            return nullptr;
        }
    }
    return array;
}
}
inline GameObject GameObject::CreateUi(std::string_view name) {
    detail::clear_error();
    const void* gameObjectClass = GameObjectType.resolve_class();
    if (!gameObjectClass) {
        detail::set_error("Unity GameObject::CreateUi failed: GameObject class not found");
        detail::append_backend_error();
        return {};
    }
    void* rectTransformType = RectTransformType.resolve_type_object();
    void* systemType = TypeRef{"mscorlib", "System", "Type"}.resolve_type_object();
    if (!rectTransformType || !systemType) {
        detail::set_error("Unity GameObject::CreateUi failed: RectTransform or System.Type metadata is unavailable");
        detail::append_backend_error();
        return {};
    }
    void* componentTypes = detail::make_reflection_array(systemType, {rectTransformType});
    if (!componentTypes)
        return {};
    const std::vector<const char*> signature{"System.String", "System.Type[]"};
    const void* constructor = detail::Backend::find_method_exact(gameObjectClass, ".ctor", signature);
    if (!constructor) {
        detail::set_error("Unity GameObject::CreateUi failed: GameObject(String, Type[]) constructor is unavailable");
        detail::append_backend_error();
        return {};
    }
    void* object = detail::Backend::object_new(gameObjectClass);
    void* managedName = detail::Backend::new_string(name);
    if (!object || !managedName) {
        detail::set_error("Unity GameObject::CreateUi failed: GameObject or managed name allocation failed");
        detail::append_backend_error();
        return {};
    }
    void* arguments[] = {managedName, componentTypes};
    void* exception = nullptr;
    if (!detail::Backend::runtime_invoke(constructor, object, arguments, nullptr, &exception) || exception) {
        detail::set_error("Unity GameObject::CreateUi failed: GameObject(String, Type[]) constructor threw or could not be invoked");
        detail::append_backend_error();
        return {};
    }
    return GameObject{object};
}
template<class T> void Object::SetReferenceArrayProperty(std::string_view propertyName, const std::vector<T>& values) const {
    static_assert(detail::is_wrapper_v<T>);
    detail::clear_error();
    if (!handle_) { detail::set_error("Unity Object::SetReferenceArrayProperty failed: target object is null"); return; }
    const TypeRef elementType = T::unity_type();
    void* elementTypeObject = elementType.resolve_type_object();
    if (!elementTypeObject) {
        detail::set_error(std::string("Unity Object::SetReferenceArrayProperty failed: element type not found: ") +
                          std::string(elementType.image) + ":" + std::string(elementType.namespc) + "." + std::string(elementType.name));
        detail::append_backend_error();
        return;
    }
    std::vector<void*> handles;
    handles.reserve(values.size());
    for (const T& value : values)
        handles.push_back(value.handle());
    void* array = detail::make_reflection_array(elementTypeObject, handles);
    if (!array)
        return;
    const std::string parameterType = std::string(elementType.namespc) + "." + std::string(elementType.name) + "[]";
    void* args[] = {array};
    CallExact<void>(std::string("set_") + std::string(propertyName), {parameterType.c_str()}, args);
}
template<class Ret, class... Args> Ret Object::InvokeGeneric(std::string_view methodName, const std::vector<TypeObject>& genericTypes, Args&&... args) const {
    detail::clear_error();
    if (!handle_) { detail::set_error("Unity Object::InvokeGeneric failed: target object is null"); return detail::from_result<Ret>(nullptr); }
    const void* klass = detail::Backend::object_get_class(handle_);
    if (!klass) { detail::set_error("Unity Object::InvokeGeneric failed: object_get_class failed"); detail::append_backend_error(); return detail::from_result<Ret>(nullptr); }
    const void* method = detail::Backend::find_method(klass, methodName, sizeof...(Args));
    if (!method) { detail::set_error(std::string("Unity Object::InvokeGeneric failed: generic method not found: ") + std::string(methodName)); detail::append_backend_error(); return detail::from_result<Ret>(nullptr); }
    void* methodInfo = detail::Backend::method_get_object(method, klass);
    if (!methodInfo) { detail::set_error("Unity Object::InvokeGeneric failed: backend cannot expose MethodInfo for generic method"); detail::append_backend_error(); return detail::from_result<Ret>(nullptr); }
    std::vector<void*> genericTypeObjects;
    genericTypeObjects.reserve(genericTypes.size());
    for (const TypeObject& type : genericTypes) genericTypeObjects.push_back(type.handle());
    if (genericTypeObjects.empty()) { detail::set_error("Unity Object::InvokeGeneric failed: at least one generic type is required"); return detail::from_result<Ret>(nullptr); }
    TypeRef typeRef{"mscorlib", "System", "Type"};
    void* typeArray = detail::make_reflection_array(typeRef.resolve_type_object(), genericTypeObjects);
    if (!typeArray) return detail::from_result<Ret>(nullptr);
    Object reflectionMethod{methodInfo};
    void* inflated = reflectionMethod.CallExact<void*>("MakeGenericMethod", {"System.Type[]"}, typeArray);
    if (!inflated) return detail::from_result<Ret>(nullptr);
    auto pack = std::tuple<detail::Arg<std::remove_cvref_t<Args>>...>(detail::Arg<std::remove_cvref_t<Args>>(std::forward<Args>(args))...);
    std::vector<void*> arguments;
    arguments.reserve(sizeof...(Args));
    std::apply([&](auto&... a) { (arguments.push_back(detail::reflection_argument(a)), ...); }, pack);
    if (detail::fallback_error()) return detail::from_result<Ret>(nullptr);
    void* objectType = TypeRef{"mscorlib", "System", "Object"}.resolve_type_object();
    void* argumentArray = detail::make_reflection_array(objectType, arguments);
    if (!argumentArray) return detail::from_result<Ret>(nullptr);
    Object inflatedMethod{inflated};
    void* result = inflatedMethod.CallExact<void*>("Invoke", {"System.Object", "System.Object[]"}, handle_, argumentArray);
    return detail::from_result<Ret>(result);
}
template<class T> std::vector<T> Object::CallArrayExact(std::string_view methodName, const std::vector<const char*>& parameterTypeNames, void** rawArgs) const { std::vector<T> out; detail::clear_error(); if(!handle_) { detail::set_error("Unity Object::CallArrayExact failed: target object is null"); return out; } const void* k=detail::Backend::object_get_class(handle_); if(!k) { detail::set_error("Unity Object::CallArrayExact failed: object_get_class failed"); detail::append_backend_error(); return out; } const void* m=detail::Backend::find_method_exact(k, methodName, parameterTypeNames); if(!m) { const std::string lookupDetail = detail::fallback_error() ? detail::fallback_error() : ""; detail::set_error(std::string("Unity Object::CallArrayExact failed: exact method not found: ")+detail::signature_text(methodName, parameterTypeNames)+(lookupDetail.empty() ? std::string{} : std::string("; detail: ")+lookupDetail)); detail::append_backend_error(); return out; } void* array=nullptr; void* ex=nullptr; if(!detail::Backend::runtime_invoke(m,handle_,rawArgs,&array,&ex) || ex) { detail::set_error(std::string("Unity Object::CallArrayExact failed: runtime_invoke exception in ")+detail::signature_text(methodName, parameterTypeNames)); detail::append_backend_error(); return out; } if(!array) { detail::set_error(std::string("Unity Object::CallArrayExact failed: returned array was null: ")+std::string(methodName)); detail::append_backend_error(); return out; } if(!detail::Backend::has_array_length()) { detail::set_error("Unity Object::CallArrayExact failed: backend array_length API is unavailable"); detail::append_backend_error(); return out; } const std::size_t count=detail::Backend::array_length(array); if(count==0) return out; if(!detail::Backend::has_array_ref_at()) { detail::set_error("Unity Object::CallArrayExact failed: backend array_ref_at API is unavailable"); detail::append_backend_error(); return out; } out.reserve(count); for(std::size_t i=0; i<count; ++i) { void* item=detail::Backend::array_ref_at(array, i); if(item) out.emplace_back(item); } return out; }
template<class T, class... Args> std::vector<T> Object::CallArrayExact(std::string_view methodName, const std::vector<const char*>& parameterTypeNames, Args&&... args) const { auto pack=std::tuple<detail::Arg<std::remove_cvref_t<Args>>...>(detail::Arg<std::remove_cvref_t<Args>>(std::forward<Args>(args))...); std::array<void*, sizeof...(Args)> argv{}; std::size_t i=0; bool argsValid=true; std::apply([&](auto&... a){ ((argsValid=argsValid && a.valid, argv[i++]=a.ptr), ...); }, pack); if (!argsValid) { detail::clear_error(); detail::set_error(std::string("Unity Object::CallArrayExact failed: managed string argument allocation failed in ")+std::string(methodName)); detail::append_backend_error(); return {}; } return CallArrayExact<T>(methodName, parameterTypeNames, argv.data()); }
inline std::vector<std::string> Object::CallStringArrayExact(std::string_view methodName, const std::vector<const char*>& parameterTypeNames) const { std::vector<std::string> out; detail::clear_error(); if(!handle_) { detail::set_error("Unity Object::CallStringArrayExact failed: target object is null"); return out; } const void* k=detail::Backend::object_get_class(handle_); if(!k) { detail::set_error("Unity Object::CallStringArrayExact failed: object_get_class failed"); detail::append_backend_error(); return out; } const void* m=detail::Backend::find_method_exact(k, methodName, parameterTypeNames); if(!m) { const std::string lookupDetail = detail::fallback_error() ? detail::fallback_error() : ""; detail::set_error(std::string("Unity Object::CallStringArrayExact failed: exact method not found: ")+detail::signature_text(methodName, parameterTypeNames)+(lookupDetail.empty() ? std::string{} : std::string("; detail: ")+lookupDetail)); detail::append_backend_error(); return out; } void* array=nullptr; void* ex=nullptr; if(!detail::Backend::runtime_invoke(m,handle_,nullptr,&array,&ex) || ex) { detail::set_error(std::string("Unity Object::CallStringArrayExact failed: runtime_invoke exception in ")+detail::signature_text(methodName, parameterTypeNames)); detail::append_backend_error(); return out; } if(!array) { detail::set_error(std::string("Unity Object::CallStringArrayExact failed: returned array was null: ")+std::string(methodName)); detail::append_backend_error(); return out; } if(!detail::Backend::has_array_length()) { detail::set_error("Unity Object::CallStringArrayExact failed: backend array_length API is unavailable"); detail::append_backend_error(); return out; } const std::size_t count=detail::Backend::array_length(array); if(count==0) return out; if(!detail::Backend::has_array_ref_at()) { detail::set_error("Unity Object::CallStringArrayExact failed: backend array_ref_at API is unavailable"); detail::append_backend_error(); return out; } out.reserve(count); for(std::size_t i=0; i<count; ++i) { void* item=detail::Backend::array_ref_at(array, i); if(!item) { detail::set_error(std::string("Unity Object::CallStringArrayExact failed: string array contains a null element in ")+std::string(methodName)); detail::append_backend_error(); out.clear(); return out; } out.emplace_back(detail::managed_string_to_utf8(item)); if(detail::fallback_error()) { out.clear(); return out; } } return out; }

}

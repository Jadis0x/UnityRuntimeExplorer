#pragma once
#include "unity_types.h"

namespace URK::Unity {
// URK_UNITY_COMPONENTS_BEGIN
struct Component : Object {
    Component() = default;
    explicit Component(void* h) : Object(h) {}
    static constexpr TypeRef unity_type(){ return ComponentType; }
    GameObject gameObject() const;
    Transform transform() const;
    template<class T> T GetComponent() const;
    template<class T> T GetComponent(const char* name) const;
    Object GetComponent(std::string_view image, std::string_view namespc, std::string_view className) const;
    template<class T> T GetComponentInChildren(bool includeInactive=false) const;
    Object GetComponentInChildren(std::string_view image, std::string_view namespc, std::string_view className, bool includeInactive=false) const;
    template<class T> T GetComponentInParent(bool includeInactive=false) const;
    Object GetComponentInParent(std::string_view image, std::string_view namespc, std::string_view className, bool includeInactive=false) const;
    template<class T = Object> std::vector<T> GetComponents() const;
    template<class T = Object> std::vector<T> GetComponentsInChildren(bool includeInactive=false) const;
    template<class T = Object> std::vector<T> GetComponentsInParent(bool includeInactive=false) const;
    template<class T> T AddComponent() const;
    Object AddComponent(std::string_view image, std::string_view namespc, std::string_view className) const;
    template<class T> bool HasComponent() const;
    bool HasComponent(std::string_view image, std::string_view namespc, std::string_view className) const;
    template<class T> T GetOrAddComponent() const;
    Object GetOrAddComponent(std::string_view image, std::string_view namespc, std::string_view className) const;
};
struct Behaviour : Component {
    Behaviour() = default;
    explicit Behaviour(void* h) : Component(h) {}
    static constexpr TypeRef unity_type(){ return BehaviourType; }
    bool enabled() const { return GetProperty<bool>("enabled"); }
    void set_enabled(bool value) const { SetProperty("enabled", value); }
    bool isActiveAndEnabled() const { return GetProperty<bool>("isActiveAndEnabled"); }
};
struct MonoBehaviour : Behaviour {
    MonoBehaviour() = default;
    explicit MonoBehaviour(void* h) : Behaviour(h) {}
    static constexpr TypeRef unity_type(){ return MonoBehaviourType; }
    bool useGUILayout() const { return GetProperty<bool>("useGUILayout"); }
    void set_useGUILayout(bool value) const { SetProperty("useGUILayout", value); }
};
struct ScriptableObject : Object {
    ScriptableObject() = default;
    explicit ScriptableObject(void* h) : Object(h) {}
    static constexpr TypeRef unity_type(){ return ScriptableObjectType; }
    static ScriptableObject CreateInstance(std::string_view image, std::string_view namespc, std::string_view className) { detail::clear_error(); void* type=TypeRef{image,namespc,className}.resolve_type_object(); if(!type){ detail::set_error(std::string("Unity ScriptableObject::CreateInstance failed: class not found: ")+std::string(image)+":"+std::string(namespc)+"."+std::string(className)); detail::append_backend_error(); return {}; } return ScriptableObject{detail::InvokeStatic<void*>(ScriptableObjectType, "CreateInstance", TypeObject{type})}; }
    static ScriptableObject CreateInstance(TypeRef type) { return CreateInstance(type.image, type.namespc, type.name); }
    template<class T> static T CreateInstance() { const TypeRef type = T::unity_type(); return T{CreateInstance(type.image, type.namespc, type.name).handle()}; }
};
struct Transform : Component {
    Transform() = default;
    explicit Transform(void* h) : Component(h) {}
    static constexpr TypeRef unity_type(){ return TransformType; }
    Vector3 position() const { return GetProperty<Vector3>("position"); }
    void set_position(Vector3 v) const { SetProperty("position", v); }
    Vector3 localPosition() const { return GetProperty<Vector3>("localPosition"); }
    void set_localPosition(Vector3 v) const { SetProperty("localPosition", v); }
    Vector3 eulerAngles() const { return GetProperty<Vector3>("eulerAngles"); }
    void set_eulerAngles(Vector3 v) const { SetProperty("eulerAngles", v); }
    Quaternion rotation() const { return GetProperty<Quaternion>("rotation"); }
    void set_rotation(Quaternion q) const { SetProperty("rotation", q); }
    Vector3 localScale() const { return GetProperty<Vector3>("localScale"); }
    void set_localScale(Vector3 value) const { SetProperty("localScale", value); }
    Vector3 forward() const { return GetProperty<Vector3>("forward"); }
    Vector3 right() const { return GetProperty<Vector3>("right"); }
    Vector3 up() const { return GetProperty<Vector3>("up"); }
    Vector3 lossyScale() const { return GetProperty<Vector3>("lossyScale"); }
    Transform parent() const { return GetProperty<Transform>("parent"); }
    void set_parent(Transform value) const { SetProperty("parent", value); }
    void SetParent(Transform value, bool worldPositionStays = true) const { CallExact<void>("SetParent", {"UnityEngine.Transform", "System.Boolean"}, value, worldPositionStays); }
    Transform root() const { return GetProperty<Transform>("root"); }
    int childCount() const { return GetProperty<int>("childCount"); }
    Transform GetChild(int index) const { return CallExact<Transform>("GetChild", {"System.Int32"}, index); }
    Transform Find(std::string_view path) const { return Call<Transform>("Find", path); }
};
struct Camera : Behaviour {
    Camera() = default;
    explicit Camera(void* h) : Behaviour(h) {}
    static constexpr TypeRef unity_type(){ return CameraType; }
    static Camera main() { return detail::InvokeStatic<Camera>(CameraType, "get_main"); }
    static Camera current() { return detail::InvokeStatic<Camera>(CameraType, "get_current"); }
    float fieldOfView() const { return GetProperty<float>("fieldOfView"); }
    void set_fieldOfView(float value) const { SetProperty("fieldOfView", value); }
    float nearClipPlane() const { return GetProperty<float>("nearClipPlane"); }
    float farClipPlane() const { return GetProperty<float>("farClipPlane"); }
    float aspect() const { return GetProperty<float>("aspect"); }
    int pixelWidth() const { return GetProperty<int>("pixelWidth"); }
    int pixelHeight() const { return GetProperty<int>("pixelHeight"); }
    Vector3 WorldToScreenPoint(Vector3 world) const { return CallExact<Vector3>("WorldToScreenPoint", {"UnityEngine.Vector3"}, world); }
    Vector3 ScreenToWorldPoint(Vector3 screen) const { return CallExact<Vector3>("ScreenToWorldPoint", {"UnityEngine.Vector3"}, screen); }
    Vector3 WorldToViewportPoint(Vector3 world) const { return CallExact<Vector3>("WorldToViewportPoint", {"UnityEngine.Vector3"}, world); }
    Vector3 ViewportToWorldPoint(Vector3 viewport) const { return CallExact<Vector3>("ViewportToWorldPoint", {"UnityEngine.Vector3"}, viewport); }
    Ray ScreenPointToRay(Vector3 screen) const { return CallExact<Ray>("ScreenPointToRay", {"UnityEngine.Vector3"}, screen); }
};
struct Mesh : Object {
    Mesh() = default;
    explicit Mesh(void* h) : Object(h) {}
    static constexpr TypeRef unity_type(){ return MeshType; }
};
struct Texture : Object {
    Texture() = default;
    explicit Texture(void* h) : Object(h) {}
    static constexpr TypeRef unity_type(){ return TextureType; }
    int width() const { return GetProperty<int>("width"); }
    int height() const { return GetProperty<int>("height"); }
    int anisoLevel() const { return GetProperty<int>("anisoLevel"); }
    void set_anisoLevel(int value) const { SetProperty("anisoLevel", value); }
    float mipMapBias() const { return GetProperty<float>("mipMapBias"); }
    void set_mipMapBias(float value) const { SetProperty("mipMapBias", value); }
};
struct Texture2D : Texture {
    Texture2D() = default;
    explicit Texture2D(void* h) : Texture(h) {}
    static constexpr TypeRef unity_type(){ return Texture2DType; }
    int mipmapCount() const { return GetProperty<int>("mipmapCount"); }
    Color GetPixel(int x, int y) const { return CallExact<Color>("GetPixel", {"System.Int32", "System.Int32"}, x, y); }
    void SetPixel(int x, int y, Color color) const { CallExact<void>("SetPixel", {"System.Int32", "System.Int32", "UnityEngine.Color"}, x, y, color); }
    bool Resize(int width, int height) const { return CallExact<bool>("Resize", {"System.Int32", "System.Int32"}, width, height); }
    void Apply() const { Apply(true, false); }
    void Apply(bool updateMipmaps, bool makeNoLongerReadable = false) const { CallExact<void>("Apply", {"System.Boolean", "System.Boolean"}, updateMipmaps, makeNoLongerReadable); }
};
struct Shader : Object {
    Shader() = default;
    explicit Shader(void* h) : Object(h) {}
    static constexpr TypeRef unity_type(){ return ShaderType; }
    static Shader Find(std::string_view name) { return detail::InvokeStatic<Shader>(ShaderType, "Find", name); }
    static int PropertyToID(std::string_view name) { return detail::InvokeStatic<int>(ShaderType, "PropertyToID", name); }
    bool isSupported() const { return GetProperty<bool>("isSupported"); }
    int maximumLOD() const { return GetProperty<int>("maximumLOD"); }
    void set_maximumLOD(int value) const { SetProperty("maximumLOD", value); }
    int renderQueue() const { return GetProperty<int>("renderQueue"); }
};
struct Material : Object {
    Material() = default;
    explicit Material(void* h) : Object(h) {}
    static constexpr TypeRef unity_type(){ return MaterialType; }
    Shader shader() const { return GetProperty<Shader>("shader"); }
    void set_shader(Shader value) const { SetProperty("shader", value); }
    Color color() const { return GetProperty<Color>("color"); }
    void set_color(Color value) const { SetProperty("color", value); }
    Texture mainTexture() const { return GetProperty<Texture>("mainTexture"); }
    void set_mainTexture(Texture value) const { SetProperty("mainTexture", value); }
    float GetFloat(std::string_view name) const { return CallExact<float>("GetFloat", {"System.String"}, name); }
    void SetFloat(std::string_view name, float value) const { CallExact<void>("SetFloat", {"System.String", "System.Single"}, name, value); }
    Color GetColor(std::string_view name) const { return CallExact<Color>("GetColor", {"System.String"}, name); }
    void SetColor(std::string_view name, Color value) const { CallExact<void>("SetColor", {"System.String", "UnityEngine.Color"}, name, value); }
    Texture GetTexture(std::string_view name) const { return CallExact<Texture>("GetTexture", {"System.String"}, name); }
    void SetTexture(std::string_view name, Texture value) const { CallExact<void>("SetTexture", {"System.String", "UnityEngine.Texture"}, name, value); }
    bool HasProperty(std::string_view name) const { return CallExact<bool>("HasProperty", {"System.String"}, name); }
};
struct Light : Behaviour {
    Light() = default;
    explicit Light(void* h) : Behaviour(h) {}
    static constexpr TypeRef unity_type(){ return LightTypeRef; }
    LightType type() const { return GetProperty<LightType>("type"); }
    void set_type(LightType value) const { CallExact<void>("set_type", {"UnityEngine.LightType"}, value); }
    Color color() const { return GetProperty<Color>("color"); }
    void set_color(Color value) const { SetProperty("color", value); }
    float colorTemperature() const { return GetProperty<float>("colorTemperature"); }
    void set_colorTemperature(float value) const { SetProperty("colorTemperature", value); }
    bool useColorTemperature() const { return GetProperty<bool>("useColorTemperature"); }
    void set_useColorTemperature(bool value) const { SetProperty("useColorTemperature", value); }
    float intensity() const { return GetProperty<float>("intensity"); }
    void set_intensity(float value) const { SetProperty("intensity", value); }
    float bounceIntensity() const { return GetProperty<float>("bounceIntensity"); }
    void set_bounceIntensity(float value) const { SetProperty("bounceIntensity", value); }
    float range() const { return GetProperty<float>("range"); }
    void set_range(float value) const { SetProperty("range", value); }
    float spotAngle() const { return GetProperty<float>("spotAngle"); }
    void set_spotAngle(float value) const { SetProperty("spotAngle", value); }
    float innerSpotAngle() const { return GetProperty<float>("innerSpotAngle"); }
    void set_innerSpotAngle(float value) const { SetProperty("innerSpotAngle", value); }
    Texture cookie() const { return GetProperty<Texture>("cookie"); }
    void set_cookie(Texture value) const { SetProperty("cookie", value); }
    float cookieSize() const { return GetProperty<float>("cookieSize"); }
    void set_cookieSize(float value) const { SetProperty("cookieSize", value); }
    LightShadows shadows() const { return GetProperty<LightShadows>("shadows"); }
    void set_shadows(LightShadows value) const { CallExact<void>("set_shadows", {"UnityEngine.LightShadows"}, value); }
    float shadowStrength() const { return GetProperty<float>("shadowStrength"); }
    void set_shadowStrength(float value) const { SetProperty("shadowStrength", value); }
    LightShadowResolution shadowResolution() const { return GetProperty<LightShadowResolution>("shadowResolution"); }
    void set_shadowResolution(LightShadowResolution value) const { CallExact<void>("set_shadowResolution", {"UnityEngine.LightShadowResolution"}, value); }
    float shadowBias() const { return GetProperty<float>("shadowBias"); }
    void set_shadowBias(float value) const { SetProperty("shadowBias", value); }
    float shadowNormalBias() const { return GetProperty<float>("shadowNormalBias"); }
    void set_shadowNormalBias(float value) const { SetProperty("shadowNormalBias", value); }
    float shadowNearPlane() const { return GetProperty<float>("shadowNearPlane"); }
    void set_shadowNearPlane(float value) const { SetProperty("shadowNearPlane", value); }
    int cullingMask() const { return GetProperty<int>("cullingMask"); }
    void set_cullingMask(int value) const { SetProperty("cullingMask", value); }
    LightRenderMode renderMode() const { return GetProperty<LightRenderMode>("renderMode"); }
    void set_renderMode(LightRenderMode value) const { CallExact<void>("set_renderMode", {"UnityEngine.LightRenderMode"}, value); }
};
struct Renderer : Component {
    Renderer() = default;
    explicit Renderer(void* h) : Component(h) {}
    static constexpr TypeRef unity_type(){ return RendererType; }
    Bounds bounds() const { return GetProperty<Bounds>("bounds"); }
    Bounds localBounds() const { return GetProperty<Bounds>("localBounds"); }
    void set_localBounds(Bounds value) const { SetProperty("localBounds", value); }
    bool enabled() const { return GetProperty<bool>("enabled"); }
    void set_enabled(bool value) const { SetProperty("enabled", value); }
    bool isVisible() const { return GetProperty<bool>("isVisible"); }
    bool forceRenderingOff() const { return GetProperty<bool>("forceRenderingOff"); }
    void set_forceRenderingOff(bool value) const { SetProperty("forceRenderingOff", value); }
    bool receiveShadows() const { return GetProperty<bool>("receiveShadows"); }
    void set_receiveShadows(bool value) const { SetProperty("receiveShadows", value); }
    bool allowOcclusionWhenDynamic() const { return GetProperty<bool>("allowOcclusionWhenDynamic"); }
    void set_allowOcclusionWhenDynamic(bool value) const { SetProperty("allowOcclusionWhenDynamic", value); }
    int sortingLayerID() const { return GetProperty<int>("sortingLayerID"); }
    void set_sortingLayerID(int value) const { SetProperty("sortingLayerID", value); }
    int sortingOrder() const { return GetProperty<int>("sortingOrder"); }
    void set_sortingOrder(int value) const { SetProperty("sortingOrder", value); }
    Material material() const { return GetProperty<Material>("material"); }
    void set_material(Material value) const { SetProperty("material", value); }
    Material sharedMaterial() const { return GetProperty<Material>("sharedMaterial"); }
    void set_sharedMaterial(Material value) const { SetProperty("sharedMaterial", value); }
    std::vector<Material> materials() const { return CallArrayExact<Material>("get_materials", {}); }
    std::vector<Material> sharedMaterials() const { return CallArrayExact<Material>("get_sharedMaterials", {}); }
    void set_materials(const std::vector<Material>& values) const { SetReferenceArrayProperty("materials", values); }
    void set_sharedMaterials(const std::vector<Material>& values) const { SetReferenceArrayProperty("sharedMaterials", values); }
    ShadowCastingMode shadowCastingMode() const { return GetProperty<ShadowCastingMode>("shadowCastingMode"); }
    void set_shadowCastingMode(ShadowCastingMode value) const { CallExact<void>("set_shadowCastingMode", {"UnityEngine.Rendering.ShadowCastingMode"}, value); }
    MotionVectorGenerationMode motionVectorGenerationMode() const { return GetProperty<MotionVectorGenerationMode>("motionVectorGenerationMode"); }
    void set_motionVectorGenerationMode(MotionVectorGenerationMode value) const { CallExact<void>("set_motionVectorGenerationMode", {"UnityEngine.MotionVectorGenerationMode"}, value); }
    LightProbeUsage lightProbeUsage() const { return GetProperty<LightProbeUsage>("lightProbeUsage"); }
    void set_lightProbeUsage(LightProbeUsage value) const { CallExact<void>("set_lightProbeUsage", {"UnityEngine.Rendering.LightProbeUsage"}, value); }
    ReflectionProbeUsage reflectionProbeUsage() const { return GetProperty<ReflectionProbeUsage>("reflectionProbeUsage"); }
    void set_reflectionProbeUsage(ReflectionProbeUsage value) const { CallExact<void>("set_reflectionProbeUsage", {"UnityEngine.Rendering.ReflectionProbeUsage"}, value); }
};
struct SkinnedMeshRenderer : Renderer {
    SkinnedMeshRenderer() = default;
    explicit SkinnedMeshRenderer(void* h) : Renderer(h) {}
    static constexpr TypeRef unity_type(){ return SkinnedMeshRendererType; }
    Mesh sharedMesh() const { return GetProperty<Mesh>("sharedMesh"); }
    void set_sharedMesh(Mesh value) const { SetProperty("sharedMesh", value); }
    std::vector<Transform> bones() const { return CallArrayExact<Transform>("get_bones", {}); }
    void set_bones(const std::vector<Transform>& values) const { SetReferenceArrayProperty("bones", values); }
    Transform rootBone() const { return GetProperty<Transform>("rootBone"); }
    void set_rootBone(Transform value) const { SetProperty("rootBone", value); }
    int blendShapeCount() const { return GetProperty<int>("blendShapeCount"); }
    float GetBlendShapeWeight(int index) const { return CallExact<float>("GetBlendShapeWeight", {"System.Int32"}, index); }
    void SetBlendShapeWeight(int index, float value) const { CallExact<void>("SetBlendShapeWeight", {"System.Int32", "System.Single"}, index, value); }
    SkinQuality quality() const { return GetProperty<SkinQuality>("quality"); }
    void set_quality(SkinQuality value) const { CallExact<void>("set_quality", {"UnityEngine.SkinQuality"}, value); }
    bool updateWhenOffscreen() const { return GetProperty<bool>("updateWhenOffscreen"); }
    void set_updateWhenOffscreen(bool value) const { SetProperty("updateWhenOffscreen", value); }
    bool forceMatrixRecalculationPerRender() const { return GetProperty<bool>("forceMatrixRecalculationPerRender"); }
    void set_forceMatrixRecalculationPerRender(bool value) const { SetProperty("forceMatrixRecalculationPerRender", value); }
    bool skinnedMotionVectors() const { return GetProperty<bool>("skinnedMotionVectors"); }
    void set_skinnedMotionVectors(bool value) const { SetProperty("skinnedMotionVectors", value); }
    void BakeMesh(Mesh mesh) const { CallExact<void>("BakeMesh", {"UnityEngine.Mesh"}, mesh); }
};
struct MeshRenderer : Renderer {
    MeshRenderer() = default;
    explicit MeshRenderer(void* h) : Renderer(h) {}
    static constexpr TypeRef unity_type(){ return MeshRendererType; }
};
struct MeshFilter : Component {
    MeshFilter() = default;
    explicit MeshFilter(void* h) : Component(h) {}
    static constexpr TypeRef unity_type(){ return MeshFilterType; }
    Mesh mesh() const { return GetProperty<Mesh>("mesh"); }
    void set_mesh(Mesh value) const { SetProperty("mesh", value); }
    Mesh sharedMesh() const { return GetProperty<Mesh>("sharedMesh"); }
    void set_sharedMesh(Mesh value) const { SetProperty("sharedMesh", value); }
};
struct Collider : Component {
    Collider() = default;
    explicit Collider(void* h) : Component(h) {}
    static constexpr TypeRef unity_type(){ return ColliderType; }
    Bounds bounds() const { return GetProperty<Bounds>("bounds"); }
    bool enabled() const { return GetProperty<bool>("enabled"); }
    void set_enabled(bool value) const { SetProperty("enabled", value); }
};
struct MeshCollider : Collider {
    MeshCollider() = default;
    explicit MeshCollider(void* h) : Collider(h) {}
    static constexpr TypeRef unity_type(){ return MeshColliderType; }
    Mesh sharedMesh() const { return GetProperty<Mesh>("sharedMesh"); }
    void set_sharedMesh(Mesh value) const { SetProperty("sharedMesh", value); }
    bool convex() const { return GetProperty<bool>("convex"); }
    void set_convex(bool value) const { SetProperty("convex", value); }
};
struct RectTransform : Transform {
    RectTransform() = default;
    explicit RectTransform(void* h) : Transform(h) {}
    static constexpr TypeRef unity_type(){ return RectTransformType; }
    Vector2 anchoredPosition() const { return GetProperty<Vector2>("anchoredPosition"); }
    void set_anchoredPosition(Vector2 value) const { SetProperty("anchoredPosition", value); }
    Vector3 anchoredPosition3D() const { return GetProperty<Vector3>("anchoredPosition3D"); }
    void set_anchoredPosition3D(Vector3 value) const { SetProperty("anchoredPosition3D", value); }
    Vector2 anchorMin() const { return GetProperty<Vector2>("anchorMin"); }
    void set_anchorMin(Vector2 value) const { SetProperty("anchorMin", value); }
    Vector2 anchorMax() const { return GetProperty<Vector2>("anchorMax"); }
    void set_anchorMax(Vector2 value) const { SetProperty("anchorMax", value); }
    Vector2 pivot() const { return GetProperty<Vector2>("pivot"); }
    void set_pivot(Vector2 value) const { SetProperty("pivot", value); }
    Vector2 sizeDelta() const { return GetProperty<Vector2>("sizeDelta"); }
    void set_sizeDelta(Vector2 value) const { SetProperty("sizeDelta", value); }
    Vector2 offsetMin() const { return GetProperty<Vector2>("offsetMin"); }
    void set_offsetMin(Vector2 value) const { SetProperty("offsetMin", value); }
    Vector2 offsetMax() const { return GetProperty<Vector2>("offsetMax"); }
    void set_offsetMax(Vector2 value) const { SetProperty("offsetMax", value); }
    Rect rect() const { return GetProperty<Rect>("rect"); }
    void SetInsetAndSizeFromParentEdge(RectTransformEdge edge, float inset, float size) const { CallExact<void>("SetInsetAndSizeFromParentEdge", {"UnityEngine.RectTransform+Edge", "System.Single", "System.Single"}, edge, inset, size); }
    void SetSizeWithCurrentAnchors(RectTransformAxis axis, float size) const { CallExact<void>("SetSizeWithCurrentAnchors", {"UnityEngine.RectTransform+Axis", "System.Single"}, axis, size); }
};
struct Rigidbody : Component {
    Rigidbody() = default;
    explicit Rigidbody(void* h) : Component(h) {}
    static constexpr TypeRef unity_type(){ return RigidbodyType; }
    Vector3 velocity() const { return GetProperty<Vector3>("velocity"); }
    void set_velocity(Vector3 value) const { SetProperty("velocity", value); }
    float angularVelocity() const { return GetProperty<float>("angularVelocity"); }
};
struct Rigidbody2D : Component {
    Rigidbody2D() = default;
    explicit Rigidbody2D(void* h) : Component(h) {}
    static constexpr TypeRef unity_type(){ return Rigidbody2DType; }
    Vector2 velocity() const { return GetProperty<Vector2>("velocity"); }
    void set_velocity(Vector2 value) const { SetProperty("velocity", value); }
    float angularVelocity() const { return GetProperty<float>("angularVelocity"); }
};
struct AudioSource : Behaviour {
    AudioSource() = default;
    explicit AudioSource(void* h) : Behaviour(h) {}
    static constexpr TypeRef unity_type(){ return AudioSourceType; }
    Object clip() const { return GetProperty<Object>("clip"); }
    void set_clip(Object value) const { SetProperty("clip", value); }
    float volume() const { return GetProperty<float>("volume"); }
    void set_volume(float value) const { SetProperty("volume", value); }
    float pitch() const { return GetProperty<float>("pitch"); }
    void set_pitch(float value) const { SetProperty("pitch", value); }
    float spatialBlend() const { return GetProperty<float>("spatialBlend"); }
    void set_spatialBlend(float value) const { SetProperty("spatialBlend", value); }
    float time() const { return GetProperty<float>("time"); }
    void set_time(float value) const { SetProperty("time", value); }
    bool loop() const { return GetProperty<bool>("loop"); }
    void set_loop(bool value) const { SetProperty("loop", value); }
    bool mute() const { return GetProperty<bool>("mute"); }
    void set_mute(bool value) const { SetProperty("mute", value); }
    bool playOnAwake() const { return GetProperty<bool>("playOnAwake"); }
    void set_playOnAwake(bool value) const { SetProperty("playOnAwake", value); }
    bool isPlaying() const { return GetProperty<bool>("isPlaying"); }
    void Play() const { Call<void>("Play"); }
    void PlayDelayed(float delaySeconds) const { CallExact<void>("PlayDelayed", {"System.Single"}, delaySeconds); }
    void PlayOneShot(Object audioClip) const { CallExact<void>("PlayOneShot", {"UnityEngine.AudioClip"}, audioClip); }
    void PlayOneShot(Object audioClip, float volumeScale) const { CallExact<void>("PlayOneShot", {"UnityEngine.AudioClip", "System.Single"}, audioClip, volumeScale); }
    void Pause() const { Call<void>("Pause"); }
    void UnPause() const { Call<void>("UnPause"); }
    void Stop() const { Call<void>("Stop"); }
};
struct Animator : Behaviour {
    Animator() = default;
    explicit Animator(void* h) : Behaviour(h) {}
    static constexpr TypeRef unity_type(){ return AnimatorType; }
    float speed() const { return GetProperty<float>("speed"); }
    void set_speed(float value) const { SetProperty("speed", value); }
    bool applyRootMotion() const { return GetProperty<bool>("applyRootMotion"); }
    void set_applyRootMotion(bool value) const { SetProperty("applyRootMotion", value); }
    Vector3 deltaPosition() const { return GetProperty<Vector3>("deltaPosition"); }
    Quaternion deltaRotation() const { return GetProperty<Quaternion>("deltaRotation"); }
    bool isHuman() const { return GetProperty<bool>("isHuman"); }
    bool hasRootMotion() const { return GetProperty<bool>("hasRootMotion"); }
    bool isInitialized() const { return GetProperty<bool>("isInitialized"); }
    int layerCount() const { return GetProperty<int>("layerCount"); }
    AnimatorCullingMode cullingMode() const { return GetProperty<AnimatorCullingMode>("cullingMode"); }
    void set_cullingMode(AnimatorCullingMode value) const { CallExact<void>("set_cullingMode", {"UnityEngine.AnimatorCullingMode"}, value); }
    AnimatorUpdateMode updateMode() const { return GetProperty<AnimatorUpdateMode>("updateMode"); }
    void set_updateMode(AnimatorUpdateMode value) const { CallExact<void>("set_updateMode", {"UnityEngine.AnimatorUpdateMode"}, value); }
    bool fireEvents() const { return GetProperty<bool>("fireEvents"); }
    void set_fireEvents(bool value) const { SetProperty("fireEvents", value); }
    Object avatar() const { return GetProperty<Object>("avatar"); }
    void set_avatar(Object value) const { SetProperty("avatar", value); }
    Object runtimeAnimatorController() const { return GetProperty<Object>("runtimeAnimatorController"); }
    void set_runtimeAnimatorController(Object value) const { SetProperty("runtimeAnimatorController", value); }
    float GetFloat(std::string_view name) const { return CallExact<float>("GetFloat", {"System.String"}, name); }
    float GetFloat(int id) const { return CallExact<float>("GetFloat", {"System.Int32"}, id); }
    void SetFloat(std::string_view name, float value) const { CallExact<void>("SetFloat", {"System.String", "System.Single"}, name, value); }
    void SetFloat(int id, float value) const { CallExact<void>("SetFloat", {"System.Int32", "System.Single"}, id, value); }
    int GetInteger(std::string_view name) const { return CallExact<int>("GetInteger", {"System.String"}, name); }
    int GetInteger(int id) const { return CallExact<int>("GetInteger", {"System.Int32"}, id); }
    void SetInteger(std::string_view name, int value) const { CallExact<void>("SetInteger", {"System.String", "System.Int32"}, name, value); }
    void SetInteger(int id, int value) const { CallExact<void>("SetInteger", {"System.Int32", "System.Int32"}, id, value); }
    bool GetBool(std::string_view name) const { return CallExact<bool>("GetBool", {"System.String"}, name); }
    bool GetBool(int id) const { return CallExact<bool>("GetBool", {"System.Int32"}, id); }
    void SetBool(std::string_view name, bool value) const { CallExact<void>("SetBool", {"System.String", "System.Boolean"}, name, value); }
    void SetBool(int id, bool value) const { CallExact<void>("SetBool", {"System.Int32", "System.Boolean"}, id, value); }
    void SetTrigger(std::string_view name) const { CallExact<void>("SetTrigger", {"System.String"}, name); }
    void SetTrigger(int id) const { CallExact<void>("SetTrigger", {"System.Int32"}, id); }
    void ResetTrigger(std::string_view name) const { CallExact<void>("ResetTrigger", {"System.String"}, name); }
    void ResetTrigger(int id) const { CallExact<void>("ResetTrigger", {"System.Int32"}, id); }
    int GetLayerIndex(std::string_view name) const { return CallExact<int>("GetLayerIndex", {"System.String"}, name); }
    std::string GetLayerName(int index) const { return CallExact<std::string>("GetLayerName", {"System.Int32"}, index); }
    float GetLayerWeight(int index) const { return CallExact<float>("GetLayerWeight", {"System.Int32"}, index); }
    void SetLayerWeight(int index, float value) const { CallExact<void>("SetLayerWeight", {"System.Int32", "System.Single"}, index, value); }
    void Play(std::string_view stateName, int layer = -1, float normalizedTime = float(-std::numeric_limits<float>::infinity())) const { CallExact<void>("Play", {"System.String", "System.Int32", "System.Single"}, stateName, layer, normalizedTime); }
    void Play(int stateHash, int layer = -1, float normalizedTime = float(-std::numeric_limits<float>::infinity())) const { CallExact<void>("Play", {"System.Int32", "System.Int32", "System.Single"}, stateHash, layer, normalizedTime); }
    void CrossFade(std::string_view stateName, float transitionDuration, int layer = -1, float normalizedTime = float(-std::numeric_limits<float>::infinity())) const { CallExact<void>("CrossFade", {"System.String", "System.Single", "System.Int32", "System.Single"}, stateName, transitionDuration, layer, normalizedTime); }
    void CrossFade(int stateHash, float transitionDuration, int layer = -1, float normalizedTime = float(-std::numeric_limits<float>::infinity())) const { CallExact<void>("CrossFade", {"System.Int32", "System.Single", "System.Int32", "System.Single"}, stateHash, transitionDuration, layer, normalizedTime); }
    static int StringToHash(std::string_view name) { return detail::InvokeStatic<int>(AnimatorType, "StringToHash", name); }
    void Update(float deltaTime) const { CallExact<void>("Update", {"System.Single"}, deltaTime); }
    void Rebind() const { Call<void>("Rebind"); }
};
struct Canvas : Behaviour {
    Canvas() = default;
    explicit Canvas(void* h) : Behaviour(h) {}
    static constexpr TypeRef unity_type(){ return CanvasType; }
    static void ForceUpdateCanvases() { detail::InvokeStatic<void>(CanvasType, "ForceUpdateCanvases"); }
    CanvasRenderMode renderMode() const { return GetProperty<CanvasRenderMode>("renderMode"); }
    void set_renderMode(CanvasRenderMode value) const { SetProperty("renderMode", value); }
    Camera worldCamera() const { return GetProperty<Camera>("worldCamera"); }
    void set_worldCamera(Camera value) const { SetProperty("worldCamera", value); }
    float planeDistance() const { return GetProperty<float>("planeDistance"); }
    void set_planeDistance(float value) const { SetProperty("planeDistance", value); }
    bool pixelPerfect() const { return GetProperty<bool>("pixelPerfect"); }
    void set_pixelPerfect(bool value) const { SetProperty("pixelPerfect", value); }
    float scaleFactor() const { return GetProperty<float>("scaleFactor"); }
    void set_scaleFactor(float value) const { SetProperty("scaleFactor", value); }
    int sortingOrder() const { return GetProperty<int>("sortingOrder"); }
    void set_sortingOrder(int value) const { SetProperty("sortingOrder", value); }
    bool overrideSorting() const { return GetProperty<bool>("overrideSorting"); }
    void set_overrideSorting(bool value) const { SetProperty("overrideSorting", value); }
    int targetDisplay() const { return GetProperty<int>("targetDisplay"); }
    void set_targetDisplay(int value) const { SetProperty("targetDisplay", value); }
};
struct CanvasRenderer : Component {
    CanvasRenderer() = default;
    explicit CanvasRenderer(void* h) : Component(h) {}
    static constexpr TypeRef unity_type(){ return CanvasRendererType; }
    bool cull() const { return GetProperty<bool>("cull"); }
    void set_cull(bool value) const { SetProperty("cull", value); }
    float GetAlpha() const { return Call<float>("GetAlpha"); }
    void SetAlpha(float value) const { CallExact<void>("SetAlpha", {"System.Single"}, value); }
};
struct GraphicRaycaster : Behaviour {
    GraphicRaycaster() = default;
    explicit GraphicRaycaster(void* h) : Behaviour(h) {}
    static constexpr TypeRef unity_type(){ return GraphicRaycasterType; }
    bool ignoreReversedGraphics() const { return GetProperty<bool>("ignoreReversedGraphics"); }
    void set_ignoreReversedGraphics(bool value) const { SetProperty("ignoreReversedGraphics", value); }
    GraphicRaycasterBlockingObjects blockingObjects() const { return GetProperty<GraphicRaycasterBlockingObjects>("blockingObjects"); }
    void set_blockingObjects(GraphicRaycasterBlockingObjects value) const { SetProperty("blockingObjects", value); }
    int blockingMask() const { return GetProperty<int>("blockingMask"); }
    void set_blockingMask(int value) const { SetProperty("blockingMask", value); }
};
struct Sprite : Object { Sprite() = default; explicit Sprite(void* h) : Object(h) {} static constexpr TypeRef unity_type(){ return SpriteType; } };
struct Graphic : Behaviour {
    Graphic() = default;
    explicit Graphic(void* h) : Behaviour(h) {}
    static constexpr TypeRef unity_type(){ return GraphicType; }
    Color color() const { return GetProperty<Color>("color"); }
    void set_color(Color value) const { SetProperty("color", value); }
    Material material() const { return GetProperty<Material>("material"); }
    void set_material(Material value) const { SetProperty("material", value); }
    bool raycastTarget() const { return GetProperty<bool>("raycastTarget"); }
    void set_raycastTarget(bool value) const { SetProperty("raycastTarget", value); }
};
struct Image : Graphic {
    Image() = default;
    explicit Image(void* h) : Graphic(h) {}
    static constexpr TypeRef unity_type(){ return ImageTypeRef; }
    Sprite sprite() const { return GetProperty<Sprite>("sprite"); }
    void set_sprite(Sprite value) const { SetProperty("sprite", value); }
    Sprite overrideSprite() const { return GetProperty<Sprite>("overrideSprite"); }
    void set_overrideSprite(Sprite value) const { SetProperty("overrideSprite", value); }
    ImageType type() const { return GetProperty<ImageType>("type"); }
    void set_type(ImageType value) const { SetProperty("type", value); }
    bool preserveAspect() const { return GetProperty<bool>("preserveAspect"); }
    void set_preserveAspect(bool value) const { SetProperty("preserveAspect", value); }
    float fillAmount() const { return GetProperty<float>("fillAmount"); }
    void set_fillAmount(float value) const { SetProperty("fillAmount", value); }
    ImageFillMethod fillMethod() const { return GetProperty<ImageFillMethod>("fillMethod"); }
    void set_fillMethod(ImageFillMethod value) const { SetProperty("fillMethod", value); }
    int fillOrigin() const { return GetProperty<int>("fillOrigin"); }
    void set_fillOrigin(int value) const { SetProperty("fillOrigin", value); }
    bool fillClockwise() const { return GetProperty<bool>("fillClockwise"); }
    void set_fillClockwise(bool value) const { SetProperty("fillClockwise", value); }
};
struct Text : Graphic {
    Text() = default;
    explicit Text(void* h) : Graphic(h) {}
    static constexpr TypeRef unity_type(){ return TextType; }
    std::string text() const { return GetProperty<std::string>("text"); }
    void set_text(std::string_view value) const { SetProperty("text", value); }
    Object font() const { return GetProperty<Object>("font"); }
    void set_font(Object value) const { SetProperty("font", value); }
    int fontSize() const { return GetProperty<int>("fontSize"); }
    void set_fontSize(int value) const { SetProperty("fontSize", value); }
    FontStyle fontStyle() const { return GetProperty<FontStyle>("fontStyle"); }
    void set_fontStyle(FontStyle value) const { SetProperty("fontStyle", value); }
    TextAnchor alignment() const { return GetProperty<TextAnchor>("alignment"); }
    void set_alignment(TextAnchor value) const { SetProperty("alignment", value); }
    bool supportRichText() const { return GetProperty<bool>("supportRichText"); }
    void set_supportRichText(bool value) const { SetProperty("supportRichText", value); }
    float lineSpacing() const { return GetProperty<float>("lineSpacing"); }
    void set_lineSpacing(float value) const { SetProperty("lineSpacing", value); }
    bool resizeTextForBestFit() const { return GetProperty<bool>("resizeTextForBestFit"); }
    void set_resizeTextForBestFit(bool value) const { SetProperty("resizeTextForBestFit", value); }
};
struct Selectable : Behaviour {
    Selectable() = default;
    explicit Selectable(void* h) : Behaviour(h) {}
    static constexpr TypeRef unity_type(){ return SelectableType; }
    bool interactable() const { return GetProperty<bool>("interactable"); }
    void set_interactable(bool value) const { SetProperty("interactable", value); }
    SelectableTransition transition() const { return GetProperty<SelectableTransition>("transition"); }
    void set_transition(SelectableTransition value) const { SetProperty("transition", value); }
    Graphic targetGraphic() const { return GetProperty<Graphic>("targetGraphic"); }
    void set_targetGraphic(Graphic value) const { SetProperty("targetGraphic", value); }
    bool IsInteractable() const { return Call<bool>("IsInteractable"); }
    void Select() const { Call<void>("Select"); }
};
struct Button : Selectable {
    Button() = default;
    explicit Button(void* h) : Selectable(h) {}
    static constexpr TypeRef unity_type(){ return ButtonType; }
    Image image() const { return GetProperty<Image>("image"); }
    Object onClick() const { return GetProperty<Object>("onClick"); }
    void Click() const { onClick().Call<void>("Invoke"); }
};
struct RawImage : Graphic {
    RawImage() = default;
    explicit RawImage(void* h) : Graphic(h) {}
    static constexpr TypeRef unity_type(){ return RawImageType; }
    Texture texture() const { return GetProperty<Texture>("texture"); }
    void set_texture(Texture value) const { SetProperty("texture", value); }
    Rect uvRect() const { return GetProperty<Rect>("uvRect"); }
    void set_uvRect(Rect value) const { SetProperty("uvRect", value); }
};
struct TextMeshProUGUI : Behaviour {
    TextMeshProUGUI() = default;
    explicit TextMeshProUGUI(void* h) : Behaviour(h) {}
    static constexpr TypeRef unity_type(){ return TextMeshProUGUIType; }
    std::string text() const { return GetProperty<std::string>("text"); }
    void set_text(std::string_view value) const { SetProperty("text", value); }
    Color color() const { return GetProperty<Color>("color"); }
    void set_color(Color value) const { SetProperty("color", value); }
    float fontSize() const { return GetProperty<float>("fontSize"); }
    void set_fontSize(float value) const { SetProperty("fontSize", value); }
    TmpFontStyles fontStyle() const { return GetProperty<TmpFontStyles>("fontStyle"); }
    void set_fontStyle(TmpFontStyles value) const { SetProperty("fontStyle", value); }
    int alignment() const { return GetProperty<int>("alignment"); }
    void set_alignment(int value) const { SetProperty("alignment", value); }
    bool enableWordWrapping() const { return GetProperty<bool>("enableWordWrapping"); }
    void set_enableWordWrapping(bool value) const { SetProperty("enableWordWrapping", value); }
    bool richText() const { return GetProperty<bool>("richText"); }
    void set_richText(bool value) const { SetProperty("richText", value); }
};
struct Toggle : Selectable {
    Toggle() = default;
    explicit Toggle(void* h) : Selectable(h) {}
    static constexpr TypeRef unity_type(){ return ToggleType; }
    bool isOn() const { return GetProperty<bool>("isOn"); }
    void set_isOn(bool value) const { SetProperty("isOn", value); }
    Graphic graphic() const { return GetProperty<Graphic>("graphic"); }
    void set_graphic(Graphic value) const { SetProperty("graphic", value); }
    Object onValueChanged() const { return GetProperty<Object>("onValueChanged"); }
    void SetIsOnWithoutNotify(bool value) const { CallExact<void>("SetIsOnWithoutNotify", {"System.Boolean"}, value); }
};
struct Slider : Selectable {
    Slider() = default;
    explicit Slider(void* h) : Selectable(h) {}
    static constexpr TypeRef unity_type(){ return SliderType; }
    float value() const { return GetProperty<float>("value"); }
    void set_value(float value) const { SetProperty("value", value); }
    float minValue() const { return GetProperty<float>("minValue"); }
    void set_minValue(float value) const { SetProperty("minValue", value); }
    float maxValue() const { return GetProperty<float>("maxValue"); }
    void set_maxValue(float value) const { SetProperty("maxValue", value); }
    bool wholeNumbers() const { return GetProperty<bool>("wholeNumbers"); }
    void set_wholeNumbers(bool value) const { SetProperty("wholeNumbers", value); }
    SliderDirection direction() const { return GetProperty<SliderDirection>("direction"); }
    void set_direction(SliderDirection value) const { SetProperty("direction", value); }
    Object onValueChanged() const { return GetProperty<Object>("onValueChanged"); }
    void SetValueWithoutNotify(float value) const { CallExact<void>("SetValueWithoutNotify", {"System.Single"}, value); }
};
struct Scrollbar : Selectable {
    Scrollbar() = default;
    explicit Scrollbar(void* h) : Selectable(h) {}
    static constexpr TypeRef unity_type(){ return ScrollbarType; }
    float value() const { return GetProperty<float>("value"); }
    void set_value(float value) const { SetProperty("value", value); }
    float size() const { return GetProperty<float>("size"); }
    void set_size(float value) const { SetProperty("size", value); }
    int numberOfSteps() const { return GetProperty<int>("numberOfSteps"); }
    void set_numberOfSteps(int value) const { SetProperty("numberOfSteps", value); }
    ScrollbarDirection direction() const { return GetProperty<ScrollbarDirection>("direction"); }
    void set_direction(ScrollbarDirection value) const { SetProperty("direction", value); }
    Object onValueChanged() const { return GetProperty<Object>("onValueChanged"); }
    void SetValueWithoutNotify(float value) const { CallExact<void>("SetValueWithoutNotify", {"System.Single"}, value); }
};
struct Dropdown : Selectable {
    Dropdown() = default;
    explicit Dropdown(void* h) : Selectable(h) {}
    static constexpr TypeRef unity_type(){ return DropdownType; }
    int value() const { return GetProperty<int>("value"); }
    void set_value(int value) const { SetProperty("value", value); }
    Text captionText() const { return GetProperty<Text>("captionText"); }
    Image captionImage() const { return GetProperty<Image>("captionImage"); }
    Text itemText() const { return GetProperty<Text>("itemText"); }
    Image itemImage() const { return GetProperty<Image>("itemImage"); }
    RectTransform templateTransform() const { return GetProperty<RectTransform>("template"); }
    Object onValueChanged() const { return GetProperty<Object>("onValueChanged"); }
    void SetValueWithoutNotify(int value) const { CallExact<void>("SetValueWithoutNotify", {"System.Int32"}, value); }
    void ClearOptions() const { Call<void>("ClearOptions"); }
    void RefreshShownValue() const { Call<void>("RefreshShownValue"); }
    void Show() const { Call<void>("Show"); }
    void Hide() const { Call<void>("Hide"); }
};
struct InputField : Selectable {
    InputField() = default;
    explicit InputField(void* h) : Selectable(h) {}
    static constexpr TypeRef unity_type(){ return InputFieldType; }
    std::string text() const { return GetProperty<std::string>("text"); }
    void set_text(std::string_view value) const { SetProperty("text", value); }
    int characterLimit() const { return GetProperty<int>("characterLimit"); }
    void set_characterLimit(int value) const { SetProperty("characterLimit", value); }
    InputFieldContentType contentType() const { return GetProperty<InputFieldContentType>("contentType"); }
    void set_contentType(InputFieldContentType value) const { SetProperty("contentType", value); }
    InputFieldLineType lineType() const { return GetProperty<InputFieldLineType>("lineType"); }
    void set_lineType(InputFieldLineType value) const { SetProperty("lineType", value); }
    bool readOnly() const { return GetProperty<bool>("readOnly"); }
    void set_readOnly(bool value) const { SetProperty("readOnly", value); }
    Graphic placeholder() const { return GetProperty<Graphic>("placeholder"); }
    Text textComponent() const { return GetProperty<Text>("textComponent"); }
    Object onValueChanged() const { return GetProperty<Object>("onValueChanged"); }
    Object onEndEdit() const { return GetProperty<Object>("onEndEdit"); }
    void SetTextWithoutNotify(std::string_view value) const { CallExact<void>("SetTextWithoutNotify", {"System.String"}, value); }
    void ActivateInputField() const { Call<void>("ActivateInputField"); }
    void DeactivateInputField() const { Call<void>("DeactivateInputField"); }
    void SelectAll() const { Call<void>("SelectAll"); }
};
struct TmpInputField : Selectable {
    TmpInputField() = default;
    explicit TmpInputField(void* h) : Selectable(h) {}
    static constexpr TypeRef unity_type(){ return TmpInputFieldType; }
    std::string text() const { return GetProperty<std::string>("text"); }
    void set_text(std::string_view value) const { SetProperty("text", value); }
    int characterLimit() const { return GetProperty<int>("characterLimit"); }
    void set_characterLimit(int value) const { SetProperty("characterLimit", value); }
    TmpInputFieldContentType contentType() const { return GetProperty<TmpInputFieldContentType>("contentType"); }
    void set_contentType(TmpInputFieldContentType value) const { SetProperty("contentType", value); }
    TmpInputFieldLineType lineType() const { return GetProperty<TmpInputFieldLineType>("lineType"); }
    void set_lineType(TmpInputFieldLineType value) const { SetProperty("lineType", value); }
    bool readOnly() const { return GetProperty<bool>("readOnly"); }
    void set_readOnly(bool value) const { SetProperty("readOnly", value); }
    Object onValueChanged() const { return GetProperty<Object>("onValueChanged"); }
    Object onEndEdit() const { return GetProperty<Object>("onEndEdit"); }
    void SetTextWithoutNotify(std::string_view value) const { CallExact<void>("SetTextWithoutNotify", {"System.String"}, value); }
    void ActivateInputField() const { Call<void>("ActivateInputField"); }
    void DeactivateInputField() const { Call<void>("DeactivateInputField"); }
    void SelectAll() const { Call<void>("SelectAll"); }
};
struct TmpDropdown : Selectable {
    TmpDropdown() = default;
    explicit TmpDropdown(void* h) : Selectable(h) {}
    static constexpr TypeRef unity_type(){ return TmpDropdownType; }
    int value() const { return GetProperty<int>("value"); }
    void set_value(int value) const { SetProperty("value", value); }
    Object onValueChanged() const { return GetProperty<Object>("onValueChanged"); }
    void SetValueWithoutNotify(int value) const { CallExact<void>("SetValueWithoutNotify", {"System.Int32"}, value); }
    void Show() const { Call<void>("Show"); }
    void Hide() const { Call<void>("Hide"); }
};
struct CanvasGroup : Behaviour {
    CanvasGroup() = default;
    explicit CanvasGroup(void* h) : Behaviour(h) {}
    static constexpr TypeRef unity_type(){ return CanvasGroupType; }
    float alpha() const { return GetProperty<float>("alpha"); }
    void set_alpha(float value) const { SetProperty("alpha", value); }
    bool interactable() const { return GetProperty<bool>("interactable"); }
    void set_interactable(bool value) const { SetProperty("interactable", value); }
    bool blocksRaycasts() const { return GetProperty<bool>("blocksRaycasts"); }
    void set_blocksRaycasts(bool value) const { SetProperty("blocksRaycasts", value); }
    bool ignoreParentGroups() const { return GetProperty<bool>("ignoreParentGroups"); }
    void set_ignoreParentGroups(bool value) const { SetProperty("ignoreParentGroups", value); }
};
struct CanvasScaler : Behaviour {
    CanvasScaler() = default;
    explicit CanvasScaler(void* h) : Behaviour(h) {}
    static constexpr TypeRef unity_type(){ return CanvasScalerType; }
    CanvasScaleMode uiScaleMode() const { return GetProperty<CanvasScaleMode>("uiScaleMode"); }
    void set_uiScaleMode(CanvasScaleMode value) const { SetProperty("uiScaleMode", value); }
    Vector2 referenceResolution() const { return GetProperty<Vector2>("referenceResolution"); }
    void set_referenceResolution(Vector2 value) const { SetProperty("referenceResolution", value); }
    CanvasScreenMatchMode screenMatchMode() const { return GetProperty<CanvasScreenMatchMode>("screenMatchMode"); }
    void set_screenMatchMode(CanvasScreenMatchMode value) const { SetProperty("screenMatchMode", value); }
    float matchWidthOrHeight() const { return GetProperty<float>("matchWidthOrHeight"); }
    void set_matchWidthOrHeight(float value) const { SetProperty("matchWidthOrHeight", value); }
    float scaleFactor() const { return GetProperty<float>("scaleFactor"); }
    void set_scaleFactor(float value) const { SetProperty("scaleFactor", value); }
    float referencePixelsPerUnit() const { return GetProperty<float>("referencePixelsPerUnit"); }
    void set_referencePixelsPerUnit(float value) const { SetProperty("referencePixelsPerUnit", value); }
};
struct Mask : Behaviour {
    Mask() = default;
    explicit Mask(void* h) : Behaviour(h) {}
    static constexpr TypeRef unity_type(){ return MaskType; }
    bool showMaskGraphic() const { return GetProperty<bool>("showMaskGraphic"); }
    void set_showMaskGraphic(bool value) const { SetProperty("showMaskGraphic", value); }
};
struct RectMask2D : Behaviour {
    RectMask2D() = default;
    explicit RectMask2D(void* h) : Behaviour(h) {}
    static constexpr TypeRef unity_type(){ return RectMask2DType; }
    Vector4 padding() const { return GetProperty<Vector4>("padding"); }
    void set_padding(Vector4 value) const { SetProperty("padding", value); }
    Rect canvasRect() const { return GetProperty<Rect>("canvasRect"); }
    void PerformClipping() const { Call<void>("PerformClipping"); }
};
struct ScrollRect : Behaviour {
    ScrollRect() = default;
    explicit ScrollRect(void* h) : Behaviour(h) {}
    static constexpr TypeRef unity_type(){ return ScrollRectType; }
    RectTransform content() const { return GetProperty<RectTransform>("content"); }
    void set_content(RectTransform value) const { SetProperty("content", value); }
    RectTransform viewport() const { return GetProperty<RectTransform>("viewport"); }
    void set_viewport(RectTransform value) const { SetProperty("viewport", value); }
    bool horizontal() const { return GetProperty<bool>("horizontal"); }
    void set_horizontal(bool value) const { SetProperty("horizontal", value); }
    bool vertical() const { return GetProperty<bool>("vertical"); }
    void set_vertical(bool value) const { SetProperty("vertical", value); }
    ScrollRectMovementType movementType() const { return GetProperty<ScrollRectMovementType>("movementType"); }
    void set_movementType(ScrollRectMovementType value) const { SetProperty("movementType", value); }
    float elasticity() const { return GetProperty<float>("elasticity"); }
    void set_elasticity(float value) const { SetProperty("elasticity", value); }
    bool inertia() const { return GetProperty<bool>("inertia"); }
    void set_inertia(bool value) const { SetProperty("inertia", value); }
    float decelerationRate() const { return GetProperty<float>("decelerationRate"); }
    void set_decelerationRate(float value) const { SetProperty("decelerationRate", value); }
    float scrollSensitivity() const { return GetProperty<float>("scrollSensitivity"); }
    void set_scrollSensitivity(float value) const { SetProperty("scrollSensitivity", value); }
    float horizontalNormalizedPosition() const { return GetProperty<float>("horizontalNormalizedPosition"); }
    void set_horizontalNormalizedPosition(float value) const { SetProperty("horizontalNormalizedPosition", value); }
    float verticalNormalizedPosition() const { return GetProperty<float>("verticalNormalizedPosition"); }
    void set_verticalNormalizedPosition(float value) const { SetProperty("verticalNormalizedPosition", value); }
    Vector2 normalizedPosition() const { return GetProperty<Vector2>("normalizedPosition"); }
    void set_normalizedPosition(Vector2 value) const { SetProperty("normalizedPosition", value); }
    void StopMovement() const { Call<void>("StopMovement"); }
};
struct LayoutElement : Behaviour {
    LayoutElement() = default;
    explicit LayoutElement(void* h) : Behaviour(h) {}
    static constexpr TypeRef unity_type(){ return LayoutElementType; }
    bool ignoreLayout() const { return GetProperty<bool>("ignoreLayout"); }
    void set_ignoreLayout(bool value) const { SetProperty("ignoreLayout", value); }
    float minWidth() const { return GetProperty<float>("minWidth"); }
    void set_minWidth(float value) const { SetProperty("minWidth", value); }
    float minHeight() const { return GetProperty<float>("minHeight"); }
    void set_minHeight(float value) const { SetProperty("minHeight", value); }
    float preferredWidth() const { return GetProperty<float>("preferredWidth"); }
    void set_preferredWidth(float value) const { SetProperty("preferredWidth", value); }
    float preferredHeight() const { return GetProperty<float>("preferredHeight"); }
    void set_preferredHeight(float value) const { SetProperty("preferredHeight", value); }
    float flexibleWidth() const { return GetProperty<float>("flexibleWidth"); }
    void set_flexibleWidth(float value) const { SetProperty("flexibleWidth", value); }
    float flexibleHeight() const { return GetProperty<float>("flexibleHeight"); }
    void set_flexibleHeight(float value) const { SetProperty("flexibleHeight", value); }
    int layoutPriority() const { return GetProperty<int>("layoutPriority"); }
    void set_layoutPriority(int value) const { SetProperty("layoutPriority", value); }
};
struct HorizontalLayoutGroup : Behaviour {
    HorizontalLayoutGroup() = default;
    explicit HorizontalLayoutGroup(void* h) : Behaviour(h) {}
    static constexpr TypeRef unity_type(){ return HorizontalLayoutGroupType; }
    float spacing() const { return GetProperty<float>("spacing"); }
    void set_spacing(float value) const { SetProperty("spacing", value); }
    TextAnchor childAlignment() const { return GetProperty<TextAnchor>("childAlignment"); }
    void set_childAlignment(TextAnchor value) const { SetProperty("childAlignment", value); }
    bool childControlWidth() const { return GetProperty<bool>("childControlWidth"); }
    void set_childControlWidth(bool value) const { SetProperty("childControlWidth", value); }
    bool childControlHeight() const { return GetProperty<bool>("childControlHeight"); }
    void set_childControlHeight(bool value) const { SetProperty("childControlHeight", value); }
    bool childForceExpandWidth() const { return GetProperty<bool>("childForceExpandWidth"); }
    void set_childForceExpandWidth(bool value) const { SetProperty("childForceExpandWidth", value); }
    bool childForceExpandHeight() const { return GetProperty<bool>("childForceExpandHeight"); }
    void set_childForceExpandHeight(bool value) const { SetProperty("childForceExpandHeight", value); }
};
struct VerticalLayoutGroup : Behaviour {
    VerticalLayoutGroup() = default;
    explicit VerticalLayoutGroup(void* h) : Behaviour(h) {}
    static constexpr TypeRef unity_type(){ return VerticalLayoutGroupType; }
    float spacing() const { return GetProperty<float>("spacing"); }
    void set_spacing(float value) const { SetProperty("spacing", value); }
    TextAnchor childAlignment() const { return GetProperty<TextAnchor>("childAlignment"); }
    void set_childAlignment(TextAnchor value) const { SetProperty("childAlignment", value); }
    bool childControlWidth() const { return GetProperty<bool>("childControlWidth"); }
    void set_childControlWidth(bool value) const { SetProperty("childControlWidth", value); }
    bool childControlHeight() const { return GetProperty<bool>("childControlHeight"); }
    void set_childControlHeight(bool value) const { SetProperty("childControlHeight", value); }
    bool childForceExpandWidth() const { return GetProperty<bool>("childForceExpandWidth"); }
    void set_childForceExpandWidth(bool value) const { SetProperty("childForceExpandWidth", value); }
    bool childForceExpandHeight() const { return GetProperty<bool>("childForceExpandHeight"); }
    void set_childForceExpandHeight(bool value) const { SetProperty("childForceExpandHeight", value); }
};
struct GridLayoutGroup : Behaviour {
    GridLayoutGroup() = default;
    explicit GridLayoutGroup(void* h) : Behaviour(h) {}
    static constexpr TypeRef unity_type(){ return GridLayoutGroupType; }
    Vector2 cellSize() const { return GetProperty<Vector2>("cellSize"); }
    void set_cellSize(Vector2 value) const { SetProperty("cellSize", value); }
    Vector2 spacing() const { return GetProperty<Vector2>("spacing"); }
    void set_spacing(Vector2 value) const { SetProperty("spacing", value); }
    GridLayoutCorner startCorner() const { return GetProperty<GridLayoutCorner>("startCorner"); }
    void set_startCorner(GridLayoutCorner value) const { SetProperty("startCorner", value); }
    GridLayoutAxis startAxis() const { return GetProperty<GridLayoutAxis>("startAxis"); }
    void set_startAxis(GridLayoutAxis value) const { SetProperty("startAxis", value); }
    GridLayoutConstraint constraint() const { return GetProperty<GridLayoutConstraint>("constraint"); }
    void set_constraint(GridLayoutConstraint value) const { SetProperty("constraint", value); }
    int constraintCount() const { return GetProperty<int>("constraintCount"); }
    void set_constraintCount(int value) const { SetProperty("constraintCount", value); }
};
struct ContentSizeFitter : Behaviour {
    ContentSizeFitter() = default;
    explicit ContentSizeFitter(void* h) : Behaviour(h) {}
    static constexpr TypeRef unity_type(){ return ContentSizeFitterType; }
    ContentSizeFitterFitMode horizontalFit() const { return GetProperty<ContentSizeFitterFitMode>("horizontalFit"); }
    void set_horizontalFit(ContentSizeFitterFitMode value) const { SetProperty("horizontalFit", value); }
    ContentSizeFitterFitMode verticalFit() const { return GetProperty<ContentSizeFitterFitMode>("verticalFit"); }
    void set_verticalFit(ContentSizeFitterFitMode value) const { SetProperty("verticalFit", value); }
};
struct AspectRatioFitter : Behaviour {
    AspectRatioFitter() = default;
    explicit AspectRatioFitter(void* h) : Behaviour(h) {}
    static constexpr TypeRef unity_type(){ return AspectRatioFitterType; }
    AspectRatioFitterMode aspectMode() const { return GetProperty<AspectRatioFitterMode>("aspectMode"); }
    void set_aspectMode(AspectRatioFitterMode value) const { SetProperty("aspectMode", value); }
    float aspectRatio() const { return GetProperty<float>("aspectRatio"); }
    void set_aspectRatio(float value) const { SetProperty("aspectRatio", value); }
};
struct EventSystem : Behaviour {
    EventSystem() = default;
    explicit EventSystem(void* h) : Behaviour(h) {}
    static constexpr TypeRef unity_type(){ return EventSystemType; }
    static EventSystem EnsureStandalone();
    static EventSystem current() { return detail::InvokeStatic<EventSystem>(EventSystemType, "get_current"); }
    GameObject firstSelectedGameObject() const;
    void set_firstSelectedGameObject(GameObject value) const;
    GameObject currentSelectedGameObject() const;
    bool sendNavigationEvents() const { return GetProperty<bool>("sendNavigationEvents"); }
    void set_sendNavigationEvents(bool value) const { SetProperty("sendNavigationEvents", value); }
    int pixelDragThreshold() const { return GetProperty<int>("pixelDragThreshold"); }
    void set_pixelDragThreshold(int value) const { SetProperty("pixelDragThreshold", value); }
    void SetSelectedGameObject(GameObject value) const;
    bool IsPointerOverGameObject() const { return Call<bool>("IsPointerOverGameObject"); }
};
struct BaseInputModule : Behaviour {
    BaseInputModule() = default;
    explicit BaseInputModule(void* h) : Behaviour(h) {}
    static constexpr TypeRef unity_type(){ return BaseInputModuleType; }
    EventSystem eventSystem() const { return GetProperty<EventSystem>("eventSystem"); }
};
struct StandaloneInputModule : BaseInputModule {
    StandaloneInputModule() = default;
    explicit StandaloneInputModule(void* h) : BaseInputModule(h) {}
    static constexpr TypeRef unity_type(){ return StandaloneInputModuleType; }
    float inputActionsPerSecond() const { return GetProperty<float>("inputActionsPerSecond"); }
    void set_inputActionsPerSecond(float value) const { SetProperty("inputActionsPerSecond", value); }
    float repeatDelay() const { return GetProperty<float>("repeatDelay"); }
    void set_repeatDelay(float value) const { SetProperty("repeatDelay", value); }
};
struct InputSystemUIInputModule : BaseInputModule {
    InputSystemUIInputModule() = default;
    explicit InputSystemUIInputModule(void* h) : BaseInputModule(h) {}
    static constexpr TypeRef unity_type(){ return InputSystemUIInputModuleType; }
};
struct LayoutRebuilder {
    static void ForceRebuildLayoutImmediate(RectTransform value) { detail::InvokeStatic<void>(TypeRef{"", "UnityEngine.UI", "LayoutRebuilder"}, "ForceRebuildLayoutImmediate", value); }
    static void MarkLayoutForRebuild(RectTransform value) { detail::InvokeStatic<void>(TypeRef{"", "UnityEngine.UI", "LayoutRebuilder"}, "MarkLayoutForRebuild", value); }
};
struct AssetBundle : Object {
    AssetBundle() = default;
    explicit AssetBundle(void* h) : Object(h) {}
    static constexpr TypeRef unity_type(){ return AssetBundleType; }
    // This wrapper is a borrowed managed reference. Unity owns the bundle and
    // its assets; do not use this or assets returned from it after Unload().
    static AssetBundle LoadFromFile(std::string_view path) {
        AssetBundle bundle = detail::InvokeStatic<AssetBundle>(AssetBundleType, "LoadFromFile", path);
        if (!bundle && !detail::fallback_error())
            detail::set_error(std::string("Unity AssetBundle::LoadFromFile returned null for path: ") + std::string(path));
        return bundle;
    }
    bool Contains(std::string_view assetName) const { return CallExact<bool>("Contains", {"System.String"}, assetName); }
    Object LoadAsset(std::string_view assetName) const {
        Object asset = CallExact<Object>("LoadAsset", {"System.String"}, assetName);
        if (!asset && !detail::fallback_error())
            detail::set_error(std::string("Unity AssetBundle::LoadAsset returned null for asset: ") + std::string(assetName));
        return asset;
    }
    Object LoadAsset(std::string_view assetName, TypeRef type) const {
        detail::clear_error();
        void* typeObject = type.resolve_type_object();
        if (!typeObject) {
            detail::set_error(std::string("Unity AssetBundle::LoadAsset failed: requested type not found: ") +
                              std::string(type.image) + ":" + std::string(type.namespc) + "." + std::string(type.name));
            detail::append_backend_error();
            return {};
        }
        Object asset = CallExact<Object>("LoadAsset", {"System.String", "System.Type"}, assetName, TypeObject{typeObject});
        if (!asset && !detail::fallback_error())
            detail::set_error(std::string("Unity AssetBundle::LoadAsset returned null for asset/type: ") +
                              std::string(assetName) + " / " + std::string(type.namespc) + "." + std::string(type.name));
        return asset;
    }
    template<class T> T LoadAsset(std::string_view assetName) const {
        return T{LoadAsset(assetName, T::unity_type()).handle()};
    }
    std::vector<Object> LoadAllAssets() const { return CallArrayExact<Object>("LoadAllAssets", {}); }
    std::vector<Object> LoadAllAssets(TypeRef type) const {
        detail::clear_error();
        void* typeObject = type.resolve_type_object();
        if (!typeObject) {
            detail::set_error(std::string("Unity AssetBundle::LoadAllAssets failed: requested type not found: ") +
                              std::string(type.image) + ":" + std::string(type.namespc) + "." + std::string(type.name));
            detail::append_backend_error();
            return {};
        }
        return CallArrayExact<Object>("LoadAllAssets", {"System.Type"}, TypeObject{typeObject});
    }
    template<class T> std::vector<T> LoadAllAssets() const {
        const std::vector<Object> assets = LoadAllAssets(T::unity_type());
        std::vector<T> typed;
        typed.reserve(assets.size());
        for (const Object& asset : assets)
            typed.emplace_back(asset.handle());
        return typed;
    }
    std::vector<std::string> GetAllAssetNames() const { return CallStringArrayExact("GetAllAssetNames", {}); }
    std::vector<std::string> GetAllScenePaths() const { return CallStringArrayExact("GetAllScenePaths", {}); }
    void Unload(bool unloadAllLoadedObjects) const { CallExact<void>("Unload", {"System.Boolean"}, unloadAllLoadedObjects); }
};
struct Scene {
    void* boxed_ = nullptr;
    Scene() = default;
    explicit Scene(void* boxed) : boxed_(boxed) {}
    void* handle() const { return boxed_; }
    explicit operator bool() const { return boxed_ != nullptr; }
    bool IsValid() const { return boxed_ ? Object{boxed_}.Call<bool>("IsValid") : false; }
    bool isLoaded() const { return boxed_ ? Object{boxed_}.GetProperty<bool>("isLoaded") : false; }
    int buildIndex() const { return boxed_ ? Object{boxed_}.GetProperty<int>("buildIndex") : -1; }
    int handle_value() const { return boxed_ ? Object{boxed_}.GetProperty<int>("handle") : 0; }
    std::string name() const { return boxed_ ? Object{boxed_}.GetProperty<std::string>("name") : std::string{}; }
    std::string path() const { return boxed_ ? Object{boxed_}.GetProperty<std::string>("path") : std::string{}; }
    bool isDontDestroyOnLoad() const { return name() == "DontDestroyOnLoad"; }
    std::vector<GameObject> GetRootGameObjects() const;
    int rootCount() const;
};
struct GameObject : Object {
    GameObject() = default;
    explicit GameObject(void* h) : Object(h) {}
    static constexpr TypeRef unity_type(){ return GameObjectType; }
    static GameObject Find(std::string_view name) { return detail::InvokeStatic<GameObject>(GameObjectType, "Find", name); }
    static GameObject FindWithTag(std::string_view tag) { return detail::InvokeStatic<GameObject>(GameObjectType, "FindWithTag", tag); }
    static std::vector<GameObject> FindGameObjectsWithTag(std::string_view tag) { return detail::StaticArrayCall<GameObject>(GameObjectType, "FindGameObjectsWithTag", tag); }
    static GameObject Create() { detail::clear_error(); auto* k=GameObjectType.resolve_class(); if(!k) { detail::set_error("Unity GameObject::Create failed: GameObject class not found"); detail::append_backend_error(); return {}; } void* o=detail::Backend::object_new(k); if(!o) { detail::set_error("Unity GameObject::Create failed: object allocation failed"); detail::append_backend_error(); return {}; } const void* c=detail::Backend::find_method(k,".ctor",0); if(!c) { detail::set_error("Unity GameObject::Create failed: default constructor not found"); detail::append_backend_error(); return {}; } void* ex=nullptr; if(!detail::Backend::runtime_invoke(c,o,nullptr,nullptr,&ex)||ex) { detail::set_error("Unity GameObject::Create failed: constructor threw or could not be invoked"); detail::append_backend_error(); return {}; } return GameObject{o}; }
    static GameObject Create(std::string_view name) { detail::clear_error(); auto* k=GameObjectType.resolve_class(); if(!k) { detail::set_error("Unity GameObject::Create failed: GameObject class not found"); detail::append_backend_error(); return {}; } void* o=detail::Backend::object_new(k); if(!o) { detail::set_error("Unity GameObject::Create failed: object allocation failed"); detail::append_backend_error(); return {}; } const std::vector<const char*> sig{"System.String"}; const void* c=detail::Backend::find_method_exact(k,".ctor",sig); if(!c) { detail::set_error(std::string("Unity GameObject::Create failed: string constructor not found: ")+detail::signature_text(".ctor", sig)); detail::append_backend_error(); return {}; } void* s=detail::Backend::new_string(name); if(!s) { detail::set_error("Unity GameObject::Create failed: name string allocation failed"); detail::append_backend_error(); return {}; } void* args[]={s}; void* ex=nullptr; if(!detail::Backend::runtime_invoke(c,o,args,nullptr,&ex)||ex) { detail::set_error("Unity GameObject::Create failed: constructor threw or could not be invoked"); detail::append_backend_error(); return {}; } return GameObject{o}; }
    static GameObject CreateUi(std::string_view name);
    static GameObject New() { return Create(); } static GameObject New(std::string_view name) { return Create(name); }
    Transform transform() const { return Call<Transform>("get_transform"); }
    bool activeSelf() const { return GetProperty<bool>("activeSelf"); }
    bool activeInHierarchy() const { return GetProperty<bool>("activeInHierarchy"); }
    void SetActive(bool value) const { CallExact<void>("SetActive", {"System.Boolean"}, value); }
    Scene scene() const { return Scene{Call<void*>("get_scene")}; }
    std::string tag() const;
    template<class T> T GetComponent() const { return T{GetComponent(T::unity_type().image, T::unity_type().namespc, T::unity_type().name).handle()}; }
    template<class T>
    T GetComponent(const char* name) const {
        void* result = CallExact<void*>("GetComponent", {"System.String"}, name);
        return T{result};
    }
    template<class T> T GetComponentInChildren(bool includeInactive=false) const { return T{GetComponentInChildren(T::unity_type().image, T::unity_type().namespc, T::unity_type().name, includeInactive).handle()}; }
    template<class T> T GetComponentInParent(bool includeInactive=false) const { return T{GetComponentInParent(T::unity_type().image, T::unity_type().namespc, T::unity_type().name, includeInactive).handle()}; }
    Object GetComponent(std::string_view image, std::string_view namespc, std::string_view className) const { detail::clear_error(); void* type=TypeRef{image,namespc,className}.resolve_type_object(); if(!type){ detail::set_error(std::string("Unity GameObject::GetComponent failed: component class not found: ")+std::string(image)+":"+std::string(namespc)+"."+std::string(className)); detail::append_backend_error(); return {}; } return CallExact<Object>("GetComponent", {"System.Type"}, TypeObject{type}); }
    Object GetComponentInChildren(std::string_view image, std::string_view namespc, std::string_view className, bool includeInactive=false) const { detail::clear_error(); void* type=TypeRef{image,namespc,className}.resolve_type_object(); if(!type){ detail::set_error(std::string("Unity GameObject::GetComponentInChildren failed: component class not found: ")+std::string(image)+":"+std::string(namespc)+"."+std::string(className)); detail::append_backend_error(); return {}; } return includeInactive ? CallExact<Object>("GetComponentInChildren", {"System.Type","System.Boolean"}, TypeObject{type}, includeInactive) : CallExact<Object>("GetComponentInChildren", {"System.Type"}, TypeObject{type}); }
    Object GetComponentInParent(std::string_view image, std::string_view namespc, std::string_view className, bool includeInactive=false) const { detail::clear_error(); void* type=TypeRef{image,namespc,className}.resolve_type_object(); if(!type){ detail::set_error(std::string("Unity GameObject::GetComponentInParent failed: component class not found: ")+std::string(image)+":"+std::string(namespc)+"."+std::string(className)); detail::append_backend_error(); return {}; } return includeInactive ? CallExact<Object>("GetComponentInParent", {"System.Type","System.Boolean"}, TypeObject{type}, includeInactive) : CallExact<Object>("GetComponentInParent", {"System.Type"}, TypeObject{type}); }
    template<class T = Object> std::vector<T> GetComponents() const { const TypeRef type = T::unity_type(); return GetComponents<T>(type.image, type.namespc, type.name); }
    template<class T = Object> std::vector<T> GetComponents(std::string_view image, std::string_view namespc, std::string_view className) const { detail::clear_error(); void* type=TypeRef{image,namespc,className}.resolve_type_object(); if(!type){ detail::set_error(std::string("Unity GameObject::GetComponents failed: component class not found: ")+std::string(image)+":"+std::string(namespc)+"."+std::string(className)); detail::append_backend_error(); return {}; } return CallArrayExact<T>("GetComponents", {"System.Type"}, TypeObject{type}); }
    template<class T = Object> std::vector<T> GetComponentsInChildren(bool includeInactive=false) const { const TypeRef type = T::unity_type(); return GetComponentsInChildren<T>(type.image, type.namespc, type.name, includeInactive); }
    template<class T = Object> std::vector<T> GetComponentsInChildren(std::string_view image, std::string_view namespc, std::string_view className, bool includeInactive=false) const { detail::clear_error(); void* type=TypeRef{image,namespc,className}.resolve_type_object(); if(!type){ detail::set_error(std::string("Unity GameObject::GetComponentsInChildren failed: component class not found: ")+std::string(image)+":"+std::string(namespc)+"."+std::string(className)); detail::append_backend_error(); return {}; } return includeInactive ? CallArrayExact<T>("GetComponentsInChildren", {"System.Type","System.Boolean"}, TypeObject{type}, includeInactive) : CallArrayExact<T>("GetComponentsInChildren", {"System.Type"}, TypeObject{type}); }
    template<class T = Object> std::vector<T> GetComponentsInParent(bool includeInactive=false) const { const TypeRef type = T::unity_type(); return GetComponentsInParent<T>(type.image, type.namespc, type.name, includeInactive); }
    template<class T = Object> std::vector<T> GetComponentsInParent(std::string_view image, std::string_view namespc, std::string_view className, bool includeInactive=false) const { detail::clear_error(); void* type=TypeRef{image,namespc,className}.resolve_type_object(); if(!type){ detail::set_error(std::string("Unity GameObject::GetComponentsInParent failed: component class not found: ")+std::string(image)+":"+std::string(namespc)+"."+std::string(className)); detail::append_backend_error(); return {}; } return includeInactive ? CallArrayExact<T>("GetComponentsInParent", {"System.Type","System.Boolean"}, TypeObject{type}, includeInactive) : CallArrayExact<T>("GetComponentsInParent", {"System.Type"}, TypeObject{type}); }
    template<class T> T AddComponent() const { if constexpr (std::is_same_v<T, Transform>) return T{}; else return T{AddComponent(T::unity_type().image, T::unity_type().namespc, T::unity_type().name).handle()}; }
    Object AddComponent(std::string_view image, std::string_view namespc, std::string_view className) const { detail::clear_error(); if (className == "Transform" && (namespc.empty() || namespc == "UnityEngine")) { detail::set_error("Unity GameObject::AddComponent failed: Transform is owned by GameObject and cannot be added"); return {}; } void* type=TypeRef{image,namespc,className}.resolve_type_object(); if(!type){ detail::set_error(std::string("Unity GameObject::AddComponent failed: component class not found: ")+std::string(image)+":"+std::string(namespc)+"."+std::string(className)); detail::append_backend_error(); return {}; } return CallExact<Object>("AddComponent", {"System.Type"}, TypeObject{type}); }
    template<class T> bool HasComponent() const { return static_cast<bool>(GetComponent<T>()); }
    bool HasComponent(std::string_view image, std::string_view namespc, std::string_view className) const { return static_cast<bool>(GetComponent(image,namespc,className)); }
    template<class T> T GetOrAddComponent() const { T c=GetComponent<T>(); return c ? c : AddComponent<T>(); }
    Object GetOrAddComponent(std::string_view image, std::string_view namespc, std::string_view className) const { Object c=GetComponent(image,namespc,className); return c ? c : AddComponent(image,namespc,className); }
};

inline std::vector<GameObject> Scene::GetRootGameObjects() const {
    return boxed_ ? Object{boxed_}.CallArrayExact<GameObject>("GetRootGameObjects", std::vector<const char *>{})
                  : std::vector<GameObject>{};
}

inline int Scene::rootCount() const {
    return static_cast<int>(GetRootGameObjects().size());
}

struct CanvasRoot {
    GameObject gameObject;
    RectTransform rectTransform;
    Canvas canvas;
    CanvasScaler scaler;
    GraphicRaycaster raycaster;
    explicit operator bool() const { return static_cast<bool>(gameObject) && static_cast<bool>(canvas); }
};

inline CanvasRoot CreateOverlayCanvas(std::string_view name, bool addRaycaster = true) {
    CanvasRoot root{};
    root.gameObject = GameObject::CreateUi(name);
    if (!root.gameObject)
        return root;
    root.rectTransform = root.gameObject.transform().GetComponent<RectTransform>();
    root.canvas = root.gameObject.AddComponent<Canvas>();
    if (!root.canvas)
        return root;
    root.canvas.set_renderMode(CanvasRenderMode::ScreenSpaceOverlay);
    root.scaler = root.gameObject.AddComponent<CanvasScaler>();
    if (addRaycaster)
        root.raycaster = root.gameObject.AddComponent<GraphicRaycaster>();
    return root;
}

inline EventSystem EventSystem::EnsureStandalone() {
    EventSystem existing = current();
    if (existing)
        return existing;
    GameObject gameObject = GameObject::Create("EventSystem");
    if (!gameObject)
        return {};
    EventSystem eventSystem = gameObject.AddComponent<EventSystem>();
    if (!eventSystem)
        return {};
    if (!gameObject.AddComponent<StandaloneInputModule>())
        return {};
    return eventSystem;
}

inline GameObject EventSystem::firstSelectedGameObject() const {
    return GetProperty<GameObject>("firstSelectedGameObject");
}

inline void EventSystem::set_firstSelectedGameObject(GameObject value) const {
    SetProperty("firstSelectedGameObject", value);
}

inline GameObject EventSystem::currentSelectedGameObject() const {
    return GetProperty<GameObject>("currentSelectedGameObject");
}

inline void EventSystem::SetSelectedGameObject(GameObject value) const {
    CallExact<void>("SetSelectedGameObject", {"UnityEngine.GameObject"}, value);
}


}

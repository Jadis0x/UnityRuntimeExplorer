#pragma once
#include <cstddef>
#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

#define URK_SDK_VERSION 28
#define URK_MONO_API_VERSION 7
#define URK_RUNTIME_API_VERSION 6
#define URK_IL2CPP_API_VERSION 6
#define URK_NETWORK_API_VERSION 1

#define URK_SCENE_NAME_MAX 128
#define URK_OBJECT_NAME_MAX 128
#define URK_OBJECT_TYPE_NAME_MAX 128
#define URK_STEAM_ID64_MAX 18

typedef struct URK_ModInfo {
    const char *projectName;
    const char *displayName;
    const char *author;
    const char *version;
    const char *url;
    const char *description;
} URK_ModInfo;

typedef const URK_ModInfo *(*URK_GetModInfoFn)();

typedef enum URK_RuntimeBackend {
    URK_RUNTIME_BACKEND_UNKNOWN = 0,
    URK_RUNTIME_BACKEND_MONO = 1,
    URK_RUNTIME_BACKEND_IL2CPP = 2
} URK_RuntimeBackend;

typedef enum URK_RuntimeCapabilityFlags {
    URK_RUNTIME_CAP_NONE = 0,
    URK_RUNTIME_CAP_MONO_API = 1ull << 0,
    URK_RUNTIME_CAP_IL2CPP_API = 1ull << 1,
    URK_RUNTIME_CAP_HOOKS = 1ull << 3,
    URK_RUNTIME_CAP_MAIN_THREAD = 1ull << 4,
    URK_RUNTIME_CAP_SCENE_EVENTS = 1ull << 5,
    URK_RUNTIME_CAP_CURSOR_CONTROL = 1ull << 6,
    URK_RUNTIME_CAP_NETWORK = 1ull << 7,
    URK_RUNTIME_CAP_INPUT = 1ull << 9,
    URK_RUNTIME_CAP_GRAPHICS_DEVICE_TYPE = 1ull << 10,
    URK_RUNTIME_CAP_OBJECT_DESTROY_REQUEST_EVENTS = 1ull << 11,
    URK_RUNTIME_CAP_STEAM_IDENTITY = 1ull << 12
} URK_RuntimeCapabilityFlags;

typedef enum URK_RuntimeModuleKind {
    URK_RUNTIME_MODULE_BACKEND_PRIMARY = 0,
    URK_RUNTIME_MODULE_MONO = 1,
    URK_RUNTIME_MODULE_GAME_ASSEMBLY = 2,
    URK_RUNTIME_MODULE_UNITY_PLAYER = 3
} URK_RuntimeModuleKind;

typedef struct URK_HookOptions URK_HookOptions;

typedef enum URK_NetworkHttpMethod {
    URK_NETWORK_HTTP_GET = 0,
    URK_NETWORK_HTTP_POST = 1,
    URK_NETWORK_HTTP_PUT = 2,
    URK_NETWORK_HTTP_UPDATE = URK_NETWORK_HTTP_PUT,
    URK_NETWORK_HTTP_PATCH = 3,
    URK_NETWORK_HTTP_DELETE = 4
} URK_NetworkHttpMethod;

typedef enum URK_NetworkResultFlags {
    URK_NETWORK_RESULT_NONE = 0,
    URK_NETWORK_RESULT_BODY_TRUNCATED = 1u << 0,
    URK_NETWORK_RESULT_ERROR_TRUNCATED = 1u << 1
} URK_NetworkResultFlags;

typedef struct URK_NetworkHeader {
    const char *name;
    const char *value;
} URK_NetworkHeader;

typedef struct URK_NetworkRequest {
    uint32_t size;
    uint32_t method;
    const char *url;
    const char *jsonBody;
    const URK_NetworkHeader *headers;
    size_t headerCount;
    uint32_t timeoutMs;
    uint32_t flags;
    /*
     * Optional libcurl pinned public key, for example
     * "sha256//BASE64_SHA256_SPKI". Leave null to use platform CA validation.
     */
    const char *pinnedPublicKey;
} URK_NetworkRequest;

typedef struct URK_NetworkResponse {
    uint32_t size;
    int32_t statusCode;
    char *body;
    size_t bodyCapacity;
    size_t bodyLength;
    char *error;
    size_t errorCapacity;
    uint32_t flags;
} URK_NetworkResponse;

typedef struct URK_NetworkApi {
    int version;
    uint32_t size;
    /*
     * Performs a synchronous JSON HTTPS request through the loader-owned
     * transport with TLS verification, HTTPS-only URLs, no redirects, bounded
     * response buffers, and caller-owned storage. Returns non-zero for transport
     * success; callers must still validate statusCode and the response JSON.
     */
    int (*json_request)(const URK_NetworkRequest *request, URK_NetworkResponse *response);
} URK_NetworkApi;

typedef struct URK_SceneInfo {
    uint32_t size;
    int32_t buildIndex;
    int32_t handle;
    char name[URK_SCENE_NAME_MAX];
} URK_SceneInfo;

typedef enum URK_CursorLockState {
    URK_CURSOR_LOCK_NONE = 0,
    URK_CURSOR_LOCK_LOCKED = 1,
    URK_CURSOR_LOCK_CONFINED = 2
} URK_CursorLockState;

typedef enum URK_GraphicsDeviceType {
    URK_GRAPHICS_DEVICE_UNKNOWN = -1,
    URK_GRAPHICS_DEVICE_OPENGL2 = 0,
    URK_GRAPHICS_DEVICE_D3D11 = 2,
    URK_GRAPHICS_DEVICE_OPENGL_CORE = 17,
    URK_GRAPHICS_DEVICE_D3D12 = 18,
    URK_GRAPHICS_DEVICE_VULKAN = 21
} URK_GraphicsDeviceType;

typedef struct URK_CursorState {
    uint32_t size;
    int visible;
    int32_t lockState;
} URK_CursorState;

typedef void (*URK_OnSceneLoadedFn)(const URK_SceneInfo *scene);
typedef void (*URK_OnSceneChangedFn)(const URK_SceneInfo *previousScene, const URK_SceneInfo *currentScene);

typedef enum URK_ObjectDestroyRequestFlags {
    URK_OBJECT_DESTROY_REQUEST_NONE = 0,
    URK_OBJECT_DESTROY_REQUEST_IMMEDIATE = 1u << 0,
    URK_OBJECT_DESTROY_REQUEST_ALLOW_DESTROYING_ASSETS = 1u << 1
} URK_ObjectDestroyRequestFlags;

/*
 * Describes a call to UnityEngine.Object.Destroy or DestroyImmediate. This is
 * a destroy request notification; Unity may defer or reject the actual object
 * destruction. objectAddress is diagnostic identity only and must never be
 * dereferenced or retained as a live managed object reference.
 */
typedef struct URK_ObjectDestroyRequest {
    uint32_t size;
    uint32_t flags;
    uintptr_t objectAddress;
    int32_t instanceId;
    float delaySeconds;
    char name[URK_OBJECT_NAME_MAX];
    char typeName[URK_OBJECT_TYPE_NAME_MAX];
} URK_ObjectDestroyRequest;

typedef void (*URK_OnObjectDestroyRequestedFn)(const URK_ObjectDestroyRequest *request);

typedef struct URK_RuntimeApi {
    int version;
    uint32_t size;
    uint32_t (*backend)();
    uint64_t (*capabilities)();
    uintptr_t (*module_base)(uint32_t kind);
    /*
     * Copies the last scene observed by the runtime event pump. Returns zero
     * until a scene is observed or when the backend cannot provide scene state.
     */
    int (*scene_current)(URK_SceneInfo *scene);
    /*
     * Acquires or releases one cursor-ownership reference for a native menu.
     * While any reference is active, the loader exposes and unlocks the cursor
     * and suppresses Unity mouse-button polling. The last release restores the
     * previous state. Returns zero without changing state when unavailable.
     */
    int (*menu_cursor_set_open)(int open);
    /*
     * Explicit Cursor.visible and Cursor.lockState access for game-specific
     * menu-close policies.
     */
    int (*cursor_state_get)(URK_CursorState *state);
    int (*cursor_state_set)(const URK_CursorState *state);
    int (*input_get_key)(int32_t keyCode);
    int (*input_get_key_down)(int32_t keyCode);
    int (*input_get_key_up)(int32_t keyCode);
    int (*input_get_mouse_button)(int32_t button);
    int (*input_get_mouse_button_down)(int32_t button);
    int (*input_get_mouse_button_up)(int32_t button);
    /*
     * Returns UnityEngine.SystemInfo.graphicsDeviceType on the captured main
     * thread, or URK_GRAPHICS_DEVICE_UNKNOWN when unavailable. Callers must
     * size-check this append-only entry before use.
     */
    int32_t (*graphics_device_type)();
    /*
     * Copies the current public individual SteamID64 reported by the Steam API
     * module loaded by the game. Returns non-zero on success. The caller must
     * provide at least URK_STEAM_ID64_MAX bytes. Returns zero and writes an
     * empty string when Steam is unavailable or not initialized yet.
     */
    int (*steam_id64)(char *output, size_t output_size);
} URK_RuntimeApi;

typedef struct URK_Il2CppManagedMethodDesc {
    uint32_t size;
    const char *image_name;
    const char *namespc;
    const char *class_name;
    const char *method_name;
    const char *const *parameter_type_names;
    int parameter_count;
} URK_Il2CppManagedMethodDesc;

typedef struct URK_Il2CppManagedHookResult {
    uint32_t size;
    const void *method;
    void *native_target;
    const char *diagnostic;
} URK_Il2CppManagedHookResult;

typedef struct URK_Il2CppApi {
    int version;
    uint32_t size;
    /*
     * Returns non-zero when the IL2CPP backend is available and initialized
     * enough for v1 metadata lookups. Returns 0 when required runtime pieces are
     * missing, initialization is incomplete, or IL2CPP support is unavailable.
     * Mods must verify ctx->il2cpp is non-null and
     * ctx->il2cpp->size >= offsetof(URK_Il2CppApi, is_available) +
     * sizeof(ctx->il2cpp->is_available) before calling this function.
     */
    int (*is_available)();
    /*
     * Returns an opaque IL2CPP domain handle for diagnostics/readiness checks, or
     * nullptr when the domain is unavailable or not initialized. The returned
     * pointer is not a public layout contract. Mods must table-size check through
     * this field before calling it.
     */
    const void *(*domain_get)();
    /*
     * Looks up an IL2CPP image/assembly by runtime metadata name. image_name
     * must be a non-null, non-empty UTF-8 string. Returns an opaque image handle,
     * or nullptr for bad input, missing metadata, unavailable runtime state, or a
     * failed lookup. Mods must table-size check through this field before use.
     */
    const void *(*find_image)(const char *image_name);
    /*
     * Looks up a class by image name, namespace, and type name. image_name and
     * name must be non-null and non-empty; namespc may be null or empty for the
     * global namespace. Returns an opaque class handle, or nullptr for bad input,
     * missing metadata, unavailable runtime state, or a failed lookup. Mods must
     * table-size check through this field before use.
     */
    const void *(*find_class)(const char *image_name, const char *namespc, const char *name);
    /*
     * Looks up a method by class handle, method name, and parameter count. klass
     * must be a class handle returned by this API, name must be non-null and
     * non-empty, and argc must be >= 0. Returns an opaque method handle, or
     * nullptr for bad input, unavailable runtime state, absent metadata,
     * ambiguity, or failed lookup. Mods must table-size check through this field
     * before use.
     */
    const void *(*find_method)(const void *klass, const char *name, int argc);
    /*
     * Looks up an overload by class handle, method name, and exact parameter type
     * names. klass must be a class handle returned by this API, name must be
     * non-null and non-empty, parameter_type_names must contain parameter_count
     * non-null entries when parameter_count > 0, and parameter_count must be >=
     * 0. Returns an opaque method handle, or nullptr for bad input, unavailable
     * runtime state, absent metadata, ambiguity, unsupported signatures, or a
     * failed lookup. Mods must table-size check through this field before use.
     */
    const void *(*find_method_exact)(const void *klass, const char *name, const char *const *parameter_type_names,
                                     int parameter_count);
    /*
     * Returns the current native entry point for a method handle returned by this
     * API. Returns nullptr for bad input, unavailable runtime state, stripped
     * methods, unhookable/internal-call/runtime-generated bodies, unsupported
     * generic states, or methods with no safely exposable native body. Mods must
     * table-size check through this field before use.
     */
    void *(*method_pointer)(const void *method);
    /*
     * Looks up a field by class handle and field name. klass must be a class
     * handle returned by this API and name must be non-null and non-empty.
     * Returns an opaque field handle, or nullptr for bad input, unavailable
     * runtime state, absent metadata, or failed lookup. Mods must table-size
     * check through this field before use.
     */
    const void *(*find_field)(const void *klass, const char *name);
    /*
     * Returns the runtime field offset reported by IL2CPP metadata for a field
     * handle returned by this API. Returns a negative value for bad input,
     * unavailable runtime state, unsupported field kinds, absent metadata, or
     * failed lookup; implementations must not invent fallback offsets. Mods must
     * table-size check through this field before use.
     */
    int32_t (*field_offset)(const void *field);
    /*
     * Returns a short backend-owned diagnostic string for the last failed IL2CPP
     * API operation on the calling thread when practical, or a process-global
     * static diagnostic otherwise. Returns nullptr or an empty string when no
     * diagnostic is available. The returned pointer is backend-owned and must not
     * be freed or stored indefinitely. Mods must table-size check through this
     * field before use.
     */
    const char *(*last_error)();
    size_t (*domain_get_assembly_count)();
    const void *(*domain_get_assembly)(size_t index);
    const void *(*assembly_get_image)(const void *assembly);
    const char *(*image_get_name)(const void *image);
    size_t (*image_get_class_count)(const void *image);
    const void *(*image_get_class)(const void *image, size_t index);
    const char *(*class_get_name)(const void *klass);
    const char *(*class_get_namespace)(const void *klass);
    const void *(*class_get_parent)(const void *klass);
    uint32_t (*class_get_flags)(const void *klass);
    int (*class_is_valuetype)(const void *klass);
    int (*class_is_enum)(const void *klass);
    const void *(*class_get_fields)(const void *klass, void **iterator);
    const void *(*class_get_methods)(const void *klass, void **iterator);
    const void *(*class_get_properties)(const void *klass, void **iterator);
    const void *(*class_get_nested_types)(const void *klass, void **iterator);
    const void *(*class_get_interfaces)(const void *klass, void **iterator);
    const char *(*method_get_name)(const void *method);
    const void *(*method_get_declaring_class)(const void *method);
    uint32_t (*method_get_param_count)(const void *method);
    const void *(*method_get_param)(const void *method, uint32_t index);
    const void *(*method_get_return_type)(const void *method);
    uint32_t (*method_get_flags)(const void *method, uint32_t *iflags);
    uint32_t (*method_get_token)(const void *method);
    const char *(*field_get_name)(const void *field);
    const void *(*field_get_type)(const void *field);
    uint32_t (*field_get_flags)(const void *field);
    int (*field_static_get_value)(const void *field, void *output);
    /* IL2CPP setter semantics: pass raw storage for value types, but pass the
     * managed object directly (including NULL) for reference types. */
    int (*field_static_set_value)(const void *field, void *value);
    const char *(*property_get_name)(const void *property);
    const void *(*property_get_get_method)(const void *property);
    const void *(*property_get_set_method)(const void *property);
    uint32_t (*property_get_flags)(const void *property);
    int (*type_get_name)(const void *type, char *output, size_t output_size);
    int32_t (*type_get_type)(const void *type);
    uint32_t (*type_get_attrs)(const void *type);
    const void *(*type_get_class_or_element_class)(const void *type);
    const void *(*object_get_class)(void *object);
    void *(*object_unbox)(void *object);
    void *(*string_new)(const char *utf8);
    int (*string_to_utf8)(void *string, char *output, size_t output_size);
    size_t (*array_length)(void *array);
    void *(*array_addr_with_size)(void *array, int element_size, size_t index);
    /* Append-only safe object/reference array element helper. Implementations
     * must not expose private array layout assumptions. */
    void *(*array_ref_at)(void *array, size_t index);
    int (*field_get_value)(void *object, const void *field, void *output);
    /* Value types use a storage address; reference types use the managed
     * object pointer directly and may pass NULL. */
    int (*field_set_value)(void *object, const void *field, void *value);
    int (*runtime_invoke)(const void *method, void *object, void **params, void **result, void **exception);
    const void *(*thread_current)();
    const void *(*thread_attach)(const void *domain);
    void (*thread_detach)(const void *thread);
    void *(*alloc)(size_t size);
    void (*free)(void *ptr);
    /* Extended IL2CPP public surface for Unity explorer/runtime inspection. */
    void (*init)(const char *domain_name);
    void (*shutdown)();
    void (*set_config_dir)(const char *config_path);
    void (*set_data_dir)(const char *data_path);
    void (*set_commandline_arguments)(int argc, const char *argv[], const char *basedir);
    void (*set_memory_callbacks)(void *callbacks);
    void (*set_find_plugin_callback)(void *method);
    void (*add_internal_call)(const char *name, void *method);
    void *(*resolve_icall)(const char *name);
    const void *(*domain_assembly_open)(const void *domain, const char *name);
    const void *(*get_corlib)();
    const void *(*image_get_assembly)(const void *image);
    const char *(*image_get_filename)(const void *image);
    const void *(*image_get_entry_point)(const void *image);
    const void *(*class_from_type)(const void *type);
    const void *(*class_from_il2cpp_type)(const void *type);
    const void *(*class_from_system_type)(void *reflection_type);
    const char *(*class_get_assemblyname)(const void *klass);
    const void *(*class_get_image)(const void *klass);
    const void *(*class_get_declaring_type)(const void *klass);
    const void *(*class_get_element_class)(const void *klass);
    const void *(*class_get_type)(const void *klass);
    uint32_t (*class_get_type_token)(const void *klass);
    int (*class_get_rank)(const void *klass);
    int32_t (*class_instance_size)(const void *klass);
    int32_t (*class_value_size)(const void *klass, uint32_t *align);
    size_t (*class_num_fields)(const void *klass);
    int (*class_array_element_size)(const void *klass);
    int (*class_is_generic)(const void *klass);
    int (*class_is_inflated)(const void *klass);
    int (*class_is_abstract)(const void *klass);
    int (*class_is_interface)(const void *klass);
    int (*class_is_subclass_of)(const void *klass, const void *klassc, int check_interfaces);
    int (*class_is_assignable_from)(const void *klass, const void *oklass);
    int (*class_has_parent)(const void *klass, const void *klassc);
    int (*class_has_attribute)(const void *klass, const void *attr_class);
    int (*class_has_references)(const void *klass);
    const void *(*class_enum_basetype)(const void *klass);
    const void *(*class_get_property_from_name)(const void *klass, const char *name);
    const void *(*class_get_events)(const void *klass, void **iterator);
    size_t (*class_get_bitmap_size)(const void *klass);
    void (*class_get_bitmap)(const void *klass, size_t *bitmap);
    const void *(*method_get_declaring_type)(const void *method);
    void *(*method_get_object)(const void *method, const void *refclass);
    int (*method_is_generic)(const void *method);
    int (*method_is_inflated)(const void *method);
    int (*method_is_instance)(const void *method);
    int (*method_has_attribute)(const void *method, const void *attr_class);
    const char *(*method_get_param_name)(const void *method, uint32_t index);
    const void *(*field_get_parent)(const void *field);
    void *(*field_get_value_object)(const void *field, void *object);
    int (*field_has_attribute)(const void *field, const void *attr_class);
    const void *(*property_get_parent)(const void *property);
    void *(*type_get_object)(const void *type);
    uint32_t (*object_get_size)(void *object);
    const void *(*object_get_virtual_method)(void *object, const void *method);
    void *(*object_new)(const void *klass);
    void *(*object_is_inst)(void *object, const void *klass);
    void *(*value_box)(const void *klass, void *data);
    void *(*string_new_len)(const char *str, uint32_t length);
    void *(*string_new_utf16)(const uint16_t *text, int32_t length);
    void *(*string_new_wrapper)(const char *str);
    int32_t (*string_length)(void *string);
    const uint16_t *(*string_chars)(void *string);
    void *(*string_intern)(void *string);
    void *(*string_is_interned)(void *string);
    const void *(*array_class_get)(const void *element_class, uint32_t rank);
    const void *(*bounded_array_class_get)(const void *element_class, uint32_t rank, int bounded);
    size_t (*array_get_byte_length)(void *array);
    int (*array_element_size)(const void *array_class);
    void *(*array_new)(const void *element_class, size_t length);
    void *(*array_new_specific)(const void *array_class, size_t length);
    void *(*array_new_full)(const void *array_class, size_t *lengths, size_t *lower_bounds);
    void *(*runtime_invoke_convert_args)(const void *method, void *object, void **params, int param_count,
                                         void **exception);
    void (*runtime_class_init)(const void *klass);
    void (*runtime_object_init)(void *object);
    void (*runtime_object_init_exception)(void *object, void **exception);
    void (*runtime_unhandled_exception_policy_set)(int value);
    void (*raise_exception)(void *exception);
    void *(*exception_from_name_msg)(const void *image, const char *name_space, const char *name, const char *msg);
    void *(*get_exception_argument_null)(const char *arg);
    void (*format_exception)(const void *exception, char *message, int message_size);
    void (*format_stack_trace)(const void *exception, char *output, int output_size);
    void (*unhandled_exception)(void *exception);
    void (*gc_collect)(int max_generations);
    int64_t (*gc_get_used_size)();
    int64_t (*gc_get_heap_size)();
    uint32_t (*gchandle_new)(void *object, int pinned);
    uint32_t (*gchandle_new_weakref)(void *object, int track_resurrection);
    void *(*gchandle_get_target)(uint32_t gchandle);
    void (*gchandle_free)(uint32_t gchandle);
    /*
     * Returned buffers are IL2CPP-allocated; release them with this table's
     * free() entry. Returns null when free() is unavailable.
     */
    char *(*thread_get_name)(const void *thread, uint32_t *length);
    const void **(*thread_get_all_attached_threads)(size_t *size);
    int (*is_vm_thread)(const void *thread);
    void (*current_thread_walk_frame_stack)(void *callback, void *user_data);
    void (*thread_walk_frame_stack)(const void *thread, void *callback, void *user_data);
    int (*current_thread_get_top_frame)(void *frame);
    int (*thread_get_top_frame)(const void *thread, void *frame);
    int (*current_thread_get_frame_at)(int32_t offset, void *frame);
    int (*thread_get_frame_at)(const void *thread, int32_t offset, void *frame);
    int32_t (*current_thread_get_stack_depth)();
    int32_t (*thread_get_stack_depth)(const void *thread);
    void (*monitor_enter)(void *object);
    int (*monitor_try_enter)(void *object, uint32_t timeout);
    void (*monitor_exit)(void *object);
    void (*monitor_pulse)(void *object);
    void (*monitor_pulse_all)(void *object);
    void (*monitor_wait)(void *object);
    int (*monitor_try_wait)(void *object, uint32_t timeout);
    void *(*delegate_begin_invoke)(void *delegate_obj, void **params, void *async_callback, void *state);
    void *(*delegate_end_invoke)(void *async_result, void **out_args);
    void (*profiler_install)(void *profiler, void *shutdown_callback);
    void (*profiler_set_events)(int events);
    void (*profiler_install_enter_leave)(void *enter, void *leave);
    void (*profiler_install_allocation)(void *callback);
    void (*profiler_install_gc)(void *callback, void *heap_resize_callback);
    void *(*unity_liveness_calculation_begin)(const void *filter, int max_object_count, void *callback, void *userdata,
                                              void *on_world_started, void *on_world_stopped);
    void (*unity_liveness_calculation_end)(void *state);
    void (*unity_liveness_calculation_from_root)(void *root, void *state);
    void (*unity_liveness_calculation_from_statics)(void *state);
    int (*stats_dump_to_file)(const char *path);
    uint64_t (*stats_get_value)(int stat);
    void *(*capture_memory_snapshot)();
    void (*free_captured_memory_snapshot)(void *snapshot);
    const void *(*debug_get_class_info)(const void *klass);
    const void *(*debug_class_get_document)(const void *info);
    const char *(*debug_document_get_filename)(const void *document);
    const char *(*debug_document_get_directory)(const void *document);
    const void *(*debug_get_method_info)(const void *method);
    const void *(*debug_method_get_document)(const void *info);
    const int32_t *(*debug_method_get_offset_table)(const void *info);
    size_t (*debug_method_get_code_size)(const void *info);
    void (*debug_update_frame_il_offset)(int32_t il_offset);
    const void **(*debug_method_get_locals_info)(const void *info);
    const void *(*debug_local_get_type)(const void *info);
    const char *(*debug_local_get_name)(const void *info);
    uint32_t (*debug_local_get_start_offset)(const void *info);
    uint32_t (*debug_local_get_end_offset)(const void *info);
    void *(*debug_method_get_param_value)(const void *info, uint32_t position);
    void *(*debug_frame_get_local_value)(const void *info, uint32_t position);
    void *(*debug_method_get_breakpoint_data_at)(const void *info, int64_t uid, int32_t offset);
    void (*debug_method_set_breakpoint_data_at)(const void *info, uint64_t location, void *data);
    void (*debug_method_clear_breakpoint_data)(const void *info);
    void (*debug_method_clear_breakpoint_data_at)(const void *info, uint64_t location);
    /*
     * Resolves and hooks MethodInfo::methodPointer after validating the target.
     * Returns a diagnostic when the runtime uses an unsupported private layout.
     */
    int (*attach_managed_method_hook)(const URK_Il2CppManagedMethodDesc *method, void **original, void *detour,
                                       const URK_HookOptions *options, URK_Il2CppManagedHookResult *result);
    /*
     * Official IL2CPP object and array layout queries. Size-check these
     * append-only entries before use.
     */
    uint32_t (*object_header_size)();
    uint32_t (*array_object_header_size)();
    uint32_t (*offset_of_array_length_in_array_object_header)();
    uint32_t (*offset_of_array_bounds_in_array_object_header)();
    uint32_t (*allocation_granularity)();
    int (*array_set_ref)(void *array, size_t index, void *value);
} URK_Il2CppApi;

#ifdef __cplusplus
static_assert(offsetof(URK_RuntimeApi, size) > offsetof(URK_RuntimeApi, version),
              "URK_RuntimeApi must preserve version followed by size.");
static_assert(offsetof(URK_RuntimeApi, menu_cursor_set_open) > offsetof(URK_RuntimeApi, module_base),
              "URK_RuntimeApi new fields must be appended.");
static_assert(offsetof(URK_RuntimeApi, cursor_state_set) > offsetof(URK_RuntimeApi, menu_cursor_set_open),
              "URK_RuntimeApi cursor state helpers must stay appended.");
static_assert(offsetof(URK_RuntimeApi, input_get_key) > offsetof(URK_RuntimeApi, cursor_state_set),
              "URK_RuntimeApi input helpers must stay appended.");
static_assert(offsetof(URK_RuntimeApi, graphics_device_type) > offsetof(URK_RuntimeApi, input_get_mouse_button_up),
              "URK_RuntimeApi graphics device helper must stay appended.");
static_assert(offsetof(URK_RuntimeApi, steam_id64) > offsetof(URK_RuntimeApi, graphics_device_type),
              "URK_RuntimeApi Steam identity helper must stay appended.");
static_assert(offsetof(URK_ObjectDestroyRequest, typeName) > offsetof(URK_ObjectDestroyRequest, name),
              "URK_ObjectDestroyRequest fields must remain append-only.");
static_assert(offsetof(URK_Il2CppApi, size) > offsetof(URK_Il2CppApi, version),
              "URK_Il2CppApi must preserve version followed by size.");
static_assert(offsetof(URK_Il2CppApi, is_available) > offsetof(URK_Il2CppApi, size),
              "URK_Il2CppApi keeps version and size before callable entries.");
static_assert(offsetof(URK_Il2CppApi, last_error) > offsetof(URK_Il2CppApi, field_offset),
              "URK_Il2CppApi field order changed unexpectedly.");
static_assert(offsetof(URK_Il2CppApi, array_set_ref) > offsetof(URK_Il2CppApi, allocation_granularity),
              "URK_Il2CppApi new fields must be appended.");
static_assert(offsetof(URK_NetworkApi, json_request) > offsetof(URK_NetworkApi, size),
              "URK_NetworkApi keeps version and size before callable entries.");
#endif

typedef struct URK_MonoApi {
    int version;
    int (*attach_current_thread)();
    const void *(*find_class)(const char *image, const char *namespc, const char *name);
    const void *(*find_method)(const char *image, const char *namespc, const char *klass, const char *name, int argc);
    const void *(*find_field)(const char *image, const char *namespc, const char *klass, const char *field);
    int (*runtime_invoke)(const void *method, void *object, void **params, void **result, void **exception,
                          uint32_t *native_exception);
    void *(*new_string)(const char *utf8);
    size_t (*array_length)(void *array);
    void *(*array_address)(void *array, int element_size, size_t index);
    int (*object_class_name)(void *object, char *output, size_t output_size);
    const char *(*class_get_name)(const void *klass);
    const char *(*class_get_namespace)(const void *klass);
    const void *(*class_get_parent)(const void *klass);
    uint32_t (*class_get_flags)(const void *klass);
    const void *(*class_get_fields)(const void *klass, void **iterator);
    const void *(*class_get_methods)(const void *klass, void **iterator);
    const void *(*class_get_properties)(const void *klass, void **iterator);
    const char *(*field_get_name)(const void *field);
    const void *(*field_get_type)(const void *field);
    uint32_t (*field_get_offset)(const void *field);
    uint32_t (*field_get_flags)(const void *field);
    const char *(*method_get_name)(const void *method);
    uint32_t (*method_get_flags)(const void *method, uint32_t *iflags);
    const void *(*method_signature)(const void *method);
    uint32_t (*signature_get_param_count)(const void *signature);
    const void *(*signature_get_return_type)(const void *signature);
    const void *(*signature_get_param)(const void *signature, void **iterator);
    int (*type_get_name)(const void *type, char *output, size_t output_size);
    const char *(*property_get_name)(const void *property);
    const void *(*property_get_get_method)(const void *property);
    const void *(*property_get_set_method)(const void *property);
    void *(*compile_method)(const void *method);
    const void *(*find_image)(const char *image);
    const void *(*find_method_exact)(const char *image, const char *namespc, const char *klass, const char *name,
                                     const char *const *parameter_types, int parameter_count);
    const void *(*object_get_class)(void *object);
    void *(*object_unbox)(void *object);
    int (*string_to_utf8)(void *string, char *output, size_t output_size);
    int (*field_get_value)(void *object, const void *field, void *output);
    int (*field_set_value)(void *object, const void *field, void *value);
    uint32_t size;
    const char *(*last_error)();
    const void *(*domain_get)();
    const void *(*root_domain_get)();
    const void *(*assembly_get_image)(const void *assembly);
    const char *(*image_get_name)(const void *image);
    const char *(*image_get_filename)(const void *image);
    int (*image_get_table_rows)(const void *image, int table_id);
    const void *(*image_get_class)(const void *image, uint32_t token);
    const void *(*class_get_type)(const void *klass);
    int (*class_is_valuetype)(const void *klass);
    int (*class_is_enum)(const void *klass);
    const void *(*class_get_nested_types)(const void *klass, void **iterator);
    const void *(*class_get_interfaces)(const void *klass, void **iterator);
    uint32_t (*property_get_flags)(const void *property);
    const void *(*method_get_return_type)(const void *method);
    const void *(*method_get_param_type)(const void *method, uint32_t index);
    int32_t (*type_get_type)(const void *type);
    uint32_t (*type_get_attrs)(const void *type);
    const void *(*type_get_class)(const void *type);
    int (*field_static_get_value)(const void *klass, const void *field, void *output);
    int (*field_static_set_value)(const void *klass, const void *field, void *value);
    const void *(*thread_current)();
    void (*thread_detach)(const void *thread);
    size_t (*string_length)(void *string);
    void *(*object_new)(const void *klass);
    void *(*type_get_object)(const void *type);
    void (*runtime_object_init)(void *object);
    /* Bounds-checked object/reference array access without layout assumptions. */
    void *(*array_ref_at)(void *array, size_t index);
    int (*array_set_ref)(void *array, size_t index, void *value);
    uint32_t (*gchandle_new)(void *object, int pinned);
    uint32_t (*gchandle_new_weakref)(void *object, int track_resurrection);
    void *(*gchandle_get_target)(uint32_t gchandle);
    void (*gchandle_free)(uint32_t gchandle);
    /* Returns the managed System.Reflection.MethodInfo for a Mono method. */
    void *(*method_get_object)(const void *method);
    /* Boxes a value-type storage slot into a managed object. */
    void *(*value_box)(const void *klass, void *data);
} URK_MonoApi;

#ifdef __cplusplus
static_assert(offsetof(URK_MonoApi, method_get_object) > offsetof(URK_MonoApi, gchandle_free),
              "URK_MonoApi new fields must be appended.");
static_assert(offsetof(URK_MonoApi, value_box) > offsetof(URK_MonoApi, method_get_object),
              "URK_MonoApi value_box must be appended.");
#endif

typedef enum URK_HookBackend {
    URK_HOOK_BACKEND_AUTO = 0,
    URK_HOOK_BACKEND_DETOURS = 1,
    URK_HOOK_BACKEND_SAFETYHOOK = 2
} URK_HookBackend;

typedef struct URK_HookOptions {
    uint32_t size;
    uint32_t backend;
    uint32_t flags;
} URK_HookOptions;

typedef struct URK_ModContext {
    int version;
    void (*Log)(const char *fmt, ...);
    int (*HookAttach)(void **ppOriginal, void *detour);
    int (*HookDetach)(void **ppOriginal, void *detour);
    int (*HookAttachEx)(void **original, void *detour, const URK_HookOptions *options);
    int (*HookDetachEx)(void **original, void *detour);
    int (*HookBackendAvailable)(uint32_t backend);
    uintptr_t runtimeModuleBase;
    const URK_MonoApi *mono;
    int (*MainThreadRegister)(void (*callback)());
    int (*MainThreadUnregister)(void (*callback)());
    uint32_t size;
    uint32_t runtimeBackend;
    uint64_t runtimeCapabilities;
    const URK_RuntimeApi *runtime;
    const URK_Il2CppApi *il2cpp;
    uintptr_t runtimeBackendModuleBase;
    uintptr_t unityPlayerModuleBase;
    uintptr_t gameAssemblyModuleBase;
    const URK_NetworkApi *network;
} URK_ModContext;

/* Required initialization export. Loaders reject a module when it is missing
 * or returns zero. */
typedef int (*URK_ModInitExFn)(const URK_ModContext *context);

#ifdef __cplusplus
} // extern "C"
namespace URK {
using ModContext = URK_ModContext;
using RuntimeBackend = URK_RuntimeBackend;
using RuntimeCapabilityFlags = URK_RuntimeCapabilityFlags;
using RuntimeModuleKind = URK_RuntimeModuleKind;
using CursorLockState = URK_CursorLockState;
using CursorState = URK_CursorState;
using GraphicsDeviceType = URK_GraphicsDeviceType;
using NetworkApi = URK_NetworkApi;
using NetworkHeader = URK_NetworkHeader;
using NetworkHttpMethod = URK_NetworkHttpMethod;
using NetworkRequest = URK_NetworkRequest;
using NetworkResponse = URK_NetworkResponse;
using NetworkResultFlags = URK_NetworkResultFlags;
using RuntimeApi = URK_RuntimeApi;
using ObjectDestroyRequest = URK_ObjectDestroyRequest;
using ObjectDestroyRequestFlags = URK_ObjectDestroyRequestFlags;
using OnObjectDestroyRequestedFn = URK_OnObjectDestroyRequestedFn;
using Il2CppApi = URK_Il2CppApi;
using MonoApi = URK_MonoApi;
using ModInfo = URK_ModInfo;
} // namespace URK
#endif

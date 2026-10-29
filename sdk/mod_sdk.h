#pragma once
#include <cstddef>
#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

#define URK_SDK_VERSION 34
/* Oldest loader contract the Explorer still accepts. Context and service
 * tables are append-only and size-checked, so an older loader stays usable. */
#define URK_SDK_MIN_COMPAT_VERSION 27
#define URK_MONO_API_VERSION 8
#define URK_RUNTIME_API_VERSION 10
#define URK_IL2CPP_API_VERSION 7
#define URK_NETWORK_API_VERSION 1
#define URK_HOOK_API_VERSION 1
#define URK_UNREAL_API_VERSION 6

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
    URK_RUNTIME_BACKEND_IL2CPP = 2,
    URK_RUNTIME_BACKEND_UNREAL = 3
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
    URK_RUNTIME_CAP_STEAM_IDENTITY = 1ull << 12,
    URK_RUNTIME_CAP_MID_HOOKS = 1ull << 13,
    URK_RUNTIME_CAP_UNREAL_API = 1ull << 14
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
    /* Optional libcurl public-key pin, for example sha256//BASE64_SHA256_SPKI. */
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
    int (*json_request)(const URK_NetworkRequest *request, URK_NetworkResponse *response);
} URK_NetworkApi;

typedef struct URK_SceneInfo {
    uint32_t size;
    /* -1 when Unity strips the optional GetBuildIndexInternal binding. */
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

/* Object.Destroy/DestroyImmediate request; Unity may defer or reject it.
 * objectAddress is identity only: never dereference or retain it. */
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

/* Loader-dispatched window message. Set handled non-zero to skip the game's
 * WndProc; the return value is then the dispatch result. */
typedef intptr_t (*URK_WindowMessageCallback)(void *window, uint32_t message, uintptr_t wparam, intptr_t lparam,
                                              int *handled);

typedef struct URK_RuntimeApi {
    int version;
    uint32_t size;
    uint32_t (*backend)();
    uint64_t (*capabilities)();
    uintptr_t (*module_base)(uint32_t kind);
    /* Last scene seen by the event pump. Zero until one is observed. */
    int (*scene_current)(URK_SceneInfo *scene);
    /* Ref-counted cursor lease per calling module; leaks are released on unload.
     * While held the cursor is visible and unlocked. Zero when unavailable. */
    int (*menu_cursor_set_open)(int open);
    /* Raw Cursor.visible / Cursor.lockState access. */
    int (*cursor_state_get)(URK_CursorState *state);
    int (*cursor_state_set)(const URK_CursorState *state);
    int (*input_get_key)(int32_t keyCode);
    int (*input_get_key_down)(int32_t keyCode);
    int (*input_get_key_up)(int32_t keyCode);
    int (*input_get_mouse_button)(int32_t button);
    int (*input_get_mouse_button_down)(int32_t button);
    int (*input_get_mouse_button_up)(int32_t button);
    /* SystemInfo.graphicsDeviceType, or URK_GRAPHICS_DEVICE_UNKNOWN. Size-check first. */
    int32_t (*graphics_device_type)();
    /* Current SteamID64 from the game's Steam module. Buffer must hold
     * URK_STEAM_ID64_MAX bytes; writes "" and returns zero when Steam is not up. */
    int (*steam_id64)(char *output, size_t output_size);
    /* v7: per-module WndProc callbacks, removed on unload. Size-check first. */
    int (*window_message_register)(void *window, URK_WindowMessageCallback callback);
    int (*window_message_unregister)(void *window, URK_WindowMessageCallback callback);
    intptr_t (*window_message_call_original)(void *window, uint32_t message, uintptr_t wparam, intptr_t lparam);
    /* v8: cursor lease with an explicit owner address inside the mod image. */
    int (*menu_cursor_set_open_owned)(const void *owner_address, int open);
    /* v9: marks a menu as consuming mouse input; separate from cursor visibility.
     * Released on unload. */
    int (*menu_mouse_capture_set_owned)(const void *owner_address, int capture);
    /* v10: non-zero only on the captured Unity main thread. */
    int (*is_main_thread)();
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
    /* Non-zero once IL2CPP metadata lookups are usable. Size-check first. */
    int (*is_available)();
    /* Opaque domain handle for readiness checks, or nullptr. */
    const void *(*domain_get)();
    /* Image by metadata name, or nullptr. */
    const void *(*find_image)(const char *image_name);
    /* Class by image, namespace (null/empty = global) and name, or nullptr. */
    const void *(*find_class)(const char *image_name, const char *namespc, const char *name);
    /* Method by name and parameter count; nullptr when missing or ambiguous. */
    const void *(*find_method)(const void *klass, const char *name, int argc);
    /* Overload by exact parameter type names; nullptr when missing or ambiguous. */
    const void *(*find_method_exact)(const void *klass, const char *name, const char *const *parameter_type_names,
                                     int parameter_count);
    /* Native entry point, or nullptr for stripped, icall or runtime-generated bodies. */
    void *(*method_pointer)(const void *method);
    /* Field by name, or nullptr. */
    const void *(*find_field)(const void *klass, const char *name);
    /* Field offset from metadata; negative on failure (no guessed fallback). */
    int32_t (*field_offset)(const void *field);
    /* Last failure diagnostic for this thread; backend-owned, do not free or keep. */
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
    /* Value types: pass raw storage. Reference types: pass the object (may be NULL). */
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
    /* Pointer-sized managed array length; do not narrow to uint32_t on 64-bit IL2CPP. */
    size_t (*array_length)(void *array);
    void *(*array_addr_with_size)(void *array, int element_size, size_t index);
    /* Object/reference array element access without layout assumptions. */
    void *(*array_ref_at)(void *array, size_t index);
    int (*field_get_value)(void *object, const void *field, void *output);
    /* Value types: storage address. Reference types: object pointer (may be NULL). */
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
    /* Buffers are IL2CPP-allocated; release with free(). Null when free() is missing. */
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
    /* Hooks MethodInfo::methodPointer after validating the target. */
    int (*attach_managed_method_hook)(const URK_Il2CppManagedMethodDesc *method, void **original, void *detour,
                                       const URK_HookOptions *options, URK_Il2CppManagedHookResult *result);
    /* IL2CPP object/array layout queries. Size-check first. */
    uint32_t (*object_header_size)();
    uint32_t (*array_object_header_size)();
    uint32_t (*offset_of_array_length_in_array_object_header)();
    uint32_t (*offset_of_array_bounds_in_array_object_header)();
    uint32_t (*allocation_granularity)();
    int (*array_set_ref)(void *array, size_t index, void *value);
    /* Unity 6 made Il2CppGCHandle pointer-sized; use these instead of the uint32_t ones. */
    uintptr_t (*gchandle_new_v2)(void *object, int pinned);
    uintptr_t (*gchandle_new_weakref_v2)(void *object, int track_resurrection);
    void *(*gchandle_get_target_v2)(uintptr_t gchandle);
    void (*gchandle_free_v2)(uintptr_t gchandle);
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
static_assert(offsetof(URK_RuntimeApi, window_message_register) > offsetof(URK_RuntimeApi, steam_id64),
              "URK_RuntimeApi window message helpers must stay appended.");
static_assert(offsetof(URK_RuntimeApi, window_message_call_original) >
                  offsetof(URK_RuntimeApi, window_message_unregister),
              "URK_RuntimeApi window message helper order changed unexpectedly.");
static_assert(offsetof(URK_RuntimeApi, menu_cursor_set_open_owned) >
                  offsetof(URK_RuntimeApi, window_message_call_original),
              "URK_RuntimeApi owner-explicit cursor helper must stay appended.");
static_assert(offsetof(URK_RuntimeApi, menu_mouse_capture_set_owned) >
                  offsetof(URK_RuntimeApi, menu_cursor_set_open_owned),
              "URK_RuntimeApi mouse-capture helper must stay appended.");
static_assert(offsetof(URK_RuntimeApi, is_main_thread) >
                  offsetof(URK_RuntimeApi, menu_mouse_capture_set_owned),
              "URK_RuntimeApi main-thread query must stay appended.");
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
static_assert(offsetof(URK_Il2CppApi, gchandle_new_v2) > offsetof(URK_Il2CppApi, array_set_ref),
              "URK_Il2CppApi pointer-sized GC handle helpers must stay appended.");
static_assert(offsetof(URK_Il2CppApi, gchandle_free_v2) > offsetof(URK_Il2CppApi, gchandle_new_v2),
              "URK_Il2CppApi pointer-sized GC handle helper order changed unexpectedly.");
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
    /* Pointer-sized managed array length; matches the runtime ABI on 64-bit Mono. */
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
    /* Non-zero for generic method definitions and inflated generic methods. */
    int (*method_is_generic)(const void *method);
} URK_MonoApi;

#ifdef __cplusplus
static_assert(offsetof(URK_MonoApi, method_get_object) > offsetof(URK_MonoApi, gchandle_free),
              "URK_MonoApi new fields must be appended.");
static_assert(offsetof(URK_MonoApi, value_box) > offsetof(URK_MonoApi, method_get_object),
              "URK_MonoApi value_box must be appended.");
static_assert(offsetof(URK_MonoApi, method_is_generic) > offsetof(URK_MonoApi, value_box),
              "URK_MonoApi generic method helper must stay appended.");
#endif

/* An address in the game's own space (object, class, function). Never
 * dereference it; go through the API. URK_UNREAL_NULL_OBJECT means not found. */
typedef uint64_t URK_UnrealObject;
#define URK_UNREAL_NULL_OBJECT ((URK_UnrealObject)0)

typedef enum URK_UnrealPropertyKind {
    URK_UNREAL_PROPERTY_UNKNOWN = 0,
    URK_UNREAL_PROPERTY_BOOL = 1,
    URK_UNREAL_PROPERTY_BYTE = 2,
    URK_UNREAL_PROPERTY_INT8 = 3,
    URK_UNREAL_PROPERTY_INT16 = 4,
    URK_UNREAL_PROPERTY_INT32 = 5,
    URK_UNREAL_PROPERTY_INT64 = 6,
    URK_UNREAL_PROPERTY_UINT16 = 7,
    URK_UNREAL_PROPERTY_UINT32 = 8,
    URK_UNREAL_PROPERTY_UINT64 = 9,
    URK_UNREAL_PROPERTY_FLOAT = 10,
    URK_UNREAL_PROPERTY_DOUBLE = 11,
    URK_UNREAL_PROPERTY_ENUM = 12,
    URK_UNREAL_PROPERTY_NAME = 13,
    URK_UNREAL_PROPERTY_STRING = 14,
    URK_UNREAL_PROPERTY_TEXT = 15,
    URK_UNREAL_PROPERTY_OBJECT = 16,
    URK_UNREAL_PROPERTY_CLASS = 17,
    URK_UNREAL_PROPERTY_WEAK_OBJECT = 18,
    URK_UNREAL_PROPERTY_SOFT_OBJECT = 19,
    URK_UNREAL_PROPERTY_INTERFACE = 20,
    URK_UNREAL_PROPERTY_STRUCT = 21,
    URK_UNREAL_PROPERTY_ARRAY = 22,
    URK_UNREAL_PROPERTY_SET = 23,
    URK_UNREAL_PROPERTY_MAP = 24,
    /* A single-cast delegate: a weak object and a function name. */
    URK_UNREAL_PROPERTY_DELEGATE = 25,
    /* Version 3. An inline multicast delegate: a list of delegate bindings. */
    URK_UNREAL_PROPERTY_MULTICAST_DELEGATE = 26,
    /* Sparse multicast delegate: bindings are not reachable, only the kind is reported. */
    URK_UNREAL_PROPERTY_SPARSE_DELEGATE = 27,
    URK_UNREAL_PROPERTY_LAZY_OBJECT = 28,
    /* FUtf8String / FAnsiString: read and written as text like FString. */
    URK_UNREAL_PROPERTY_UTF8_STRING = 29,
    URK_UNREAL_PROPERTY_ANSI_STRING = 30,
    /* TFieldPath: read as text ("/Script/Engine.Actor:bHidden"), not written. */
    URK_UNREAL_PROPERTY_FIELD_PATH = 31,
    /* TOptional (5.3+): count 0 or 1, element 0 is the value, insert(0, 1) sets a default, clear unsets.
       Only where the set flag follows the value; an intrusive unset state is refused. */
    URK_UNREAL_PROPERTY_OPTIONAL = 32
} URK_UnrealPropertyKind;

/* A member's shape. size is per element; fixed C arrays have array_dim > 1. */
typedef struct URK_UnrealPropertyInfo {
    uint32_t size;
    int32_t kind;
    int32_t element_size;
    int32_t array_dim;
    /* Object/Class: required UClass. Struct: UScriptStruct. Array: element property.
     * Enum: underlying numeric property. Optional: value property. */
    URK_UnrealObject inner;
    /* v2. Bools: byte and bit mask; 0xFF means a whole bool. */
    uint8_t bool_byte_offset;
    uint8_t bool_byte_mask;
    uint8_t bool_field_mask;
    uint8_t reserved;
    /* v3. The type's UEnum, UScriptStruct, UClass or delegate signature; null otherwise. */
    URK_UnrealObject type_object;
} URK_UnrealPropertyInfo;

/* A function's parameter block, built once and reused; access is by name, bounds-checked. */
typedef struct URK_UnrealCallFrame URK_UnrealCallFrame;

/* Every Blueprint call, including script-to-script ones ProcessEvent misses.
 * Called before (after=0) and after (after=1) the body; hot path. */
typedef void (*URK_UnrealScriptCallObserverFn)(void *user_data, URK_UnrealObject object, URK_UnrealObject function,
                                               void *locals, void *result, int after);

/* Object gets (created=1) or loses (created=0) its array slot. Any thread; record only. */
typedef void (*URK_UnrealObjectLifeObserverFn)(void *user_data, URK_UnrealObject object, int created);

/* Runs inside ProcessEvent on the calling thread. Zero drops the call; keep it cheap. */
typedef int (*URK_UnrealProcessEventObserverFn)(void *user_data, URK_UnrealObject object, URK_UnrealObject function,
                                                void *parms);

/* Runs on the game thread. */
typedef void (*URK_UnrealPostedWorkFn)(void *user_data);

/* One call of a hooked function; frame is valid until the callback returns. */
typedef struct URK_UnrealHookedCall {
    URK_UnrealObject object;
    URK_UnrealObject function;
    URK_UnrealCallFrame *frame;
    int32_t after;
    /* After only: a before callback skipped the body. */
    int32_t skipped;
} URK_UnrealHookedCall;

/* Before: zero skips the body. After: result ignored. Calls inside are not hooked. */
typedef int (*URK_UnrealFunctionHookFn)(void *user_data, const URK_UnrealHookedCall *call);

/* v3. A value's location: object member or frame parameter, plus steps.
 * Re-resolved on every use and bounds-checked. */
#define URK_UNREAL_PLACE_MAX_STEPS 8

typedef enum URK_UnrealStepKind {
    /* An array index, a set's slot, or a multicast delegate's binding index. */
    URK_UNREAL_STEP_ELEMENT = 1,
    /* A struct member by name; index selects into a fixed C array member. */
    URK_UNREAL_STEP_MEMBER = 2,
    /* Map slot's key or value; index is the slot from place_slots. */
    URK_UNREAL_STEP_KEY = 3,
    URK_UNREAL_STEP_VALUE = 4
} URK_UnrealStepKind;

typedef struct URK_UnrealStep {
    int32_t kind;
    int32_t index;
    const char *name;
} URK_UnrealStep;

typedef struct URK_UnrealPlace {
    /* Root: object member, or frame parameter when object is null. */
    URK_UnrealObject object;
    URK_UnrealCallFrame *frame;
    const char *member;
    /* Selects into a fixed C array member; zero otherwise. */
    int32_t member_index;
    uint32_t step_count;
    URK_UnrealStep steps[URK_UNREAL_PLACE_MAX_STEPS];
} URK_UnrealPlace;

/* Set element or map key. The field matching the key's kind is read. */
typedef struct URK_UnrealKey {
    int64_t integer;
    double floating;
    URK_UnrealObject object;
    const char *text;
    const void *bytes;
    size_t size;
} URK_UnrealKey;

typedef struct URK_UnrealApi {
    uint32_t version;
    uint32_t size;

    /* Non-zero once calibration resolved. Everything else returns null/zero until then. */
    int (*is_available)();

    /* Engine version; zero when unknown. */
    void (*engine_version)(int32_t *major, int32_t *minor, int32_t *patch);
    /* 4.25+: properties are FField. Informational only. */
    int (*uses_field_properties)();

    /* First object with this name. Names are not unique; use find_in_outer. */
    URK_UnrealObject (*find_object)(const char *name);
    URK_UnrealObject (*find_object_in_outer)(const char *name, const char *outer_name);
    URK_UnrealObject (*class_of)(URK_UnrealObject object);
    URK_UnrealObject (*outer_of)(URK_UnrealObject object);
    /* NUL-terminated, truncated to fit. */
    int (*name_of)(URK_UnrealObject object, char *output, size_t output_size);

    /* Whether struct_object derives from base or is base itself. */
    int (*is_child_of)(URK_UnrealObject struct_object, URK_UnrealObject base);
    /* Same check through the instance's class. */
    int (*is_a)(URK_UnrealObject object, URK_UnrealObject class_object);
    URK_UnrealObject (*default_object_of)(URK_UnrealObject class_object);
    /* Returns the total count, which may exceed capacity. exact excludes subclasses. */
    size_t (*instances_of)(URK_UnrealObject class_object, URK_UnrealObject *output, size_t output_capacity,
                           int exact);

    /* Member shape on the object's class or its bases. Zero when not reflected. */
    int (*describe_property)(URK_UnrealObject object, const char *member_name, URK_UnrealPropertyInfo *info);

    /* Value access by member name. Zero when missing, wrong kind or out of range. */
    int (*read_integer)(URK_UnrealObject object, const char *member_name, int32_t index, int64_t *output);
    int (*read_floating)(URK_UnrealObject object, const char *member_name, int32_t index, double *output);
    int (*read_bool)(URK_UnrealObject object, const char *member_name, int32_t index, int *output);
    URK_UnrealObject (*read_object)(URK_UnrealObject object, const char *member_name, int32_t index);
    /* FName or FString text, copied out. */
    int (*read_name)(URK_UnrealObject object, const char *member_name, int32_t index, char *output,
                     size_t output_size);
    int (*read_string)(URK_UnrealObject object, const char *member_name, int32_t index, char *output,
                       size_t output_size);

    /* In-place writes only; strings, arrays, maps and text go through place_* (v3). */
    int (*write_integer)(URK_UnrealObject object, const char *member_name, int32_t index, int64_t value);
    int (*write_floating)(URK_UnrealObject object, const char *member_name, int32_t index, double value);
    int (*write_bool)(URK_UnrealObject object, const char *member_name, int32_t index, int value);
    int (*write_object)(URK_UnrealObject object, const char *member_name, int32_t index, URK_UnrealObject value);

    /* Function by name on owner_class or a base. An instance stands for its class. */
    URK_UnrealObject (*find_function)(URK_UnrealObject owner_class, const char *name);

    /* Zeroed frame sized to the function's parameter block (padding included). */
    URK_UnrealCallFrame *(*call_frame_create)(URK_UnrealObject function);
    void (*call_frame_destroy)(URK_UnrealCallFrame *frame);
    /* One parameter by name. size must equal its element_size or the call is refused. */
    int (*call_frame_set)(URK_UnrealCallFrame *frame, const char *parameter_name, const void *value, size_t size);
    int (*call_frame_get)(const URK_UnrealCallFrame *frame, const char *parameter_name, void *output, size_t size);

    /* Calls through the object's own ProcessEvent. Game thread only; zero when
     * off-thread, null or the hook is not installed. */
    int (*call)(URK_UnrealObject object, URK_UnrealCallFrame *frame);

    /* Installs the ProcessEvent hook on every override. Idempotent. */
    int (*hook_install)();
    int (*hook_installed)();
    /* Removes the hook once in-flight calls finish. Zero on timeout: the patch
     * stays, so the mod must not unload. */
    int (*hook_remove)();

    /* Single observer; NULL clears. Do not re-enter *_call or *_post from it. */
    void (*process_event_observe)(URK_UnrealProcessEventObserverFn observer, void *user_data);

    /* Most frequent ProcessEvent thread; zero until known. */
    uint32_t (*game_thread_id)();
    /* Queues work for the game thread. Zero when the hook is off or the queue is full. */
    int (*post_to_game_thread)(URK_UnrealPostedWorkFn work, void *user_data);

    /* Version 2: struct values. */

    /* Size of a struct value as a member (aligned, supers included). Zero if not a UStruct. */
    int32_t (*struct_size)(URK_UnrealObject struct_object);
    /* A struct type's member shape and offset. Supers are searched. */
    int (*describe_struct_member)(URK_UnrealObject struct_object, const char *member_name,
                                  URK_UnrealPropertyInfo *info, int32_t *offset);
    /* Whole struct copy; size must equal element_size. Writes may change numbers and
     * nested structs only, not engine-owned or unchecked parts. */
    int (*read_struct)(URK_UnrealObject object, const char *member_name, int32_t index, void *output,
                       size_t size);
    int (*write_struct)(URK_UnrealObject object, const char *member_name, int32_t index, const void *value,
                        size_t size);

    /* v3: every kind through a place. Engine memory is changed only by engine code,
     * on the game thread; plain reads work anywhere. Zero changes nothing. */

    /* Index -1 describes the element type even when the container is empty. */
    int (*place_describe)(const URK_UnrealPlace *place, URK_UnrealPropertyInfo *info);

    /* Integers, bytes and enums (by number). */
    int (*place_read_integer)(const URK_UnrealPlace *place, int64_t *output);
    int (*place_write_integer)(const URK_UnrealPlace *place, int64_t value);
    int (*place_read_floating)(const URK_UnrealPlace *place, double *output);
    int (*place_write_floating)(const URK_UnrealPlace *place, double value);
    int (*place_read_bool)(const URK_UnrealPlace *place, int *output);
    int (*place_write_bool)(const URK_UnrealPlace *place, int value);
    /* Objects, classes and weak/lazy/soft/interface/delegate targets.
     * Writes must be live and of the declared class; lazy takes only null. */
    URK_UnrealObject (*place_read_object)(const URK_UnrealPlace *place);
    int (*place_write_object)(const URK_UnrealPlace *place, URK_UnrealObject value);
    /* UTF-8 text of names, strings, FText, enum names, soft paths, delegate functions.
     * *length gets the full length even when output is too small. */
    int (*place_read_text)(const URK_UnrealPlace *place, char *output, size_t output_size, size_t *length);
    int (*place_write_text)(const URK_UnrealPlace *place, const char *utf8);
    /* A struct's whole value, or a lazy pointer copied from another. */
    int (*place_read_bytes)(const URK_UnrealPlace *place, void *output, size_t size);
    int (*place_write_bytes)(const URK_UnrealPlace *place, const void *value, size_t size);

    /* Elements in an array, set, map or multicast delegate; -1 otherwise. */
    int32_t (*place_count)(const URK_UnrealPlace *place);
    /* Reachable indices in iteration order. Returns the total, may exceed capacity. */
    int32_t (*place_slots)(const URK_UnrealPlace *place, int32_t *output, int32_t capacity);
    /* Array or multicast: inserts count defaults before index (index == count appends). */
    int (*place_insert)(const URK_UnrealPlace *place, int32_t index, int32_t count);
    /* Array: removes count from index. Set/map: the slot at index (count = 1). */
    int (*place_remove)(const URK_UnrealPlace *place, int32_t index, int32_t count);
    /* Empties a container, string or text and releases what it owned. */
    int (*place_clear)(const URK_UnrealPlace *place);
    /* Set or map: the slot holding key, or -1. */
    int32_t (*place_find)(const URK_UnrealPlace *place, const URK_UnrealKey *key);
    /* Slot holding key, adding it when missing. -1 on failure. */
    int32_t (*place_add)(const URK_UnrealPlace *place, const URK_UnrealKey *key);
    /* Binds object + function; refused unless the signature matches. Null object clears. */
    int (*place_bind)(const URK_UnrealPlace *place, URK_UnrealObject object, const char *function);

    /* An enum's entries as the running game defines them. */
    int32_t (*enum_count)(URK_UnrealObject enum_object);
    int (*enum_entry)(URK_UnrealObject enum_object, int32_t index, char *name, size_t name_size, int64_t *value);
    /* By name, with or without the "Enum::" prefix. */
    int (*enum_value)(URK_UnrealObject enum_object, const char *name, int64_t *value);
    int (*enum_name)(URK_UnrealObject enum_object, int64_t value, char *output, size_t output_size);

    /* Version 4 */
    /* One observer; NULL clears. Nonzero when the script hooks are in. */
    int (*script_call_observe)(URK_UnrealScriptCallObserverFn observer, void *user_data);
    /* One observer; NULL clears. Nonzero when the object array hooks are in. */
    int (*object_life_observe)(URK_UnrealObjectLifeObserverFn observer, void *user_data);

    /* Version 5 */
    /* Before/after callbacks for one function (Blueprint overrides included).
     * Native functions called straight from bytecode are not seen. */
    uint64_t (*function_hook_add)(URK_UnrealObject function, URK_UnrealFunctionHookFn before,
                                  URK_UnrealFunctionHookFn after, void *user_data);
    /* Non-zero once no callback will run again. Zero on timeout: stay loaded. */
    int (*function_hook_remove)(uint64_t id);

    /* Version 6 */
    /* Calls back on each broadcast of a multicast delegate (inline or sparse), as a binding would:
     * the call's object is the delegate's owner, its frame the broadcast's parameters. Game thread. */
    uint64_t (*delegate_subscribe)(const URK_UnrealPlace *place, URK_UnrealFunctionHookFn callback, void *user_data);
    /* Non-zero once the callback will not run again. Zero on timeout: stay loaded. */
    int (*delegate_unsubscribe)(uint64_t id);
} URK_UnrealApi;

#ifdef __cplusplus
static_assert(offsetof(URK_UnrealApi, is_available) > offsetof(URK_UnrealApi, size),
              "URK_UnrealApi must keep version and size before callable entries.");
static_assert(offsetof(URK_UnrealApi, hook_install) > offsetof(URK_UnrealApi, call),
              "URK_UnrealApi new fields must be appended.");
static_assert(offsetof(URK_UnrealApi, post_to_game_thread) > offsetof(URK_UnrealApi, game_thread_id),
              "URK_UnrealApi new fields must be appended.");
static_assert(offsetof(URK_UnrealApi, struct_size) > offsetof(URK_UnrealApi, post_to_game_thread),
              "URK_UnrealApi new fields must be appended.");
static_assert(offsetof(URK_UnrealApi, place_describe) > offsetof(URK_UnrealApi, write_struct),
              "URK_UnrealApi new fields must be appended.");
static_assert(offsetof(URK_UnrealApi, function_hook_add) > offsetof(URK_UnrealApi, object_life_observe),
              "URK_UnrealApi new fields must be appended.");
static_assert(offsetof(URK_UnrealPropertyInfo, type_object) > offsetof(URK_UnrealPropertyInfo, reserved),
              "URK_UnrealPropertyInfo new fields must be appended.");
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

typedef union URK_HookXmmRegister {
    uint8_t u8[16];
    uint16_t u16[8];
    uint32_t u32[4];
    uint64_t u64[2];
    float f32[4];
    double f64[2];
} URK_HookXmmRegister;

/* Mid-function hook registers (x64). Writes take effect on resume. */
typedef struct URK_HookRegisters {
    uint32_t size;
    uint32_t reserved;
    URK_HookXmmRegister xmm[16];
    uintptr_t rflags;
    uintptr_t r15;
    uintptr_t r14;
    uintptr_t r13;
    uintptr_t r12;
    uintptr_t r11;
    uintptr_t r10;
    uintptr_t r9;
    uintptr_t r8;
    uintptr_t rdi;
    uintptr_t rsi;
    uintptr_t rdx;
    uintptr_t rcx;
    uintptr_t rbx;
    uintptr_t rax;
    uintptr_t rbp;
    /* Stack pointer at the hook site. Read-only: writes are ignored. */
    uintptr_t rsp;
    /* Stack pointer used when execution resumes. Write this instead of rsp. */
    uintptr_t trampoline_rsp;
    /* Points at the displaced-instruction trampoline; write to redirect. */
    uintptr_t rip;
} URK_HookRegisters;

typedef void (*URK_MidHookCallbackFn)(URK_HookRegisters *registers, void *userData);

typedef struct URK_MidHookOptions {
    uint32_t size;
    uint32_t flags;
    void *userData;
} URK_MidHookOptions;

typedef struct URK_MidHookHandle URK_MidHookHandle;

typedef struct URK_HookApi {
    uint32_t version;
    uint32_t size;
    /* Hooks an instruction boundary. NULL when unhookable or the pool is full. */
    URK_MidHookHandle *(*mid_attach)(void *target, URK_MidHookCallbackFn callback,
                                      const URK_MidHookOptions *options);
    int (*mid_detach)(URK_MidHookHandle *hook);
    int (*mid_set_enabled)(URK_MidHookHandle *hook, int enabled);
} URK_HookApi;

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
    const URK_HookApi *hooks;
    /* Unreal only, and null until calibration resolves. Always check. */
    const URK_UnrealApi *unreal;
} URK_ModContext;

static_assert(offsetof(URK_HookApi, mid_attach) > offsetof(URK_HookApi, size),
              "URK_HookApi must stay append-only.");
static_assert(offsetof(URK_ModContext, hooks) > offsetof(URK_ModContext, network),
              "URK_ModContext hook API pointer must stay appended.");
static_assert(offsetof(URK_ModContext, unreal) > offsetof(URK_ModContext, hooks),
              "URK_ModContext unreal API pointer must stay appended.");

/* Required export; the loader rejects the module when missing or zero. */
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
using HookApi = URK_HookApi;
using HookRegisters = URK_HookRegisters;
using MidHookCallbackFn = URK_MidHookCallbackFn;
using MidHookHandle = URK_MidHookHandle;
using MidHookOptions = URK_MidHookOptions;
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
using UnrealApi = URK_UnrealApi;
using UnrealObject = URK_UnrealObject;
using UnrealPropertyKind = URK_UnrealPropertyKind;
using UnrealPropertyInfo = URK_UnrealPropertyInfo;
using UnrealCallFrame = URK_UnrealCallFrame;
using UnrealProcessEventObserverFn = URK_UnrealProcessEventObserverFn;
using UnrealPostedWorkFn = URK_UnrealPostedWorkFn;
using UnrealPlace = URK_UnrealPlace;
using UnrealStep = URK_UnrealStep;
using UnrealKey = URK_UnrealKey;
} // namespace URK
#endif

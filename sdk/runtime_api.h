#pragma once

#include "sdk/mod_sdk.h"
#include <cstddef>
#include <cstdint>

namespace URK {
using GetModInfoFn = ::URK_GetModInfoFn;
using HookBackend = ::URK_HookBackend;
using HookOptions = ::URK_HookOptions;
using Il2CppApi = ::URK_Il2CppApi;
using ModContext = ::URK_ModContext;
using ModInfo = ::URK_ModInfo;
using MonoApi = ::URK_MonoApi;
using NetworkApi = ::URK_NetworkApi;
using NetworkHeader = ::URK_NetworkHeader;
using NetworkHttpMethod = ::URK_NetworkHttpMethod;
using NetworkRequest = ::URK_NetworkRequest;
using NetworkResponse = ::URK_NetworkResponse;
using NetworkResultFlags = ::URK_NetworkResultFlags;
using RuntimeApi = ::URK_RuntimeApi;
using RuntimeBackend = ::URK_RuntimeBackend;
using RuntimeCapabilityFlags = ::URK_RuntimeCapabilityFlags;
using RuntimeModuleKind = ::URK_RuntimeModuleKind;
using SceneInfo = ::URK_SceneInfo;
using CursorLockState = ::URK_CursorLockState;
using CursorState = ::URK_CursorState;
using OnSceneLoadedFn = ::URK_OnSceneLoadedFn;
using OnSceneChangedFn = ::URK_OnSceneChangedFn;
using ObjectDestroyRequest = ::URK_ObjectDestroyRequest;
using ObjectDestroyRequestFlags = ::URK_ObjectDestroyRequestFlags;
using OnObjectDestroyRequestedFn = ::URK_OnObjectDestroyRequestedFn;

inline constexpr RuntimeBackend runtime_backend_unknown =
    URK_RUNTIME_BACKEND_UNKNOWN;
inline constexpr RuntimeBackend runtime_backend_mono = URK_RUNTIME_BACKEND_MONO;
inline constexpr RuntimeBackend runtime_backend_il2cpp =
    URK_RUNTIME_BACKEND_IL2CPP;

inline constexpr HookBackend hook_backend_auto = URK_HOOK_BACKEND_AUTO;
inline constexpr HookBackend hook_backend_detours = URK_HOOK_BACKEND_DETOURS;

inline constexpr std::uint64_t runtime_cap_none = URK_RUNTIME_CAP_NONE;
inline constexpr std::uint64_t runtime_cap_mono_api = URK_RUNTIME_CAP_MONO_API;
inline constexpr std::uint64_t runtime_cap_il2cpp_api =
    URK_RUNTIME_CAP_IL2CPP_API;
inline constexpr std::uint64_t runtime_cap_hooks = URK_RUNTIME_CAP_HOOKS;
inline constexpr std::uint64_t runtime_cap_main_thread =
    URK_RUNTIME_CAP_MAIN_THREAD;
inline constexpr std::uint64_t runtime_cap_scene_events =
    URK_RUNTIME_CAP_SCENE_EVENTS;
inline constexpr std::uint64_t runtime_cap_cursor_control =
    URK_RUNTIME_CAP_CURSOR_CONTROL;
inline constexpr std::uint64_t runtime_cap_network = URK_RUNTIME_CAP_NETWORK;
inline constexpr std::uint64_t runtime_cap_input = URK_RUNTIME_CAP_INPUT;
inline constexpr std::uint64_t runtime_cap_graphics_device =
    URK_RUNTIME_CAP_GRAPHICS_DEVICE_TYPE;
inline constexpr std::uint64_t runtime_cap_object_destroy_request_events =
    URK_RUNTIME_CAP_OBJECT_DESTROY_REQUEST_EVENTS;
inline constexpr std::uint64_t runtime_cap_steam_identity =
    URK_RUNTIME_CAP_STEAM_IDENTITY;
inline constexpr std::int32_t graphics_device_unknown =
    URK_GRAPHICS_DEVICE_UNKNOWN;
inline constexpr std::int32_t graphics_device_direct3d11 =
    URK_GRAPHICS_DEVICE_D3D11;
inline constexpr std::int32_t graphics_device_direct3d12 =
    URK_GRAPHICS_DEVICE_D3D12;
inline constexpr std::int32_t graphics_device_opengl2 =
    URK_GRAPHICS_DEVICE_OPENGL2;
inline constexpr std::int32_t graphics_device_openglcore =
    URK_GRAPHICS_DEVICE_OPENGL_CORE;
inline constexpr NetworkHttpMethod network_http_get = URK_NETWORK_HTTP_GET;
inline constexpr NetworkHttpMethod network_http_post = URK_NETWORK_HTTP_POST;
inline constexpr NetworkHttpMethod network_http_put = URK_NETWORK_HTTP_PUT;
inline constexpr NetworkHttpMethod network_http_update =
    URK_NETWORK_HTTP_UPDATE;
inline constexpr NetworkHttpMethod network_http_patch = URK_NETWORK_HTTP_PATCH;
inline constexpr NetworkHttpMethod network_http_delete =
    URK_NETWORK_HTTP_DELETE;
inline constexpr std::uint32_t network_result_none = URK_NETWORK_RESULT_NONE;
inline constexpr std::uint32_t network_result_body_truncated =
    URK_NETWORK_RESULT_BODY_TRUNCATED;
inline constexpr std::uint32_t network_result_error_truncated =
    URK_NETWORK_RESULT_ERROR_TRUNCATED;
inline constexpr CursorLockState cursor_lock_none = URK_CURSOR_LOCK_NONE;
inline constexpr CursorLockState cursor_lock_locked = URK_CURSOR_LOCK_LOCKED;
inline constexpr CursorLockState cursor_lock_confined = URK_CURSOR_LOCK_CONFINED;
inline const ModContext *&ContextSlot() {
  static const ModContext *ctx = nullptr;
  return ctx;
}

inline void set_context(const ModContext *ctx) { ContextSlot() = ctx; }
inline const ModContext *context() { return ContextSlot(); }

inline bool context_has_field(std::size_t fieldEnd) {
  const auto *ctx = context();
  return ctx && ctx->size >= fieldEnd;
}

inline std::uint64_t runtime_capabilities() {
  const auto *ctx = context();
  if (!ctx || ctx->version < URK_SDK_VERSION ||
      !context_has_field(offsetof(ModContext, runtimeCapabilities) +
                         sizeof(ctx->runtimeCapabilities)))
    return runtime_cap_none;
  const std::uint64_t contextCapabilities = ctx->runtimeCapabilities;
  if (context_has_field(offsetof(ModContext, runtime) + sizeof(ctx->runtime)) &&
      ctx->runtime && ctx->runtime->version >= 1 &&
      ctx->runtime->size >=
           offsetof(RuntimeApi, capabilities) + sizeof(ctx->runtime->capabilities) &&
      ctx->runtime->capabilities) {
    return contextCapabilities & ctx->runtime->capabilities();
  }
  return contextCapabilities;
}

inline bool has_runtime_capability(std::uint64_t capability) {
  return (runtime_capabilities() & capability) != 0;
}
inline bool has_mono_api() {
  return has_runtime_capability(runtime_cap_mono_api);
}
inline bool has_il2cpp_api() {
  return has_runtime_capability(runtime_cap_il2cpp_api);
}
inline bool has_hooks() { return has_runtime_capability(runtime_cap_hooks); }
inline bool has_main_thread() {
  return has_runtime_capability(runtime_cap_main_thread);
}
inline bool has_scene_events() {
  return has_runtime_capability(runtime_cap_scene_events);
}
inline bool has_object_destroy_request_events() {
  return has_runtime_capability(runtime_cap_object_destroy_request_events);
}
inline bool has_cursor_control() {
  return has_runtime_capability(runtime_cap_cursor_control);
}
inline bool has_network() { return has_runtime_capability(runtime_cap_network); }
inline bool has_input() { return has_runtime_capability(runtime_cap_input); }
inline bool has_graphics_device() {
  return has_runtime_capability(runtime_cap_graphics_device);
}
inline bool has_steam_identity() {
  return has_runtime_capability(runtime_cap_steam_identity);
}

inline bool runtime_api_has_field(std::size_t fieldEnd) {
  const auto *ctx = context();
  return ctx && ctx->runtime && ctx->runtime->size >= fieldEnd;
}
inline std::int32_t graphics_device_type() {
  const auto *ctx = context();
  return has_graphics_device() &&
                 runtime_api_has_field(offsetof(RuntimeApi, graphics_device_type) +
                                       sizeof(ctx->runtime->graphics_device_type)) &&
                 ctx->runtime->graphics_device_type
             ? ctx->runtime->graphics_device_type()
             : graphics_device_unknown;
}
inline bool steam_id64(char *output, std::size_t output_size) {
  if (!output || output_size == 0)
    return false;
  output[0] = '\0';
  const auto *ctx = context();
  return has_steam_identity() &&
         runtime_api_has_field(offsetof(RuntimeApi, steam_id64) +
                               sizeof(ctx->runtime->steam_id64)) &&
         ctx->runtime->steam_id64 &&
         ctx->runtime->steam_id64(output, output_size) != 0;
}
inline bool current_scene(SceneInfo *scene) {
  if (!scene)
    return false;
  scene->size = sizeof(SceneInfo);
  const auto *ctx = context();
  return runtime_api_has_field(offsetof(RuntimeApi, scene_current) +
                               sizeof(ctx->runtime->scene_current)) &&
         ctx->runtime->scene_current &&
         ctx->runtime->scene_current(scene) != 0;
}
inline bool set_menu_cursor_open(bool open) {
  const auto *ctx = context();
  return runtime_api_has_field(offsetof(RuntimeApi, menu_cursor_set_open) +
                               sizeof(ctx->runtime->menu_cursor_set_open)) &&
         ctx->runtime->menu_cursor_set_open &&
         ctx->runtime->menu_cursor_set_open(open ? 1 : 0) != 0;
}
inline bool cursor_state_get(CursorState *state) {
  if (!state)
    return false;
  state->size = sizeof(CursorState);
  const auto *ctx = context();
  return runtime_api_has_field(offsetof(RuntimeApi, cursor_state_get) +
                               sizeof(ctx->runtime->cursor_state_get)) &&
         ctx->runtime->cursor_state_get &&
         ctx->runtime->cursor_state_get(state) != 0;
}
inline bool cursor_state_set(bool visible, CursorLockState lockState) {
  CursorState state{};
  state.size = sizeof(state);
  state.visible = visible ? 1 : 0;
  state.lockState = static_cast<std::int32_t>(lockState);
  const auto *ctx = context();
  return runtime_api_has_field(offsetof(RuntimeApi, cursor_state_set) +
                               sizeof(ctx->runtime->cursor_state_set)) &&
         ctx->runtime->cursor_state_set &&
         ctx->runtime->cursor_state_set(&state) != 0;
}
inline bool cursor_state_set(const CursorState *state) {
  if (!state || state->size < sizeof(CursorState))
    return false;
  const auto *ctx = context();
  return runtime_api_has_field(offsetof(RuntimeApi, cursor_state_set) +
                               sizeof(ctx->runtime->cursor_state_set)) &&
         ctx->runtime->cursor_state_set &&
         ctx->runtime->cursor_state_set(state) != 0;
}
inline bool input_get_key(std::int32_t keyCode) {
  const auto *ctx = context();
  return runtime_api_has_field(offsetof(RuntimeApi, input_get_key) +
                               sizeof(ctx->runtime->input_get_key)) &&
         ctx->runtime->input_get_key &&
         ctx->runtime->input_get_key(keyCode) != 0;
}
inline bool input_get_key_down(std::int32_t keyCode) {
  const auto *ctx = context();
  return runtime_api_has_field(offsetof(RuntimeApi, input_get_key_down) +
                               sizeof(ctx->runtime->input_get_key_down)) &&
         ctx->runtime->input_get_key_down &&
         ctx->runtime->input_get_key_down(keyCode) != 0;
}
inline bool input_get_key_up(std::int32_t keyCode) {
  const auto *ctx = context();
  return runtime_api_has_field(offsetof(RuntimeApi, input_get_key_up) +
                               sizeof(ctx->runtime->input_get_key_up)) &&
         ctx->runtime->input_get_key_up &&
         ctx->runtime->input_get_key_up(keyCode) != 0;
}
inline bool input_get_mouse_button(std::int32_t button) {
  const auto *ctx = context();
  return runtime_api_has_field(offsetof(RuntimeApi, input_get_mouse_button) +
                               sizeof(ctx->runtime->input_get_mouse_button)) &&
         ctx->runtime->input_get_mouse_button &&
         ctx->runtime->input_get_mouse_button(button) != 0;
}
inline bool input_get_mouse_button_down(std::int32_t button) {
  const auto *ctx = context();
  return runtime_api_has_field(offsetof(RuntimeApi, input_get_mouse_button_down) +
                               sizeof(ctx->runtime->input_get_mouse_button_down)) &&
         ctx->runtime->input_get_mouse_button_down &&
         ctx->runtime->input_get_mouse_button_down(button) != 0;
}
inline bool input_get_mouse_button_up(std::int32_t button) {
  const auto *ctx = context();
  return runtime_api_has_field(offsetof(RuntimeApi, input_get_mouse_button_up) +
                               sizeof(ctx->runtime->input_get_mouse_button_up)) &&
         ctx->runtime->input_get_mouse_button_up &&
         ctx->runtime->input_get_mouse_button_up(button) != 0;
}

inline void log(const char *text) {
  const auto *ctx = context();
  if (ctx && ctx->Log)
    ctx->Log("%s", text ? text : "");
}
} // namespace URK
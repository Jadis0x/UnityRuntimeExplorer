// Copyright (c) 2026 Jadis0x. All rights reserved.
#include "unity_log_hook.h"

#include "config/mod_config.h"
#include "support/mod_log.h"

#include "sdk/runtime_api.h"
#include "sdk/hook_api.h"
#include "sdk/runtime/managed_hooks.h"
#include "sdk/runtime/managed_strings.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#endif

#include <cstdio>
#include <cstring>
#include <string>
#include <utility>

namespace ModUnityLogHook {
#if defined(URK_BACKEND_MONO)
using DebugLogFn = void(__fastcall*)(void* message);
#else
using DebugLogFn = void(__fastcall*)(void* message, void* method_info);
#endif

inline DebugLogFn g_originals[3]{};
inline bool g_installed = false;

enum class LogLevel { info, warning, error };

#if defined(_WIN32)
bool invoke_log_guarded(void (*operation)(void*), void* context) noexcept {
  __try {
    operation(context);
    return true;
  }
  __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}
#endif

inline std::string message_text(void* message) {
  if (!message)
    return "<null>";

  std::string result;
#if defined(_WIN32)
  auto operation = [](void* raw_context) {
    auto& context =
        *static_cast<std::pair<void*, std::string*>*>(raw_context);
    void* message = context.first;
    std::string& result = *context.second;
#endif
  auto* object = static_cast<URK::managed::Object*>(message);
  const auto* klass = URK::managed::object_get_class(object);
  const char* namespc = klass ? URK::managed::class_get_namespace(klass) : nullptr;
  const char* name = klass ? URK::managed::class_get_name(klass) : nullptr;
  if (namespc && name && std::strcmp(namespc, "System") == 0 &&
      std::strcmp(name, "String") == 0) {
    result = URK::managed_strings::to_utf8(
        static_cast<URK::managed::String*>(message), "<unreadable Unity log message>");
  } else {
    char fallback[192]{};
    std::snprintf(fallback, sizeof(fallback), "<%s%s%s object at %p>",
                  namespc && namespc[0] ? namespc : "",
                  namespc && namespc[0] ? "." : "",
                  name && name[0] ? name : "unknown", message);
    result = fallback;
  }
#if defined(_WIN32)
  };
  std::pair<void*, std::string*> context{message, &result};
  if (!invoke_log_guarded(operation, &context))
    return "<unreadable Unity log object>";
#else
  (void)message;
#endif
  return result;
}

inline void write(LogLevel level, void* message) {
  const std::string text = message_text(message);
  switch (level) {
  case LogLevel::warning: ModLog::warn("[Unity] %s", text.c_str()); break;
  case LogLevel::error: ModLog::error("[Unity] %s", text.c_str()); break;
  default: ModLog::info("[Unity] %s", text.c_str()); break;
  }
}

inline void __fastcall detour_log(
    void* message
#if !defined(URK_BACKEND_MONO)
    , void* method_info
#endif
) {
  write(LogLevel::info, message);
  if (g_originals[0])
#if defined(URK_BACKEND_MONO)
    g_originals[0](message);
#else
    g_originals[0](message, method_info);
#endif
}

inline void __fastcall detour_warning(
    void* message
#if !defined(URK_BACKEND_MONO)
    , void* method_info
#endif
) {
  write(LogLevel::warning, message);
  if (g_originals[1])
#if defined(URK_BACKEND_MONO)
    g_originals[1](message);
#else
    g_originals[1](message, method_info);
#endif
}

inline void __fastcall detour_error(
    void* message
#if !defined(URK_BACKEND_MONO)
    , void* method_info
#endif
) {
  write(LogLevel::error, message);
  if (g_originals[2])
#if defined(URK_BACKEND_MONO)
    g_originals[2](message);
#else
    g_originals[2](message, method_info);
#endif
}

inline constexpr const char* k_method_names[] = {"Log", "LogWarning", "LogError"};
inline DebugLogFn k_detours[] = {&detour_log, &detour_warning, &detour_error};

inline bool attach(const char* image_name, const char* method_name,
                   DebugLogFn* original, DebugLogFn detour) {
  const char* parameter_types[] = {"System.Object"};
  return URK::managed_hooks::try_hook_managed_method(
      image_name, "UnityEngine", "Debug", method_name,
      parameter_types, 1, original, detour,
      [](const char* text) { ModLog::warn("%s", text ? text : ""); });
}

inline void detach(DebugLogFn* original, DebugLogFn detour) {
  if (*original)
    URK::hooks::detach_ex(reinterpret_cast<void**>(original),
                          reinterpret_cast<void*>(detour));
  *original = nullptr;
}

inline bool try_install_for_image(const char* image_name) {
  for (std::size_t index = 0; index < 3; ++index) {
    if (attach(image_name, k_method_names[index], &g_originals[index], k_detours[index]))
      continue;
    g_originals[index] = nullptr;
    while (index > 0) {
      --index;
      detach(&g_originals[index], k_detours[index]);
    }
    return false;
  }
  return true;
}

bool install(const URK_ModContext* ctx) {
  URK::set_context(ctx);
  if (!ctx || !ModConfig::enable_unity_log_hook)
    return true;
  if (g_installed)
    return true;
  if (!URK::managed::init(ctx) || !URK::hooks::available()) {
    ModLog::warn("%s Unity log hooks are unavailable", URK::compiled_runtime_name);
    return false;
  }

  g_installed = try_install_for_image("UnityEngine.CoreModule.dll") ||
                try_install_for_image("UnityEngine.dll");
  if (!g_installed)
    ModLog::warn("Unity Log/LogWarning/LogError hooks were not installed");
  return g_installed;
}

void uninstall() {
  if (g_installed) {
    for (std::size_t index = 3; index > 0; --index)
      detach(&g_originals[index - 1], k_detours[index - 1]);
  }
  g_installed = false;
}
} // namespace ModUnityLogHook

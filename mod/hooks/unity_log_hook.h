// Copyright (c) 2026 Jadis0x. All rights reserved.
#pragma once

#include "config/mod_config.h"
#include "support/mod_log.h"

#include "sdk/runtime_api.h"
#include "sdk/hook_api.h"
#include "sdk/il2cpp/il2cpp_runtime.h"
#include "sdk/il2cpp/il2cpp_helpers.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace ModUnityLogHook {
using DebugLogFn = void(__fastcall*)(void* message, void* method_info);

inline DebugLogFn g_originals[3]{};
inline bool g_installed = false;

enum class LogLevel { info, warning, error };

inline std::string message_text(void* message) {
  if (!message)
    return "<null>";

#if defined(_WIN32)
  __try {
#endif
  auto* object = static_cast<URK::il2cpp::Object*>(message);
  const auto* klass = URK::il2cpp::object_get_class(object);
  const char* namespc = klass ? URK::il2cpp::class_get_namespace(klass) : nullptr;
  const char* name = klass ? URK::il2cpp::class_get_name(klass) : nullptr;
  if (namespc && name && std::strcmp(namespc, "System") == 0 &&
      std::strcmp(name, "String") == 0) {
    return URK::il2cpp::helpers::to_utf8(
        static_cast<URK::il2cpp::String*>(message), "<unreadable Unity log message>");
  }

  char fallback[192]{};
  std::snprintf(fallback, sizeof(fallback), "<%s%s%s object at %p>",
                namespc && namespc[0] ? namespc : "",
                namespc && namespc[0] ? "." : "", name && name[0] ? name : "unknown",
                message);
  return fallback;
#if defined(_WIN32)
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return "<unreadable Unity log object>";
  }
#endif
}

inline void write(LogLevel level, void* message) {
  const std::string text = message_text(message);
  switch (level) {
  case LogLevel::warning: ModLog::warn("[Unity] %s", text.c_str()); break;
  case LogLevel::error: ModLog::error("[Unity] %s", text.c_str()); break;
  default: ModLog::info("[Unity] %s", text.c_str()); break;
  }
}

inline void __fastcall detour_log(void* message, void* method_info) {
  write(LogLevel::info, message);
  if (g_originals[0])
    g_originals[0](message, method_info);
}

inline void __fastcall detour_warning(void* message, void* method_info) {
  write(LogLevel::warning, message);
  if (g_originals[1])
    g_originals[1](message, method_info);
}

inline void __fastcall detour_error(void* message, void* method_info) {
  write(LogLevel::error, message);
  if (g_originals[2])
    g_originals[2](message, method_info);
}

inline constexpr const char* k_method_names[] = {"Log", "LogWarning", "LogError"};
inline DebugLogFn k_detours[] = {&detour_log, &detour_warning, &detour_error};

inline bool attach(const char* image_name, const char* method_name,
                   DebugLogFn* original, DebugLogFn detour) {
  return Il2CppHook::attach(image_name,
                            "UnityEngine",
                            "Debug",
                            method_name,
                            {"System.Object"},
                            original,
                            detour,
                            [](const char* text) {
                              ModLog::warn("%s", text ? text : "");
                            });
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

inline bool install(const URK_ModContext* ctx) {
  URK::set_context(ctx);
  if (!ctx || !ModConfig::enable_unity_log_hook)
    return true;
  if (g_installed)
    return true;
  if (!URK::il2cpp::init(ctx) || !URK::hooks::available()) {
    ModLog::warn("IL2CPP Unity log hooks are unavailable");
    return false;
  }

  g_installed = try_install_for_image("UnityEngine.CoreModule.dll") ||
                try_install_for_image("UnityEngine.dll");
  if (!g_installed)
    ModLog::warn("Unity Log/LogWarning/LogError hooks were not installed");
  return g_installed;
}

inline void uninstall() {
  if (g_installed) {
    for (std::size_t index = 3; index > 0; --index)
      detach(&g_originals[index - 1], k_detours[index - 1]);
  }
  g_installed = false;
}
} // namespace ModUnityLogHook

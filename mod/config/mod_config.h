// Copyright (c) 2026 Jadis0x. All rights reserved.
#pragma once

#include "project_version.h"

#include <atomic>

namespace ModConfig {
#if defined(URK_BACKEND_MONO)
inline constexpr const char *project_name = "URK_Mono_UnityRuntimeExplorer";
inline constexpr const char *display_name = "URK.Mono.UnityRuntimeExplorer";
inline constexpr const char *backend_name = "Mono";
inline constexpr bool is_il2cpp_backend = false;
inline constexpr const char *mod_id = "URK_Mono_UnityRuntimeExplorer_24859613";
#else
inline constexpr const char *project_name = "URK_Il2cpp_UnityRuntimeExplorer";
inline constexpr const char *display_name = "URK.Il2cpp.UnityRuntimeExplorer";
inline constexpr const char *backend_name = "IL2CPP";
inline constexpr bool is_il2cpp_backend = true;
inline constexpr const char *mod_id = "URK_Il2cpp_UnityRuntimeExplorer_24859613";
#endif
// Stable namespace for this mod's deployed resources. Do not change it after release.
inline constexpr const char *author = "Jadis0x";
inline constexpr const char *version = URK::project_version;
inline constexpr const char *url = "https://github.com/Jadis0x/UnityRuntimeExplorer";
inline constexpr const char *social = "https://buymeacoffee.com/jadis0x";
inline constexpr const char *description = "A live IL2CPP and Mono Unity inspector with secure MCP research and method tracing, built on URKit";
inline bool show_menu = false;
// Instrumentation through MCP requires an explicit, persisted in-game opt-in.
// MCP permissions are authoritative in the injected Explorer. The helper never
// grants itself capabilities through request arguments.
inline std::atomic<bool> enable_mcp{true};
inline std::atomic<bool> enable_mcp_auto_discovery{true};
inline std::atomic<bool> enable_mcp_property_access{true};
inline std::atomic<bool> enable_mcp_writes{true};
inline std::atomic<bool> enable_mcp_tracing{true};
inline std::atomic<bool> enable_mcp_invocation{true};
inline std::atomic<bool> enable_mcp_destructive_operations{true};
// English is used as the fixed language when localization support is not generated.
inline bool enable_localization = false;
inline constexpr const char *default_language = "en";
inline bool enable_unity_log_hook = false;
// Loaded from URK_Explorer.ini during startup.
inline int menu_toggle_key = 0x76;
} // namespace ModConfig

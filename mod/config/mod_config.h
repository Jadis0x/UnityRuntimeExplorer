// Copyright (c) 2026 Jadis0x. All rights reserved.
#pragma once

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
inline constexpr const char *version = "0.2.0";
inline constexpr const char *url = "https://github.com/Jadis0x/UnityRuntimeExplorer";
inline constexpr const char *social = "https://buymeacoffee.com/jadis0x";
inline constexpr const char *description = "A live in-game hierarchy and inspector for Windows x64 Unity games";
inline bool show_menu = false;
// English is used as the fixed language when localization support is not generated.
inline bool enable_localization = false;
inline constexpr const char *default_language = "en";
inline bool enable_unity_log_hook = false;
// Loaded from URK_Explorer.ini during startup.
inline int menu_toggle_key = 0x76;
} // namespace ModConfig

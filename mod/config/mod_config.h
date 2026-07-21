// Copyright (c) 2026 Jadis0x. All rights reserved.
#pragma once

namespace ModConfig {
inline constexpr const char *project_name = "URK_Il2cpp_UnityRuntimeExplorer";
// Stable namespace for this mod's deployed resources. Do not change it after release.
inline constexpr const char *mod_id = "URK_Il2cpp_UnityRuntimeExplorer_24859613";
inline constexpr const char *display_name = "URK.Il2cpp.UnityRuntimeExplorer";
inline constexpr const char *author = "Jadis0x";
inline constexpr const char *version = "0.1.1";
inline constexpr const char *url = "https://github.com/Jadis0x/UnityRuntimeExplorer";
inline constexpr const char *social = "https://buymeacoffee.com/jadis0x";
inline constexpr const char *description = "A live Unity-style hierarchy and inspector for IL2CPP games";
inline constexpr bool is_il2cpp_backend = true;
inline bool show_menu = true;
// English is used as the fixed language when localization support is not generated.
inline bool enable_localization = false;
inline constexpr const char *default_language = "en";
inline bool enable_unity_log_hook = false;
// Win32 virtual-key code used by the generated ImGui WndProc toggle.
// Default: VK_TAB (0x09). Change this value to customize the menu key.
inline int menu_toggle_key = 0x09;
} // namespace ModConfig

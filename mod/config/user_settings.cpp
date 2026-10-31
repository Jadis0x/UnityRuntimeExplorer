// Copyright (c) 2026 Jadis0x. All rights reserved.
#include "user_settings.h"

#include "mod_config.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cwchar>
#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>

namespace ModConfig::UserSettings {
namespace {

std::string g_last_error;
int g_module_anchor = 0;
std::array<bool, 256> g_capture_key_down{};
bool g_capture_active = false;
// Opaque by default: a translucent panel over a bright scene is what made text hard to read.
float g_ui_scale = 1.0f;
float g_panel_opacity = 1.0f;

float read_float(const wchar_t* key, float fallback, float minimum, float maximum, const std::filesystem::path& path) {
    wchar_t text[32]{};
    GetPrivateProfileStringW(L"Explorer", key, L"", text, static_cast<DWORD>(std::size(text)), path.c_str());
    wchar_t* end = nullptr;
    const float value = std::wcstof(text, &end);
    if (end == text || !std::isfinite(value))
        return fallback;
    return std::clamp(value, minimum, maximum);
}

int read_mcp_int(const wchar_t* key, int fallback, int minimum, int maximum,
                const std::filesystem::path& path) {
    const UINT stored = GetPrivateProfileIntW(L"MCP", key, static_cast<UINT>(fallback), path.c_str());
    return std::clamp(static_cast<int>(stored), minimum, maximum);
}

std::filesystem::path settings_path() {
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&g_module_anchor), &module)) {
        return {};
    }

    std::array<wchar_t, 32768> path{};
    const DWORD length = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size())
        return {};
    return std::filesystem::path(path.data(), path.data() + length).parent_path() / L"URK_Explorer.ini";
}

bool valid_virtual_key(int key) {
    return key >= 0x08 && key <= 0xfe;
}

bool ignored_capture_key(int key) {
    return key == VK_LBUTTON || key == VK_RBUTTON || key == VK_MBUTTON ||
        key == VK_XBUTTON1 || key == VK_XBUTTON2;
}

void set_win32_error(std::string operation) {
    g_last_error = std::move(operation) + " failed (Win32 " + std::to_string(GetLastError()) + ")";
}

bool save_mcp_value(const wchar_t* key, bool enabled, std::atomic<bool>& destination,
                    std::string_view operation) {
    g_last_error.clear();
    const std::filesystem::path path = settings_path();
    if (path.empty()) {
        g_last_error = "Could not resolve the Explorer DLL directory";
        return false;
    }
    if (!WritePrivateProfileStringW(L"MCP", key, enabled ? L"1" : L"0", path.c_str())) {
        set_win32_error(std::string(operation));
        return false;
    }
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, path.c_str());
    destination.store(enabled, std::memory_order_release);
    return true;
}

} // namespace

bool load() {
    g_last_error.clear();
    const std::filesystem::path path = settings_path();
    if (path.empty()) {
        g_last_error = "Could not resolve the Explorer DLL directory";
        return false;
    }

    const int stored = GetPrivateProfileIntW(L"Explorer", L"ToggleKey", menu_toggle_key, path.c_str());
    if (!valid_virtual_key(stored)) {
        g_last_error = "ToggleKey in URK_Explorer.ini is outside the Win32 virtual-key range";
        return false;
    }
    menu_toggle_key = stored;
    g_ui_scale = read_float(L"UiScale", 1.0f, 0.75f, 2.0f, path);
    g_panel_opacity = read_float(L"PanelOpacity", 1.0f, 0.35f, 1.0f, path);
    enable_mcp.store(GetPrivateProfileIntW(L"MCP", L"Enabled", 1, path.c_str()) == 1);
    enable_mcp_auto_discovery.store(GetPrivateProfileIntW(L"MCP", L"AutoDiscovery", 1, path.c_str()) == 1);
    enable_mcp_property_access.store(GetPrivateProfileIntW(L"MCP", L"AllowProperties", 1, path.c_str()) == 1);
    enable_mcp_writes.store(GetPrivateProfileIntW(L"MCP", L"AllowWrites", 1, path.c_str()) == 1);
    enable_mcp_tracing.store(GetPrivateProfileIntW(L"MCP", L"AllowTracing", 1, path.c_str()) == 1);
    enable_mcp_invocation.store(GetPrivateProfileIntW(L"MCP", L"AllowInvocation", 1, path.c_str()) == 1);
    enable_mcp_destructive_operations.store(
        GetPrivateProfileIntW(L"MCP", L"AllowDestructiveOperations", 1, path.c_str()) == 1);
    mcp_requests_per_second.store(read_mcp_int(L"RatePerSecond", 20, 1, 200, path));
    mcp_burst.store(read_mcp_int(L"Burst", 60, 1, 500, path));
    mcp_timeout_ms.store(read_mcp_int(L"TimeoutMs", 8000, 1000, 30000, path));
    return true;
}

bool save_toggle_key(int virtual_key) {
    g_last_error.clear();
    if (!valid_virtual_key(virtual_key)) {
        g_last_error = "The selected key is not a valid Win32 virtual-key code";
        return false;
    }
    const std::filesystem::path path = settings_path();
    if (path.empty()) {
        g_last_error = "Could not resolve the Explorer DLL directory";
        return false;
    }

    wchar_t value[16]{};
    _snwprintf_s(value, _TRUNCATE, L"%d", virtual_key);
    if (!WritePrivateProfileStringW(L"Explorer", L"ToggleKey", value, path.c_str())) {
        set_win32_error("Writing URK_Explorer.ini");
        return false;
    }
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, path.c_str());
    menu_toggle_key = virtual_key;
    return true;
}

bool save_mcp_enabled(bool enabled) {
    return save_mcp_value(L"Enabled", enabled, enable_mcp, "Writing MCP enabled state");
}

bool save_mcp_auto_discovery(bool enabled) {
    return save_mcp_value(L"AutoDiscovery", enabled, enable_mcp_auto_discovery,
                          "Writing MCP auto-discovery permission");
}

bool save_mcp_property_access(bool enabled) {
    return save_mcp_value(L"AllowProperties", enabled, enable_mcp_property_access,
                          "Writing MCP property access permission");
}

bool save_mcp_writes(bool enabled) {
    return save_mcp_value(L"AllowWrites", enabled, enable_mcp_writes,
                          "Writing MCP write permission");
}

bool save_mcp_tracing(bool enabled) {
    return save_mcp_value(L"AllowTracing", enabled, enable_mcp_tracing,
                          "Writing MCP tracing permission");
}

bool save_mcp_invocation(bool enabled) {
    return save_mcp_value(L"AllowInvocation", enabled, enable_mcp_invocation,
                          "Writing MCP invocation permission");
}

bool save_mcp_destructive_operations(bool enabled) {
    return save_mcp_value(L"AllowDestructiveOperations", enabled,
                          enable_mcp_destructive_operations,
                          "Writing MCP destructive-operation permission");
}

bool save_mcp_full_access(bool enabled) {
    return save_mcp_enabled(enabled) && save_mcp_auto_discovery(enabled) &&
        save_mcp_property_access(enabled) && save_mcp_writes(enabled) &&
        save_mcp_tracing(enabled) && save_mcp_invocation(enabled) &&
        save_mcp_destructive_operations(enabled);
}

bool save_mcp_pacing(int requests_per_second, int burst, int timeout_ms) {
    g_last_error.clear();
    requests_per_second = std::clamp(requests_per_second, 1, 200);
    burst = std::clamp(burst, 1, 500);
    timeout_ms = std::clamp(timeout_ms, 1000, 30000);
    const std::filesystem::path path = settings_path();
    if (path.empty()) {
        g_last_error = "Could not resolve the Explorer DLL directory";
        return false;
    }
    const std::pair<const wchar_t*, int> entries[] = {
        {L"RatePerSecond", requests_per_second},
        {L"Burst", burst},
        {L"TimeoutMs", timeout_ms},
    };
    for (const auto& [key, value] : entries) {
        const std::wstring text = std::to_wstring(value);
        if (!WritePrivateProfileStringW(L"MCP", key, text.c_str(), path.c_str())) {
            set_win32_error("Writing MCP pacing settings");
            return false;
        }
    }
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, path.c_str());
    mcp_requests_per_second.store(requests_per_second, std::memory_order_release);
    mcp_burst.store(burst, std::memory_order_release);
    mcp_timeout_ms.store(timeout_ms, std::memory_order_release);
    return true;
}

void begin_toggle_key_capture() {
    for (int key = 0x08; key <= 0xfe; ++key)
        g_capture_key_down[static_cast<std::size_t>(key)] =
            !ignored_capture_key(key) && (GetAsyncKeyState(key) & 0x8000) != 0;
    g_capture_active = true;
}

int poll_toggle_key_capture() {
    if (!g_capture_active)
        return 0;

    int pressed = 0;
    for (int key = 0x08; key <= 0xfe; ++key) {
        if (ignored_capture_key(key))
            continue;
        const bool down = (GetAsyncKeyState(key) & 0x8000) != 0;
        const std::size_t index = static_cast<std::size_t>(key);
        if (pressed == 0 && down && !g_capture_key_down[index])
            pressed = key;
        g_capture_key_down[index] = down;
    }
    return pressed;
}

void end_toggle_key_capture() {
    g_capture_active = false;
    g_capture_key_down.fill(false);
}

std::string virtual_key_name(int virtual_key) {
    UINT scan_code = MapVirtualKeyW(static_cast<UINT>(virtual_key), MAPVK_VK_TO_VSC);
    if (virtual_key == VK_LEFT || virtual_key == VK_UP || virtual_key == VK_RIGHT || virtual_key == VK_DOWN ||
        virtual_key == VK_PRIOR || virtual_key == VK_NEXT || virtual_key == VK_END || virtual_key == VK_HOME ||
        virtual_key == VK_INSERT || virtual_key == VK_DELETE || virtual_key == VK_DIVIDE || virtual_key == VK_NUMLOCK)
        scan_code |= 0x100;

    char name[64]{};
    if (GetKeyNameTextA(static_cast<LONG>(scan_code << 16), name, static_cast<int>(sizeof(name))) > 0) {
        char code[8]{};
        std::snprintf(code, sizeof(code), "%02X", virtual_key);
        return std::string(name) + " (0x" + code + ")";
    }

    char fallback[16]{};
    std::snprintf(fallback, sizeof(fallback), "VK 0x%02X", virtual_key);
    return fallback;
}

float ui_scale() {
    return g_ui_scale;
}

float panel_opacity() {
    return g_panel_opacity;
}

bool save_appearance(float ui_scale, float panel_opacity) {
    g_last_error.clear();
    g_ui_scale = std::clamp(ui_scale, 0.75f, 2.0f);
    g_panel_opacity = std::clamp(panel_opacity, 0.35f, 1.0f);
    const std::filesystem::path path = settings_path();
    if (path.empty()) {
        g_last_error = "Could not resolve the Explorer DLL directory";
        return false;
    }
    wchar_t scale[16]{};
    wchar_t opacity[16]{};
    _snwprintf_s(scale, _TRUNCATE, L"%.2f", g_ui_scale);
    _snwprintf_s(opacity, _TRUNCATE, L"%.2f", g_panel_opacity);
    if (!WritePrivateProfileStringW(L"Explorer", L"UiScale", scale, path.c_str()) ||
        !WritePrivateProfileStringW(L"Explorer", L"PanelOpacity", opacity, path.c_str())) {
        set_win32_error("Writing URK_Explorer.ini");
        return false;
    }
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, path.c_str());
    return true;
}

const std::string& last_error() {
    return g_last_error;
}


} // namespace ModConfig::UserSettings

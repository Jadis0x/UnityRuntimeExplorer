// Copyright (c) 2026 Jadis0x. All rights reserved.
#include "user_settings.h"

#include "mod_config.h"

#include <Windows.h>

#include <array>
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
    enable_mcp.store(GetPrivateProfileIntW(L"MCP", L"Enabled", 1, path.c_str()) == 1);
    enable_mcp_auto_discovery.store(GetPrivateProfileIntW(L"MCP", L"AutoDiscovery", 1, path.c_str()) == 1);
    enable_mcp_property_access.store(GetPrivateProfileIntW(L"MCP", L"AllowProperties", 1, path.c_str()) == 1);
    enable_mcp_writes.store(GetPrivateProfileIntW(L"MCP", L"AllowWrites", 1, path.c_str()) == 1);
    enable_mcp_tracing.store(GetPrivateProfileIntW(L"MCP", L"AllowTracing", 1, path.c_str()) == 1);
    enable_mcp_invocation.store(GetPrivateProfileIntW(L"MCP", L"AllowInvocation", 1, path.c_str()) == 1);
    enable_mcp_destructive_operations.store(
        GetPrivateProfileIntW(L"MCP", L"AllowDestructiveOperations", 1, path.c_str()) == 1);
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

const std::string& last_error() {
    return g_last_error;
}


} // namespace ModConfig::UserSettings

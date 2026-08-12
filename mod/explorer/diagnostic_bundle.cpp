// Copyright (c) 2026 Jadis0x. All rights reserved.
#include "diagnostic_bundle.h"

#include "explorer_types.h"
#include "config/mod_config.h"
#include "sdk/runtime_api.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>

namespace Explorer::DiagnosticBundle {
namespace {
int g_module_anchor = 0;

std::filesystem::path module_directory() {
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&g_module_anchor), &module))
        return {};
    std::array<wchar_t, 32768> path{};
    const DWORD length = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size())
        return {};
    return std::filesystem::path(path.data(), path.data() + length).parent_path();
}

std::string capability_list(std::uint64_t capabilities) {
    struct Entry { std::uint64_t flag; const char* name; };
    constexpr Entry entries[] = {
        {URK::runtime_cap_mono_api, "mono-api"},
        {URK::runtime_cap_il2cpp_api, "il2cpp-api"},
        {URK::runtime_cap_hooks, "hooks"},
        {URK::runtime_cap_main_thread, "main-thread"},
        {URK::runtime_cap_scene_events, "scene-events"},
        {URK::runtime_cap_object_destroy_request_events, "destroy-request-events"},
        {URK::runtime_cap_cursor_control, "cursor-control"},
        {URK::runtime_cap_network, "network"},
        {URK::runtime_cap_input, "input"},
        {URK::runtime_cap_graphics_device, "graphics-device"},
        {URK::runtime_cap_steam_identity, "steam-identity"},
    };
    std::string result;
    for (const Entry& entry : entries) {
        if ((capabilities & entry.flag) == 0)
            continue;
        if (!result.empty())
            result += ", ";
        result += entry.name;
    }
    return result.empty() ? "none" : result;
}
} // namespace

Result write(const Snapshot& snapshot) {
    Result result{};
    const std::filesystem::path base = module_directory();
    if (base.empty()) {
        result.error = "Explorer DLL directory could not be resolved";
        return result;
    }
    std::error_code error;
    const std::filesystem::path directory = base / L"URK_Diagnostics";
    std::filesystem::create_directories(directory, error);
    if (error) {
        result.error = "Diagnostic directory could not be created: " + error.message();
        return result;
    }

    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
    localtime_s(&local, &time);
    std::ostringstream filename;
    filename << "URK_Diagnostic_" << std::put_time(&local, "%Y%m%d_%H%M%S") << ".txt";
    const std::filesystem::path path = directory / filename.str();
    std::ofstream output(path, std::ios::out | std::ios::trunc);
    if (!output) {
        result.error = "Diagnostic file could not be opened for writing";
        return result;
    }

    output << "URK Unity Runtime Explorer diagnostic bundle\n"
           << "Generated: " << std::put_time(&local, "%Y-%m-%d %H:%M:%S") << "\n"
           << "Explorer: " << ModConfig::display_name << " v" << ModConfig::version << "\n"
           << "Backend: " << snapshot.runtime_backend << "\n"
           << "Unity: " << (snapshot.unity_version.empty() ? "unavailable" : snapshot.unity_version) << "\n"
           << "Capabilities (0x" << std::hex << std::uppercase << snapshot.runtime_capabilities << std::dec
           << "): " << capability_list(snapshot.runtime_capabilities) << "\n"
           << "Status: " << snapshot.status << "\n"
           << "GC: " << snapshot.managed_used_bytes << " used / " << snapshot.managed_heap_bytes << " heap bytes\n"
           << "Handles: " << snapshot.strong_handle_count << " strong, " << snapshot.weak_handle_count
           << " weak, " << snapshot.quarantined_handle_count << " quarantined\n\n"
           << "Recent errors (" << snapshot.diagnostics.size() << ")\n";
    for (const std::string& diagnostic : snapshot.diagnostics)
        output << "- " << diagnostic << "\n";
    output << "\nFlight recorder (" << snapshot.flight_recorder.size() << ")\n";
    for (const Snapshot::FlightEvent& event : snapshot.flight_recorder)
        output << "- #" << event.sequence << " +" << std::fixed << std::setprecision(3)
               << event.seconds_since_start << "s [" << event.stage << "] " << event.operation
               << (event.detail.empty() ? "" : " | ") << event.detail << "\n";
    output.flush();
    if (!output) {
        result.error = "Diagnostic file write did not complete";
        return result;
    }
    result.succeeded = true;
    result.path = path.string();
    return result;
}
} // namespace Explorer::DiagnosticBundle

// Copyright (c) 2026 Jadis0x. All rights reserved.
#include "mcp_bridge.h"

#include "explorer/explorer_model.h"
#include "config/mod_config.h"
#include "mcp/core/bridge_protocol.h"
#include "mcp_runtime_tools.h"
#include "support/mod_log.h"

#include <Windows.h>
#include <sddl.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <deque>
#include <filesystem>
#include <fstream>
#include <future>
#include <memory>
#include <mutex>
#include <thread>

namespace Explorer::Mcp::Bridge {
namespace {
using Clock = std::chrono::steady_clock;

CapabilityMask current_capabilities() {
    if (!ModConfig::enable_mcp.load(std::memory_order_acquire))
        return 0;
    CapabilityMask result = capability_bit(Capability::Read);
    if (ModConfig::enable_mcp_auto_discovery.load(std::memory_order_acquire))
        result |= capability_bit(Capability::AutoDiscovery);
    if (ModConfig::enable_mcp_property_access.load(std::memory_order_acquire))
        result |= capability_bit(Capability::PropertyAccess);
    if (ModConfig::enable_mcp_writes.load(std::memory_order_acquire))
        result |= capability_bit(Capability::Write);
    if (ModConfig::enable_mcp_tracing.load(std::memory_order_acquire))
        result |= capability_bit(Capability::Trace);
    if (ModConfig::enable_mcp_invocation.load(std::memory_order_acquire))
        result |= capability_bit(Capability::Invoke);
    if (ModConfig::enable_mcp_destructive_operations.load(std::memory_order_acquire))
        result |= capability_bit(Capability::Destructive);
    return result;
}

struct PendingRequest {
    Request request;
    std::promise<Response> completion;
    std::atomic<bool> cancelled{false};
};

struct State {
    std::atomic<bool> stopping{false};
    std::atomic<bool> running{false};
    std::atomic<bool> revoke_instrumentation{false};
    std::mutex mutex;
    std::deque<std::shared_ptr<PendingRequest>> pending;
    std::thread server_thread;
    std::wstring pipe_name;
    std::filesystem::path discovery_path;
    RuntimeTools tools;
};

State g_state;

std::filesystem::path discovery_directory() {
    std::array<wchar_t, 32768> value{};
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", value.data(), static_cast<DWORD>(value.size()));
    if (length == 0 || length >= value.size())
        return {};
    return std::filesystem::path(value.data(), value.data() + length) / L"URK" /
           L"UnityRuntimeExplorer" / L"bridges";
}

bool write_discovery(std::string& error) {
    const std::filesystem::path directory = discovery_directory();
    if (directory.empty()) {
        error = "LOCALAPPDATA could not be resolved for MCP bridge discovery";
        return false;
    }
    std::error_code filesystem_error;
    std::filesystem::create_directories(directory, filesystem_error);
    if (filesystem_error) {
        error = "MCP discovery directory could not be created: " + filesystem_error.message();
        return false;
    }
    g_state.discovery_path = directory / (std::to_wstring(GetCurrentProcessId()) + L".json");
    std::ofstream output(g_state.discovery_path, std::ios::out | std::ios::trunc);
    if (!output) {
        error = "MCP discovery file could not be opened";
        return false;
    }
    const std::string pipe(g_state.pipe_name.begin(), g_state.pipe_name.end());
    output << nlohmann::json{{"protocol", bridge_protocol_version}, {"pid", GetCurrentProcessId()},
                             {"pipe", pipe},
                             {"permission_authority", "explorer_config"},
                             {"capabilities", current_capabilities()}}.dump();
    output.flush();
    if (!output) {
        error = "MCP discovery file write did not complete";
        return false;
    }
    return true;
}

HANDLE create_pipe(std::string& error) {
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            L"D:P(A;;GA;;;SY)(A;;GA;;;OW)", SDDL_REVISION_1, &descriptor, nullptr)) {
        error = "MCP pipe security descriptor failed (Win32 " + std::to_string(GetLastError()) + ")";
        return INVALID_HANDLE_VALUE;
    }
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), descriptor, FALSE};
    const HANDLE pipe = CreateNamedPipeW(
        g_state.pipe_name.c_str(), PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
        1, static_cast<DWORD>(max_message_bytes + 1), static_cast<DWORD>(max_message_bytes + 1),
        5000, &attributes);
    LocalFree(descriptor);
    if (pipe == INVALID_HANDLE_VALUE)
        error = "MCP named pipe creation failed (Win32 " + std::to_string(GetLastError()) + ")";
    return pipe;
}

bool read_line(HANDLE pipe, std::string& line) {
    line.clear();
    std::array<char, 4096> buffer{};
    while (!g_state.stopping.load(std::memory_order_acquire)) {
        DWORD read = 0;
        if (!ReadFile(pipe, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr) || read == 0)
            return false;
        const char* newline = std::find(buffer.data(), buffer.data() + read, '\n');
        const std::size_t count = static_cast<std::size_t>(newline - buffer.data());
        if (line.size() + count > max_message_bytes)
            return false;
        line.append(buffer.data(), count);
        if (newline != buffer.data() + read)
            return true;
    }
    return false;
}

bool write_line(HANDLE pipe, std::string text) {
    text.push_back('\n');
    std::size_t offset = 0;
    while (offset < text.size()) {
        DWORD written = 0;
        if (!WriteFile(pipe, text.data() + offset, static_cast<DWORD>(text.size() - offset), &written, nullptr) ||
            written == 0)
            return false;
        offset += written;
    }
    return true;
}

void serve_client(HANDLE pipe) {
    double tokens = 20.0;
    Clock::time_point last_refill = Clock::now();
    std::string line;
    while (read_line(pipe, line)) {
        const Clock::time_point now = Clock::now();
        tokens = std::min(20.0, tokens + std::chrono::duration<double>(now - last_refill).count() * 5.0);
        last_refill = now;
        Request request;
        std::string parse_error;
        if (!parse_request(line, request, parse_error)) {
            if (!write_line(pipe, serialize(failure("invalid", "invalid_request", parse_error))))
                return;
            continue;
        }
        if (tokens < 1.0) {
            if (!write_line(pipe, serialize(failure(request.id, "rate_limited",
                                                    "MCP bridge rate limit exceeded; retry shortly."))))
                return;
            continue;
        }
        tokens -= 1.0;
        auto pending = std::make_shared<PendingRequest>();
        pending->request = std::move(request);
        std::future<Response> completion = pending->completion.get_future();
        {
            std::lock_guard lock(g_state.mutex);
            if (g_state.pending.size() >= 64) {
                if (!write_line(pipe, serialize(failure(pending->request.id, "busy",
                                                        "The MCP bridge request queue is full."))))
                    return;
                continue;
            }
            g_state.pending.push_back(pending);
        }
        if (completion.wait_for(std::chrono::seconds(5)) != std::future_status::ready) {
            pending->cancelled.store(true, std::memory_order_release);
            if (!write_line(pipe, serialize(failure(pending->request.id, "timeout",
                                                    "The Unity main thread did not answer in time."))))
                return;
            continue;
        }
        std::string response = serialize(completion.get());
        if (response.size() > max_message_bytes)
            response = serialize(failure(pending->request.id, "result_too_large",
                                         "The bounded MCP result exceeded the bridge message limit."));
        if (!write_line(pipe, std::move(response)))
            return;
    }
}

void server_main(HANDLE first_pipe) {
    g_state.running.store(true, std::memory_order_release);
    HANDLE pipe = first_pipe;
    while (!g_state.stopping.load(std::memory_order_acquire)) {
        const BOOL connected = ConnectNamedPipe(pipe, nullptr) ? TRUE : GetLastError() == ERROR_PIPE_CONNECTED;
        if (connected && !g_state.stopping.load(std::memory_order_acquire)) {
            serve_client(pipe);
            g_state.revoke_instrumentation.store(true, std::memory_order_release);
        }
        FlushFileBuffers(pipe);
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
        if (g_state.stopping.load(std::memory_order_acquire))
            break;
        std::string error;
        pipe = create_pipe(error);
        if (pipe == INVALID_HANDLE_VALUE) {
            ModLog::error("MCP bridge listener failed: %s", error.c_str());
            break;
        }
    }
    g_state.running.store(false, std::memory_order_release);
}
} // namespace

bool start(std::string& error) {
    error.clear();
    if (g_state.server_thread.joinable())
        return true;
    g_state.stopping.store(false, std::memory_order_release);
    g_state.pipe_name = L"\\\\.\\pipe\\URK.UnityRuntimeExplorer." + std::to_wstring(GetCurrentProcessId());
    const HANDLE first_pipe = create_pipe(error);
    if (first_pipe == INVALID_HANDLE_VALUE)
        return false;
    if (!write_discovery(error))
    {
        CloseHandle(first_pipe);
        return false;
    }
    try {
        g_state.server_thread = std::thread(server_main, first_pipe);
    } catch (const std::system_error& exception) {
        CloseHandle(first_pipe);
        error = "MCP bridge thread could not start: " + std::string(exception.what());
        std::error_code ignored;
        std::filesystem::remove(g_state.discovery_path, ignored);
        return false;
    }
    return true;
}

void tick(RuntimeModel& model) {
    if (g_state.revoke_instrumentation.exchange(false, std::memory_order_acq_rel))
        g_state.tools.revoke_instrumentation(model, "MCP helper disconnected");
    constexpr std::size_t kMaxRequestsPerFrame = 4;
    for (std::size_t index = 0; index < kMaxRequestsPerFrame; ++index) {
        std::shared_ptr<PendingRequest> pending;
        {
            std::lock_guard lock(g_state.mutex);
            if (g_state.pending.empty())
                break;
            pending = std::move(g_state.pending.front());
            g_state.pending.pop_front();
        }
        if (pending->cancelled.load(std::memory_order_acquire))
            continue;
        try {
            pending->request.context.capabilities = current_capabilities();
            pending->completion.set_value(g_state.tools.execute(model, pending->request));
        } catch (const std::exception&) {
            pending->completion.set_value(failure(pending->request.id, "internal_error",
                "The MCP request failed inside the Explorer runtime adapter."));
        }
    }
}

void stop() {
    g_state.stopping.store(true, std::memory_order_release);
    if (g_state.server_thread.joinable()) {
        CancelSynchronousIo(g_state.server_thread.native_handle());
        g_state.server_thread.join();
    }
    std::deque<std::shared_ptr<PendingRequest>> pending;
    {
        std::lock_guard lock(g_state.mutex);
        pending.swap(g_state.pending);
    }
    for (const auto& request : pending)
        request->completion.set_value(failure(request->request.id, "bridge_stopped", "The MCP bridge stopped."));
    g_state.tools.reset();
    std::error_code ignored;
    if (!g_state.discovery_path.empty())
        std::filesystem::remove(g_state.discovery_path, ignored);
    g_state.discovery_path.clear();
}

bool running() {
    return g_state.running.load(std::memory_order_acquire);
}

} // namespace Explorer::Mcp::Bridge

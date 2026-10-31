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
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <deque>
#include <filesystem>
#include <fstream>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace Explorer::Mcp::Bridge {
namespace {
using Clock = std::chrono::steady_clock;

// Grace period before an unattended trace hook is detached.
constexpr std::int64_t kRevokeGraceMs = 30000;

std::int64_t now_ms() {
    const auto since_start = Clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(since_start).count();
}

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

// A served client; joined once finished.
struct ClientThread {
    std::thread thread;
    std::atomic<bool> finished{false};
};

// One extra pipe instance waits for the next client.
constexpr int kMaxClients = 4;

struct State {
    std::atomic<bool> stopping{false};
    std::atomic<bool> running{false};
    // Revocation waits out the grace period after a client leaves.
    std::atomic<bool> revoke_instrumentation{false};
    std::atomic<int> active_clients{0};
    std::mutex clients_mutex;
    std::vector<std::unique_ptr<ClientThread>> clients;
    std::atomic<std::int64_t> disconnected_at_ms{0};
    // Signalled once by stop() so every blocking wait on the server thread
    // unblocks. CancelSynchronousIo cannot reach a thread parked in
    // ConnectNamedPipe, which used to leave stop() joining forever.
    HANDLE stop_event = nullptr;
    std::mutex mutex;
    std::deque<std::shared_ptr<PendingRequest>> pending;
    std::thread server_thread;
    std::wstring pipe_name;
    std::filesystem::path discovery_path;
    std::atomic<CapabilityMask> published_capabilities{0};
    RuntimeTools tools;
};

State g_state;

// One client connection: the pipe, the event its overlapped I/O signals, and
// the bytes a read delivered past the newline it completed.
struct Connection {
    HANDLE pipe = INVALID_HANDLE_VALUE;
    HANDLE io_event = nullptr;
    std::string carry;
    bool served_request = false;
};

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
    const CapabilityMask capabilities = current_capabilities();
    // Publish through a temporary so a helper never reads a half-written
    // document while Explorer Config is being toggled.
    const std::filesystem::path staging = g_state.discovery_path.wstring() + L".tmp";
    {
        std::ofstream output(staging, std::ios::out | std::ios::trunc);
        if (!output) {
            error = "MCP discovery file could not be opened";
            return false;
        }
        const std::string pipe(g_state.pipe_name.begin(), g_state.pipe_name.end());
        output << nlohmann::json{{"protocol", bridge_protocol_version}, {"pid", GetCurrentProcessId()},
                                 {"pipe", pipe},
                                 {"permission_authority", "explorer_config"},
                                 {"capabilities", capabilities}}.dump();
        output.flush();
        if (!output) {
            error = "MCP discovery file write did not complete";
            return false;
        }
    }
    std::filesystem::rename(staging, g_state.discovery_path, filesystem_error);
    if (filesystem_error) {
        std::error_code ignored;
        std::filesystem::remove(staging, ignored);
        error = "MCP discovery file could not be published: " + filesystem_error.message();
        return false;
    }
    g_state.published_capabilities.store(capabilities, std::memory_order_release);
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
    const DWORD buffer_bytes = static_cast<DWORD>(max_message_bytes + 1);
    const HANDLE pipe = CreateNamedPipeW(
        g_state.pipe_name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
        kMaxClients + 1, buffer_bytes, buffer_bytes, 5000, &attributes);
    LocalFree(descriptor);
    if (pipe == INVALID_HANDLE_VALUE)
        error = "MCP named pipe creation failed (Win32 " + std::to_string(GetLastError()) + ")";
    return pipe;
}

// Parks until the overlapped operation finishes or stop() signals. A cancelled
// operation is always drained so the OVERLAPPED can safely leave scope.
bool await_overlapped(HANDLE pipe, OVERLAPPED& overlapped, DWORD& transferred) {
    const HANDLE waits[2] = {overlapped.hEvent, g_state.stop_event};
    if (WaitForMultipleObjects(2, waits, FALSE, INFINITE) != WAIT_OBJECT_0) {
        CancelIoEx(pipe, &overlapped);
        DWORD discarded = 0;
        GetOverlappedResult(pipe, &overlapped, &discarded, TRUE);
        return false;
    }
    return GetOverlappedResult(pipe, &overlapped, &transferred, FALSE) != FALSE;
}

bool await_connection(Connection& connection) {
    OVERLAPPED overlapped{};
    overlapped.hEvent = connection.io_event;
    ResetEvent(connection.io_event);
    if (ConnectNamedPipe(connection.pipe, &overlapped))
        return true;
    const DWORD status = GetLastError();
    if (status == ERROR_PIPE_CONNECTED)
        return true;
    if (status != ERROR_IO_PENDING)
        return false;
    DWORD transferred = 0;
    return await_overlapped(connection.pipe, overlapped, transferred);
}

bool read_some(Connection& connection, char* buffer, DWORD capacity, DWORD& read) {
    OVERLAPPED overlapped{};
    overlapped.hEvent = connection.io_event;
    ResetEvent(connection.io_event);
    read = 0;
    if (ReadFile(connection.pipe, buffer, capacity, &read, &overlapped))
        return read != 0;
    if (GetLastError() != ERROR_IO_PENDING)
        return false;
    DWORD transferred = 0;
    if (!await_overlapped(connection.pipe, overlapped, transferred))
        return false;
    read = transferred;
    return read != 0;
}

enum class ReadStatus { Line, Oversize, Closed };

// Newline framing over a byte-mode pipe. Bytes a read delivered past the
// newline stay in the carry buffer instead of being discarded, so a client that
// pipelines requests no longer loses every message but the first.
ReadStatus read_line(Connection& connection, std::string& line) {
    line.clear();
    bool discarding = false;
    for (;;) {
        if (const std::size_t newline = connection.carry.find('\n'); newline != std::string::npos) {
            if (!discarding)
                line.assign(connection.carry, 0, newline);
            connection.carry.erase(0, newline + 1);
            return discarding ? ReadStatus::Oversize : ReadStatus::Line;
        }
        if (connection.carry.size() > max_message_bytes) {
            // Drop the oversized request but keep the connection, so the client
            // still receives an answer for it and can carry on.
            discarding = true;
            connection.carry.clear();
        }
        std::array<char, 4096> buffer{};
        DWORD read = 0;
        if (!read_some(connection, buffer.data(), static_cast<DWORD>(buffer.size()), read))
            return ReadStatus::Closed;
        connection.carry.append(buffer.data(), read);
    }
}

bool write_line(Connection& connection, std::string text) {
    text.push_back('\n');
    std::size_t offset = 0;
    while (offset < text.size()) {
        OVERLAPPED overlapped{};
        overlapped.hEvent = connection.io_event;
        ResetEvent(connection.io_event);
        DWORD written = 0;
        const DWORD remaining = static_cast<DWORD>(text.size() - offset);
        if (!WriteFile(connection.pipe, text.data() + offset, remaining, &written, &overlapped)) {
            if (GetLastError() != ERROR_IO_PENDING)
                return false;
            DWORD transferred = 0;
            if (!await_overlapped(connection.pipe, overlapped, transferred))
                return false;
            written = transferred;
        }
        if (written == 0)
            return false;
        offset += written;
    }
    return true;
}

// Relative cost of a request: reads are cheap, scans are heavy.
double request_cost(std::string_view tool) {
    static constexpr std::array<std::string_view, 15> cheap = {
        "runtime_status", "list_scenes", "browse_hierarchy", "get_selected_object", "describe_reference",
        "list_labels", "get_activity_log", "list_method_traces", "get_method_trace", "get_watch_history",
        "get_instance_scan", "hierarchy_search", "list_components", "inspect_game_object", "read_member"};
    static constexpr std::array<std::string_view, 11> heavy = {
        "start_instance_scan", "search_types", "search_members", "discover_runtime", "find_game_objects",
        "build_reference_graph", "build_call_graph", "export_diagnostic_bundle", "invoke_method",
        "get_type_hierarchy", "list_assemblies"};
    if (std::find(cheap.begin(), cheap.end(), tool) != cheap.end())
        return 0.25;
    if (std::find(heavy.begin(), heavy.end(), tool) != heavy.end())
        return 3.0;
    return 1.0;
}

// Longest a request waits for budget before it is refused.
constexpr double kMaxBudgetWaitSeconds = 2.0;

void serve_client(Connection& connection) {
    double tokens = static_cast<double>(ModConfig::mcp_burst.load(std::memory_order_acquire));
    Clock::time_point last_refill = Clock::now();
    std::string line;
    for (;;) {
        const ReadStatus status = read_line(connection, line);
        if (status == ReadStatus::Closed)
            return;
        if (status == ReadStatus::Oversize) {
            if (!write_line(connection, serialize(failure("invalid", "request_too_large",
                                                          "The MCP request exceeded the bridge message limit."))))
                return;
            continue;
        }
        Request request;
        std::string parse_error;
        if (!parse_request(line, request, parse_error)) {
            if (!write_line(connection, serialize(failure("invalid", "invalid_request", parse_error))))
                return;
            continue;
        }
        const int rate_setting = ModConfig::mcp_requests_per_second.load(std::memory_order_acquire);
        const int burst_setting = ModConfig::mcp_burst.load(std::memory_order_acquire);
        const double rate = static_cast<double>(rate_setting);
        const double burst = static_cast<double>(burst_setting);
        // Cost is capped at the bucket size.
        const double cost = std::min(request_cost(request.tool), burst);
        const auto refill = [&] {
            const Clock::time_point now = Clock::now();
            const double elapsed = std::chrono::duration<double>(now - last_refill).count();
            tokens = std::min(burst, tokens + elapsed * rate);
            last_refill = now;
        };
        refill();
        if (tokens < cost) {
            // Wait for a refill; refuse only when the wait is too long.
            const double wait_seconds = (cost - tokens) / rate;
            const double wait_ms = std::ceil(wait_seconds * 1000.0);
            if (wait_seconds > kMaxBudgetWaitSeconds) {
                Response refusal = failure(request.id, "rate_limited",
                                           "Request budget exhausted; retry after the suggested delay.");
                refusal.retry_after_ms = static_cast<std::uint32_t>(wait_ms);
                if (!write_line(connection, serialize(refusal)))
                    return;
                continue;
            }
            const DWORD woke = WaitForSingleObject(g_state.stop_event, static_cast<DWORD>(wait_ms));
            if (woke == WAIT_OBJECT_0)
                return;
            refill();
        }
        tokens -= cost;
        connection.served_request = true;
        auto pending = std::make_shared<PendingRequest>();
        pending->request = std::move(request);
        std::future<Response> completion = pending->completion.get_future();
        {
            std::lock_guard lock(g_state.mutex);
            if (g_state.pending.size() >= 64) {
                Response busy = failure(pending->request.id, "busy", "Request queue is full.");
                busy.retry_after_ms = 250;
                if (!write_line(connection, serialize(busy)))
                    return;
                continue;
            }
            g_state.pending.push_back(pending);
        }
        // Short wait slices keep stop() responsive.
        const int timeout_ms = ModConfig::mcp_timeout_ms.load(std::memory_order_acquire);
        const Clock::time_point deadline = Clock::now() + std::chrono::milliseconds(timeout_ms);
        std::future_status completed = std::future_status::timeout;
        while (completed != std::future_status::ready && Clock::now() < deadline &&
               !g_state.stopping.load(std::memory_order_acquire))
            completed = completion.wait_for(std::chrono::milliseconds(100));
        if (completed != std::future_status::ready) {
            pending->cancelled.store(true, std::memory_order_release);
            if (g_state.stopping.load(std::memory_order_acquire))
                return;
            if (!write_line(connection, serialize(failure(pending->request.id, "timeout",
                                                          "The Unity main thread did not answer in time."))))
                return;
            continue;
        }
        std::string response = serialize(completion.get());
        if (response.size() > max_message_bytes)
            response = serialize(failure(pending->request.id, "result_too_large",
                                         "The bounded MCP result exceeded the bridge message limit."));
        if (!write_line(connection, std::move(response)))
            return;
    }
}

void close_connection(Connection& connection) {
    if (connection.pipe != INVALID_HANDLE_VALUE) {
        FlushFileBuffers(connection.pipe);
        DisconnectNamedPipe(connection.pipe);
        CloseHandle(connection.pipe);
        connection.pipe = INVALID_HANDLE_VALUE;
    }
    if (connection.io_event) {
        CloseHandle(connection.io_event);
        connection.io_event = nullptr;
    }
}

void run_client(Connection& connection) {
    serve_client(connection);
    // Only clients that sent requests can trigger trace revocation.
    if (connection.served_request)
        g_state.revoke_instrumentation.store(true, std::memory_order_release);
    close_connection(connection);
    g_state.disconnected_at_ms.store(now_ms(), std::memory_order_release);
    g_state.active_clients.fetch_sub(1, std::memory_order_acq_rel);
}

void reap_finished_clients() {
    std::vector<std::unique_ptr<ClientThread>> finished;
    {
        std::lock_guard lock(g_state.clients_mutex);
        for (auto it = g_state.clients.begin(); it != g_state.clients.end();) {
            if ((*it)->finished.load(std::memory_order_acquire)) {
                finished.push_back(std::move(*it));
                it = g_state.clients.erase(it);
            } else {
                ++it;
            }
        }
    }
    for (auto& client : finished)
        if (client->thread.joinable())
            client->thread.join();
}

void listener_main(HANDLE first_pipe) {
    g_state.running.store(true, std::memory_order_release);
    HANDLE pipe = first_pipe;
    while (!g_state.stopping.load(std::memory_order_acquire)) {
        auto connection = std::make_unique<Connection>();
        connection->pipe = pipe;
        connection->io_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!connection->io_event) {
            ModLog::error("MCP bridge I/O event could not be created (Win32 %lu)", GetLastError());
            close_connection(*connection);
            break;
        }
        bool connected = await_connection(*connection);
        if (g_state.stopping.load(std::memory_order_acquire))
            connected = false;
        reap_finished_clients();
        const bool full = g_state.active_clients.load(std::memory_order_acquire) >= kMaxClients;
        if (connected && full) {
            const Response busy = failure("invalid", "bridge_busy", "Too many MCP clients are connected.");
            write_line(*connection, serialize(busy));
            close_connection(*connection);
        } else if (connected) {
            auto client = std::make_unique<ClientThread>();
            ClientThread* slot = client.get();
            g_state.active_clients.fetch_add(1, std::memory_order_acq_rel);
            try {
                client->thread = std::thread([slot, owned = std::move(connection)]() mutable {
                    run_client(*owned);
                    slot->finished.store(true, std::memory_order_release);
                });
                std::lock_guard lock(g_state.clients_mutex);
                g_state.clients.push_back(std::move(client));
            } catch (const std::system_error& exception) {
                g_state.active_clients.fetch_sub(1, std::memory_order_acq_rel);
                ModLog::error("MCP client thread could not start: %s", exception.what());
            }
        } else {
            close_connection(*connection);
        }
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
    g_state.stop_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!g_state.stop_event) {
        error = "MCP bridge stop event could not be created (Win32 " + std::to_string(GetLastError()) + ")";
        return false;
    }
    const HANDLE first_pipe = create_pipe(error);
    if (first_pipe == INVALID_HANDLE_VALUE) {
        CloseHandle(g_state.stop_event);
        g_state.stop_event = nullptr;
        return false;
    }
    if (!write_discovery(error)) {
        CloseHandle(first_pipe);
        CloseHandle(g_state.stop_event);
        g_state.stop_event = nullptr;
        return false;
    }
    try {
        g_state.server_thread = std::thread(listener_main, first_pipe);
    } catch (const std::system_error& exception) {
        CloseHandle(first_pipe);
        CloseHandle(g_state.stop_event);
        g_state.stop_event = nullptr;
        error = "MCP bridge thread could not start: " + std::string(exception.what());
        std::error_code ignored;
        std::filesystem::remove(g_state.discovery_path, ignored);
        return false;
    }
    return true;
}

void tick(RuntimeModel& model) {
    if (g_state.revoke_instrumentation.load(std::memory_order_acquire)) {
        const std::int64_t away_ms = now_ms() - g_state.disconnected_at_ms.load(std::memory_order_acquire);
        if (g_state.active_clients.load(std::memory_order_acquire) > 0) {
            g_state.revoke_instrumentation.store(false, std::memory_order_release);
        } else if (away_ms >= kRevokeGraceMs) {
            if (g_state.revoke_instrumentation.exchange(false, std::memory_order_acq_rel))
                g_state.tools.revoke_instrumentation(model, "MCP helper disconnected");
        }
    }
    // Explorer Config is the permission authority, so a toggle has to reach the
    // discovery document a helper reads before it advertises any tool.
    if (!g_state.discovery_path.empty() &&
        g_state.published_capabilities.load(std::memory_order_acquire) != current_capabilities()) {
        std::string discovery_error;
        if (!write_discovery(discovery_error))
            ModLog::error("MCP discovery refresh failed: %s", discovery_error.c_str());
    }
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
    if (g_state.stop_event)
        SetEvent(g_state.stop_event);
    const auto fail_queued_requests = [] {
        std::deque<std::shared_ptr<PendingRequest>> pending;
        {
            std::lock_guard lock(g_state.mutex);
            pending.swap(g_state.pending);
        }
        for (const auto& request : pending) {
            const Response stopped = failure(request->request.id, "bridge_stopped", "The MCP bridge stopped.");
            request->completion.set_value(stopped);
        }
    };
    // Unblock client threads waiting on the main thread.
    fail_queued_requests();
    if (g_state.server_thread.joinable())
        g_state.server_thread.join();
    std::vector<std::unique_ptr<ClientThread>> clients;
    {
        std::lock_guard lock(g_state.clients_mutex);
        clients.swap(g_state.clients);
    }
    for (auto& client : clients)
        if (client->thread.joinable())
            client->thread.join();
    fail_queued_requests();
    g_state.active_clients.store(0, std::memory_order_release);
    if (g_state.stop_event) {
        CloseHandle(g_state.stop_event);
        g_state.stop_event = nullptr;
    }
    g_state.tools.reset();
    std::error_code ignored;
    if (!g_state.discovery_path.empty())
        std::filesystem::remove(g_state.discovery_path, ignored);
    g_state.discovery_path.clear();
    g_state.published_capabilities.store(0, std::memory_order_release);
}

bool running() {
    return g_state.running.load(std::memory_order_acquire);
}

} // namespace Explorer::Mcp::Bridge

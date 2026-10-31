// Copyright (c) 2026 Jadis0x. All rights reserved.
#include "mcp_stdio_server.h"

#include "bounded_stdio_transport.h"
#include "mcp/core/result_compaction.h"
#include "mcp/core/tool_catalog.h"
#include "mcp/core/schema_validator.h"
#include "project_version.h"
#include "request_dispatcher.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <functional>
#include <iostream>
#include <string>
#include <thread>

namespace Explorer::Mcp {
namespace {
using Json = nlohmann::json;

constexpr std::uint64_t kDefaultTaskTtlMs = 300000;
constexpr std::size_t kMaximumStdioOutputBytes = 128 * 1024;
constexpr int kMaximumAutoRetries = 2;
constexpr std::uint32_t kMaximumAutoRetryMs = 4000;
} // namespace

bool StdioServer::ensure_connected(std::string& error) {
    if (bridge_.game_pid() != 0)
        return true;
    if (!bridge_.connect(game_pid_, error))
        return false;
    if (!connection_announced_) {
        std::cerr << "Bridge    : connected to game PID " << bridge_.game_pid() << '\n' << std::flush;
        connection_announced_ = true;
    }
    return true;
}

StdioServer::ToolPermissions StdioServer::permissions() {
    // Explorer Config is the only permission authority; --allow-tracing and
    // --allow-invocation stay deprecated no-ops. Without a bridge the granted
    // set is unknown, so tools/list still answers with the whole catalog.
    if (bridge_.game_pid() == 0)
        return ToolPermissions{};
    const CapabilityMask mask = bridge_.capabilities();
    return ToolPermissions{has_capability(mask, Capability::Trace),
                           has_capability(mask, Capability::Invoke),
                           has_capability(mask, Capability::Write) ||
                               has_capability(mask, Capability::Destructive)};
}

bool StdioServer::catalog_changed() {
    if (bridge_.game_pid() == 0)
        return false;
    const CapabilityMask mask = bridge_.capabilities();
    const bool changed = announced_capabilities_valid_ && mask != announced_capabilities_;
    announced_capabilities_ = mask;
    announced_capabilities_valid_ = true;
    return changed;
}

nlohmann::json StdioServer::call_tool(std::string tool_name, Json arguments) {
    if (tool_name == "batch")
        return call_batch(arguments);
    return call_single(std::move(tool_name), std::move(arguments));
}

namespace {
// Stop dispatching a batch once its results pass this size.
constexpr std::size_t kBatchResultBytes = 24 * 1024;

// Reads only; no tracing, invocation or writes.
bool batchable(std::string_view tool) {
    return tool != "batch" && is_base_tool(tool) &&
           (is_read_only_tool(tool) || tool == "read_member" || tool == "inspect_managed_object" ||
            tool == "read_static_members");
}

using ToolRunner = std::function<Json(std::string, Json)>;

Json batch_failure(const std::string& error) {
    return {{"ok", false}, {"error", error}};
}

Json batch_entry(const Json& call, std::size_t spent, const ToolRunner& run) {
    const std::string tool = call.value("tool", std::string{});
    Json arguments = call.value("arguments", Json::object());
    if (!batchable(tool))
        return batch_failure("not_batchable: " + tool);

    const auto& catalog = tool_catalog(true, true, true);
    const auto definition = std::find_if(catalog.begin(), catalog.end(), [&](const Json& entry) {
        return entry.value("name", std::string{}) == tool;
    });
    std::string problem;
    if (definition == catalog.end())
        return batch_failure("not_batchable: " + tool);
    if (!validate_json_schema(arguments, (*definition)["inputSchema"], problem))
        return batch_failure("invalid_arguments: " + problem);
    if (spent >= kBatchResultBytes)
        return batch_failure("batch_budget_exhausted: send this call again");

    Json outcome = run(tool, std::move(arguments));
    if (outcome.value("isError", false))
        return batch_failure(outcome["content"][0].value("text", std::string{}));
    return {{"ok", true}, {"result", std::move(outcome["structuredContent"])}};
}
} // namespace

nlohmann::json StdioServer::call_batch(const Json& arguments) {
    Json results = Json::array();
    std::size_t spent = 0;
    for (const Json& call : arguments.value("calls", Json::array())) {
        Json entry = batch_entry(call, spent, [this](std::string tool, Json call_arguments) {
            return call_single(std::move(tool), std::move(call_arguments));
        });
        spent += entry.dump().size();
        results.push_back(std::move(entry));
    }
    Json structured = {{"results", std::move(results)}};
    return {{"content", {{{"type", "text"}, {"text", structured.dump()}}}},
            {"structuredContent", std::move(structured)}, {"isError", false}};
}

nlohmann::json StdioServer::call_single(std::string tool_name, Json arguments) {
    if (!is_available_tool(tool_name, true, true, true))
        return {{"content", {{{"type", "text"}, {"text", "Unknown MCP tool: " + tool_name}}}},
                {"isError", true}};
    std::string connection_error;
    if (!ensure_connected(connection_error))
        return {{"content", {{{"type", "text"}, {"text", connection_error}}}}, {"isError", true}};
    // Explorer Config is the permission authority, so the check belongs after
    // the bridge is known rather than against the helper's switches alone.
    const ToolPermissions granted = permissions();
    if (!is_available_tool(tool_name, granted.tracing, granted.invocation, granted.mutation))
        return {{"content", {{{"type", "text"},
            {"text", "permission_denied: Explorer Config does not grant " + tool_name}}}},
                {"isError", true}};
    Request request{std::to_string(next_bridge_id_++), std::move(tool_name), std::move(arguments)};
    Response response;
    std::string bridge_error;
    // Honour the bridge's retry hint instead of failing.
    for (int attempt = 0;; ++attempt) {
        if (!bridge_.transact(request, response, bridge_error)) {
            connection_announced_ = false;
            return {{"content", {{{"type", "text"}, {"text", bridge_error}}}}, {"isError", true}};
        }
        const bool retryable = !response.ok && response.retry_after_ms != 0 &&
                               response.retry_after_ms <= kMaximumAutoRetryMs;
        if (!retryable || attempt >= kMaximumAutoRetries)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(response.retry_after_ms));
        request.id = std::to_string(next_bridge_id_++);
    }
    if (!response.ok) {
        std::string text = response.error_code + ": " + response.error_message;
        if (response.retry_after_ms != 0)
            text += " (retry_after_ms=" + std::to_string(response.retry_after_ms) + ")";
        return {{"content", {{{"type", "text"}, {"text", text}}}}, {"isError", true}};
    }
    Json result = std::move(response.result);
    if (compact_output_)
        result = compact_tables(std::move(result));
    return {{"content", {{{"type", "text"}, {"text", result.dump()}}}},
            {"structuredContent", std::move(result)},
            {"isError", false}};
}

int StdioServer::run() {
    return run(std::cin, std::cout);
}

int StdioServer::run(std::istream& input, std::ostream& output) {
    BoundedStdioTransport transport(input, output, max_message_bytes, kMaximumStdioOutputBytes);
    JsonRpcSession session;
    RequestDispatcher dispatcher(
        [this](std::string tool, Json arguments) { return call_tool(std::move(tool), std::move(arguments)); },
        [&](const Json& message) { transport.emit(message); });
    std::string line;
    for (;;) {
        const auto read = transport.read(line);
        if (read == BoundedStdioTransport::ReadResult::End)
            break;
        if (read == BoundedStdioTransport::ReadResult::Error) {
            dispatcher.stop();
            return 1;
        }
        if (read == BoundedStdioTransport::ReadResult::TooLarge) {
            transport.emit(JsonRpcSession::error(nullptr, -32600, "Message exceeds the 64 KiB limit"));
            continue;
        }
        Json decode_error;
        const std::optional<RpcMessage> decoded = session.decode(line, decode_error);
        if (!decoded) {
            transport.emit(decode_error);
            continue;
        }
        const RpcMessage& message = *decoded;
        Json lifecycle_error;
        if (!session.permits(message, lifecycle_error)) {
            if (!message.notification && !lifecycle_error.is_null())
                transport.emit(lifecycle_error);
            continue;
        }
        if (message.method == "notifications/initialized") {
            session.initialized();
            continue;
        }
        if (message.method == "notifications/cancelled") {
            if (message.params.contains("requestId"))
                dispatcher.cancel_request(message.params["requestId"],
                    message.params.value("reason", std::string("Client cancelled the request")));
            continue;
        }
        if (message.method == "server/discover") {
            if (!message.notification)
                transport.emit(JsonRpcSession::result(message.id, {{"resultType", "complete"},
                {"supportedVersions", {"2025-11-25", "2025-06-18", "2025-03-26"}},
                {"capabilities", {{"tools", {{"listChanged", true}}},
                                  {"tasks", {{"list", Json::object()}, {"cancel", Json::object()},
                                   {"requests", {{"tools", {{"call", Json::object()}}}}}}}}},
                {"serverInfo", {{"name", "unity-runtime-explorer"}, {"version", URK::project_version}}}}));
            continue;
        }
        if (message.method == "initialize") {
            transport.emit(session.initialize(message));
            continue;
        }
        if (message.method == "ping") {
            if (!message.notification)
                transport.emit(JsonRpcSession::result(message.id, Json::object()));
            continue;
        }
        if (message.method == "tools/list") {
            if (!message.notification) {
                // Connect opportunistically so the catalog reflects what
                // Explorer Config actually grants instead of everything.
                std::string ignored;
                ensure_connected(ignored);
                catalog_changed();
                const ToolPermissions granted = permissions();
                transport.emit(JsonRpcSession::result(message.id,
                    {{"tools", tool_catalog(granted.tracing, granted.invocation, granted.mutation)}}));
            }
            continue;
        }
        if (dispatcher.handle_task_request(message.method, message.id, message.params))
            continue;
        if (message.method == "tools/call") {
            if (message.notification)
                continue;
            if (!message.params.contains("name") || !message.params["name"].is_string()) {
                transport.emit(JsonRpcSession::error(message.id, -32602, "tools/call requires a string name"));
                continue;
            }
            const std::string tool_name = message.params["name"].get<std::string>();
            if (!is_available_tool(tool_name, true, true, true)) {
                transport.emit(JsonRpcSession::error(message.id, -32602, "Unknown tool: " + tool_name));
                continue;
            }
            const Json arguments = message.params.value("arguments", Json::object());
            if (!arguments.is_object()) {
                transport.emit(JsonRpcSession::error(message.id, -32602, "tool arguments must be an object"));
                continue;
            }
            const auto& catalog = tool_catalog(true, true, true);
            const auto definition = std::find_if(catalog.begin(), catalog.end(), [&](const Json& tool) {
                return tool.value("name", std::string{}) == tool_name;
            });
            std::string validation_error;
            if (definition == catalog.end() ||
                !validate_json_schema(arguments, (*definition)["inputSchema"], validation_error)) {
                transport.emit(JsonRpcSession::result(message.id,
                    {{"content", {{{"type", "text"}, {"text", "invalid_arguments: " + validation_error}}}},
                     {"isError", true}}));
                continue;
            }
            std::optional<Json> progress_token;
            if (message.params.contains("_meta") && message.params["_meta"].is_object() &&
                message.params["_meta"].contains("progressToken")) {
                const Json& token = message.params["_meta"]["progressToken"];
                if (token.is_string() || token.is_number_integer() || token.is_number_unsigned())
                    progress_token = token;
            }
            std::optional<std::uint64_t> task_ttl;
            if (message.params.contains("task")) {
                if (session.protocol_version() != "2025-11-25" || !message.params["task"].is_object()) {
                    transport.emit(JsonRpcSession::error(message.id, -32602, "Invalid or unsupported task augmentation"));
                    continue;
                }
                task_ttl = kDefaultTaskTtlMs;
                if (message.params["task"].contains("ttl")) {
                    if (!message.params["task"]["ttl"].is_number_unsigned() &&
                        !message.params["task"]["ttl"].is_number_integer()) {
                        transport.emit(JsonRpcSession::error(message.id, -32602, "task.ttl must be an integer"));
                        continue;
                    }
                    const auto value = message.params["task"]["ttl"].get<std::int64_t>();
                    if (value <= 0) {
                        transport.emit(JsonRpcSession::error(message.id, -32602, "task.ttl must be positive"));
                        continue;
                    }
                    task_ttl = static_cast<std::uint64_t>(value);
                }
            }
            dispatcher.submit(message.id, tool_name, arguments, std::move(progress_token), task_ttl);
            // A Config toggle in game changes the permitted catalog mid-session.
            if (catalog_changed())
                transport.emit({{"jsonrpc", "2.0"}, {"method", "notifications/tools/list_changed"},
                                {"params", Json::object()}});
            continue;
        }
        if (!message.notification)
            transport.emit(JsonRpcSession::error(message.id, -32601, "Method not found"));
    }
    session.close();
    dispatcher.stop();
    return 0;
}

} // namespace Explorer::Mcp

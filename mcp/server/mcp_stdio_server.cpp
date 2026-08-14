// Copyright (c) 2026 Jadis0x. All rights reserved.
#include "mcp_stdio_server.h"

#include "bounded_stdio_transport.h"
#include "mcp/core/tool_catalog.h"
#include "mcp/core/schema_validator.h"
#include "project_version.h"
#include "request_dispatcher.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <iostream>
#include <string>

namespace Explorer::Mcp {
namespace {
using Json = nlohmann::json;

constexpr std::uint64_t kDefaultTaskTtlMs = 300000;
constexpr std::size_t kMaximumStdioOutputBytes = 128 * 1024;
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

nlohmann::json StdioServer::call_tool(std::string tool_name, Json arguments) {
    if (!is_available_tool(tool_name, true, true, true))
        return {{"content", {{{"type", "text"}, {"text", "Unknown MCP tool: " + tool_name}}}},
                {"isError", true}};
    std::string connection_error;
    if (!ensure_connected(connection_error))
        return {{"content", {{{"type", "text"}, {"text", connection_error}}}}, {"isError", true}};
    Request request{std::to_string(next_bridge_id_++), std::move(tool_name), std::move(arguments)};
    Response response;
    std::string bridge_error;
    if (!bridge_.transact(request, response, bridge_error)) {
        connection_announced_ = false;
        return {{"content", {{{"type", "text"}, {"text", bridge_error}}}}, {"isError", true}};
    }
    if (!response.ok)
        return {{"content", {{{"type", "text"},
            {"text", response.error_code + ": " + response.error_message}}}}, {"isError", true}};
    return {{"content", {{{"type", "text"}, {"text", response.result.dump()}}}},
            {"structuredContent", response.result}, {"isError", false}};
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
                {"capabilities", {{"tools", {{"listChanged", false}}},
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
            if (!message.notification)
                transport.emit(JsonRpcSession::result(message.id, {{"tools", tool_catalog(true, true, true)}}));
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

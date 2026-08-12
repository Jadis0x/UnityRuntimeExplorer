// Copyright (c) 2026 Jadis0x. All rights reserved.
#include "mcp_stdio_server.h"

#include "mcp/core/tool_catalog.h"

#include <nlohmann/json.hpp>

#include <iostream>
#include <string>

namespace Explorer::Mcp {
namespace {
using Json = nlohmann::json;

Json rpc_error(const Json& id, int code, std::string message) {
    return {{"jsonrpc", "2.0"}, {"id", id}, {"error", {{"code", code}, {"message", std::move(message)}}}};
}

Json rpc_result(const Json& id, Json result) {
    return {{"jsonrpc", "2.0"}, {"id", id}, {"result", std::move(result)}};
}

void emit(const Json& value) {
    std::cout << value.dump() << '\n' << std::flush;
}
} // namespace

bool StdioServer::ensure_connected(std::string& error) {
    if (bridge_.game_pid() != 0)
        return true;
    return bridge_.connect(game_pid_, error);
}

int StdioServer::run() {
    std::string line;
    while (std::getline(std::cin, line)) {
        Json message;
        try {
            message = Json::parse(line);
        } catch (const Json::exception&) {
            emit(rpc_error(nullptr, -32700, "Parse error"));
            continue;
        }
        if (!message.is_object() || message.value("jsonrpc", std::string{}) != "2.0" ||
            !message.contains("method") || !message["method"].is_string()) {
            emit(rpc_error(message.value("id", Json(nullptr)), -32600, "Invalid Request"));
            continue;
        }
        const bool notification = !message.contains("id");
        const Json id = message.value("id", Json(nullptr));
        const std::string method = message["method"].get<std::string>();
        if (method == "notifications/initialized" || method == "notifications/cancelled")
            continue;
        if (method == "server/discover") {
            emit(rpc_result(id, {{"resultType", "complete"},
                {"supportedVersions", {"2025-11-25", "2025-06-18", "2025-03-26"}},
                {"capabilities", {{"tools", Json::object()}}},
                {"serverInfo", {{"name", "unity-runtime-explorer"}, {"version", "0.2.0"}}},
                {"instructions", "Read-only access to one live UnityRuntimeExplorer instance. Opaque references expire when the scene or hierarchy changes."}}));
            continue;
        }
        if (method == "initialize") {
            const Json params = message.value("params", Json::object());
            const std::string requested = params.value("protocolVersion", std::string("2025-06-18"));
            const std::string protocol = requested == "2025-11-25" || requested == "2025-06-18" ||
                                         requested == "2025-03-26" ? requested : "2025-06-18";
            emit(rpc_result(id, {{"protocolVersion", protocol}, {"capabilities", {{"tools", Json::object()}}},
                {"serverInfo", {{"name", "unity-runtime-explorer"}, {"version", "0.2.0"}}},
                {"instructions", "Read-only access to one live UnityRuntimeExplorer instance. Use hierarchy_search to obtain opaque references. References expire when the scene or hierarchy changes. Never request or infer managed pointers. Calls are rate-limited and bounded."}}));
            continue;
        }
        if (method == "ping") {
            if (!notification)
                emit(rpc_result(id, Json::object()));
            continue;
        }
        if (method == "tools/list") {
            if (!notification)
                emit(rpc_result(id, {{"tools", tool_catalog()}}));
            continue;
        }
        if (method == "tools/call") {
            if (notification)
                continue;
            const Json params = message.value("params", Json::object());
            if (!params.is_object() || !params.contains("name") || !params["name"].is_string()) {
                emit(rpc_error(id, -32602, "tools/call requires a tool name"));
                continue;
            }
            const std::string tool_name = params["name"].get<std::string>();
            if (!is_read_only_tool(tool_name)) {
                emit(rpc_result(id, {{"content", {{{"type", "text"},
                    {"text", "The requested tool is not available in the read-only server."}}}}, {"isError", true}}));
                continue;
            }
            const Json arguments = params.value("arguments", Json::object());
            if (!arguments.is_object()) {
                emit(rpc_error(id, -32602, "tool arguments must be an object"));
                continue;
            }
            std::string connection_error;
            if (!ensure_connected(connection_error)) {
                emit(rpc_result(id, {{"content", {{{"type", "text"}, {"text", connection_error}}}},
                                     {"isError", true}}));
                continue;
            }
            Request request{std::to_string(next_bridge_id_++), tool_name, arguments};
            Response response;
            std::string bridge_error;
            if (!bridge_.transact(request, response, bridge_error)) {
                emit(rpc_result(id, {{"content", {{{"type", "text"}, {"text", bridge_error}}}},
                                     {"isError", true}}));
                continue;
            }
            if (!response.ok) {
                emit(rpc_result(id, {{"content", {{{"type", "text"},
                    {"text", response.error_code + ": " + response.error_message}}}}, {"isError", true}}));
                continue;
            }
            emit(rpc_result(id, {{"content", {{{"type", "text"}, {"text", response.result.dump()}}}},
                                 {"structuredContent", response.result}, {"isError", false}}));
            continue;
        }
        if (!notification)
            emit(rpc_error(id, -32601, "Method not found"));
    }
    return std::cin.bad() ? 1 : 0;
}

} // namespace Explorer::Mcp

// Copyright (c) 2026 Jadis0x. All rights reserved.
#include "mcp/core/bridge_protocol.h"
#include "mcp/core/schema_validator.h"
#include "mcp/core/tool_catalog.h"
#include "mcp/server/mcp_stdio_server.h"
#include "mcp/server/request_dispatcher.h"

#include <cstdlib>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace {
using Json = nlohmann::json;

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

std::vector<Json> run_server(std::string input) {
    std::istringstream stream(std::move(input));
    std::ostringstream output;
    Explorer::Mcp::StdioServer server(4294967295u, false, false);
    require(server.run(stream, output) == 0, "stdio server exits cleanly");
    std::vector<Json> messages;
    std::istringstream lines(output.str());
    std::string line;
    while (std::getline(lines, line))
        messages.push_back(Json::parse(line));
    return messages;
}
} // namespace

int main() {
    using namespace Explorer::Mcp;
    const std::string initialize =
        R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-11-25","capabilities":{},"clientInfo":{"name":"test","version":"1"}}})";

    auto messages = run_server(std::string(
        R"({"jsonrpc":2,"id":0,"method":"initialize","params":{}})" "\n") +
        R"({"jsonrpc":"2.0","id":9,"method":"initialize","params":[]})" "\n" +
        initialize + "\n" +
        R"({"jsonrpc":"2.0","method":"notifications/initialized"})" "\n" +
        R"({"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}})" "\n");
    require(messages.size() == 4, "malformed messages produce errors and valid messages continue");
    require(messages[0]["error"]["code"] == -32600, "invalid JSON-RPC version is rejected");
    require(messages[1]["error"]["code"] == -32602, "non-object params are rejected");
    require(messages[2]["result"]["protocolVersion"] == "2025-11-25", "current protocol is negotiated");
    require(messages[3]["result"]["tools"].size() == tool_catalog(true, true, true).size(),
            "stdio exposes the complete catalog");

    messages = run_server(std::string(
        R"({"jsonrpc":"2.0","id":1,"method":"tools/list","params":{}})" "\n") +
        initialize + "\n" +
        R"({"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}})" "\n" +
        R"({"jsonrpc":"2.0","method":"notifications/initialized"})" "\n" +
        R"({"jsonrpc":"2.0","id":3,"method":"initialize","params":{"protocolVersion":"2025-11-25","capabilities":{},"clientInfo":{"name":"test","version":"1"}}})" "\n");
    require(messages[0]["error"]["code"] == -32600, "operation before initialize is rejected");
    require(messages[2]["error"]["code"] == -32600, "operation before initialized notification is rejected");
    require(messages[3]["error"]["code"] == -32600, "duplicate initialize is rejected");

    std::string oversized(max_message_bytes + 128, 'x');
    messages = run_server(oversized + "\n" + initialize + "\n");
    require(messages.size() == 2 && messages[0]["error"]["code"] == -32600,
            "oversized input is drained without terminating the session");

    std::string schema_error;
    require(!validate_json_schema(Json{{"limit", 0}},
        Json{{"type", "object"}, {"properties", {{"limit", {{"type", "integer"}, {"minimum", 1}}}}},
             {"additionalProperties", false}}, schema_error),
        "central schema validation enforces numeric bounds");

    std::mutex emitted_mutex;
    std::vector<Json> emitted;
    RequestDispatcher dispatcher(
        [](std::string tool, Json arguments) {
            return Json{{"content", {{{"type", "text"}, {"text", tool}}}},
                        {"structuredContent", arguments}, {"isError", false}};
        },
        [&](const Json& value) {
            std::lock_guard lock(emitted_mutex);
            emitted.push_back(value);
        });
    dispatcher.submit(7, "runtime_status", Json::object(), Json("progress-1"), 60000);
    dispatcher.stop();
    std::string task_id;
    {
        std::lock_guard lock(emitted_mutex);
        for (const Json& value : emitted)
            if (value.value("id", Json(nullptr)) == Json(7) && value.contains("result") &&
                value["result"].contains("task"))
                task_id = value["result"]["task"]["taskId"].get<std::string>();
    }
    require(!task_id.empty(), "task-augmented tool call returns a task id");
    dispatcher.handle_task_request("tasks/get", 8, {{"taskId", task_id}});
    dispatcher.handle_task_request("tasks/result", 9, {{"taskId", task_id}});
    {
        std::lock_guard lock(emitted_mutex);
        bool completed = false;
        bool related_result = false;
        for (const Json& value : emitted) {
            if (value.value("id", Json(nullptr)) == Json(8) &&
                value["result"].value("status", std::string{}) == "completed")
                completed = true;
            if (value.value("id", Json(nullptr)) == Json(9) && value.contains("result") &&
                value["result"].contains("_meta"))
                related_result = true;
        }
        require(completed, "completed tasks remain queryable");
        require(related_result, "task results include related-task metadata");
    }
    return 0;
}

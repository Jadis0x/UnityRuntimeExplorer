// Copyright (c) 2026 Jadis0x. All rights reserved.
#include "json_rpc_session.h"
#include "project_version.h"

#include <array>

namespace Explorer::Mcp {
namespace {
using Json = nlohmann::json;
constexpr std::array<std::string_view, 3> kSupportedVersions{
    "2025-11-25", "2025-06-18", "2025-03-26"};

bool valid_id(const Json& id) {
    return id.is_null() || id.is_string() || id.is_number_integer() || id.is_number_unsigned();
}
} // namespace

Json JsonRpcSession::error(const Json& id, int code, std::string message, Json data) {
    Json body{{"code", code}, {"message", std::move(message)}};
    if (!data.is_null())
        body["data"] = std::move(data);
    return {{"jsonrpc", "2.0"}, {"id", id}, {"error", std::move(body)}};
}

Json JsonRpcSession::result(const Json& id, Json value) {
    return {{"jsonrpc", "2.0"}, {"id", id}, {"result", std::move(value)}};
}

std::optional<RpcMessage> JsonRpcSession::decode(std::string_view line, Json& error_response) const {
    Json value;
    try {
        value = Json::parse(line);
    } catch (const Json::parse_error&) {
        error_response = error(nullptr, -32700, "Parse error");
        return std::nullopt;
    } catch (const Json::exception&) {
        error_response = error(nullptr, -32600, "Invalid Request");
        return std::nullopt;
    }
    if (!value.is_object() || !value.contains("jsonrpc") || !value["jsonrpc"].is_string() ||
        value["jsonrpc"].get_ref<const std::string&>() != "2.0" ||
        !value.contains("method") || !value["method"].is_string()) {
        error_response = error(value.is_object() && value.contains("id") && valid_id(value["id"])
                                   ? value["id"] : Json(nullptr),
                               -32600, "Invalid Request");
        return std::nullopt;
    }
    RpcMessage message;
    message.notification = !value.contains("id");
    message.id = message.notification ? Json(nullptr) : value["id"];
    if (!message.notification && !valid_id(message.id)) {
        error_response = error(nullptr, -32600, "Invalid Request id");
        return std::nullopt;
    }
    message.method = value["method"].get<std::string>();
    if (value.contains("params")) {
        if (!value["params"].is_object()) {
            error_response = error(message.id, -32602, "params must be an object");
            return std::nullopt;
        }
        message.params = value["params"];
    }
    return message;
}

bool JsonRpcSession::permits(const RpcMessage& message, Json& error_response) const {
    if (state_ == State::Closed) {
        error_response = error(message.id, -32600, "Session is closed");
        return false;
    }
    if (message.method == "server/discover" || message.method == "ping")
        return true;
    if (message.method == "initialize" && message.notification)
        return false;
    if ((message.method == "notifications/initialized" ||
         message.method == "notifications/cancelled") && !message.notification) {
        error_response = error(message.id, -32600, "Notification method must not contain an id");
        return false;
    }
    if (state_ == State::AwaitingInitialize) {
        if (message.method == "initialize")
            return true;
        if (!message.notification)
            error_response = error(message.id, -32600, "initialize must be the first request");
        return false;
    }
    if (state_ == State::AwaitingInitialized) {
        if (message.method == "notifications/initialized")
            return true;
        if (!message.notification)
            error_response = error(message.id, -32600, "notifications/initialized is required");
        return false;
    }
    if (message.method == "initialize") {
        error_response = error(message.id, -32600, "Session is already initialized");
        return false;
    }
    return true;
}

Json JsonRpcSession::initialize(const RpcMessage& message) {
    if (!message.params.contains("protocolVersion") || !message.params["protocolVersion"].is_string() ||
        !message.params.contains("capabilities") || !message.params["capabilities"].is_object() ||
        !message.params.contains("clientInfo") || !message.params["clientInfo"].is_object() ||
        !message.params["clientInfo"].contains("name") ||
        !message.params["clientInfo"]["name"].is_string() ||
        !message.params["clientInfo"].contains("version") ||
        !message.params["clientInfo"]["version"].is_string()) {
        return error(message.id, -32602,
                     "initialize requires protocolVersion, capabilities, and clientInfo name/version");
    }
    const std::string requested = message.params["protocolVersion"].get<std::string>();
    protocol_version_ = kSupportedVersions.front();
    for (const std::string_view supported : kSupportedVersions)
        if (requested == supported) {
            protocol_version_ = requested;
            break;
        }
    state_ = State::AwaitingInitialized;
    Json capabilities{{"tools", {{"listChanged", false}}}};
    if (protocol_version_ == "2025-11-25")
        capabilities["tasks"] = {{"list", Json::object()}, {"cancel", Json::object()},
            {"requests", {{"tools", {{"call", Json::object()}}}}}};
    return result(message.id, {{"protocolVersion", protocol_version_},
        {"capabilities", std::move(capabilities)},
        {"serverInfo", {{"name", "unity-runtime-explorer"},
                        {"title", "Unity Runtime Explorer"}, {"version", URK::project_version}}},
        {"instructions", "Discover and inspect the live Unity runtime through opaque references. Explorer Config is the authoritative permission boundary. Prefer discover_runtime before targeted tools; property getters, writes, tracing, invocation, and destructive operations are independently audited."}});
}

void JsonRpcSession::initialized() {
    if (state_ == State::AwaitingInitialized)
        state_ = State::Operational;
}

} // namespace Explorer::Mcp

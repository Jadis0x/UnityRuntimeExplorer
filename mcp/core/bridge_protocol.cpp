// Copyright (c) 2026 Jadis0x. All rights reserved.
#include "bridge_protocol.h"

#include <algorithm>

namespace Explorer::Mcp {
namespace {
bool valid_id(const nlohmann::json& value) {
    return value.is_string() && !value.get_ref<const std::string&>().empty() &&
           value.get_ref<const std::string&>().size() <= 128;
}

void redact_json(nlohmann::json& value) {
    if (value.is_string()) {
        value = redact_sensitive_text(value.get_ref<const std::string&>());
        return;
    }
    if (value.is_array()) {
        for (nlohmann::json& child : value)
            redact_json(child);
        return;
    }
    if (value.is_object())
        for (auto& [_, child] : value.items())
            redact_json(child);
}
} // namespace

std::string redact_sensitive_text(std::string_view text) {
    constexpr std::size_t kMaxTextBytes = 8192;
    const std::size_t length = std::min(text.size(), kMaxTextBytes);
    std::string output;
    output.reserve(length);
    for (std::size_t index = 0; index < length;) {
        const bool prefix = index + 2 <= length && text[index] == '0' &&
            (text[index + 1] == 'x' || text[index + 1] == 'X');
        if (!prefix) {
            output.push_back(text[index++]);
            continue;
        }
        std::size_t end = index + 2;
        while (end < length && ((text[end] >= '0' && text[end] <= '9') ||
               (text[end] >= 'a' && text[end] <= 'f') || (text[end] >= 'A' && text[end] <= 'F')))
            ++end;
        if (end - (index + 2) < 8) {
            output.append(text.substr(index, end - index));
            index = end;
            continue;
        }
        output += "<redacted-address>";
        index = end;
    }
    if (text.size() > kMaxTextBytes)
        output += "<truncated>";
    return output;
}

bool parse_request(std::string_view text, Request& request, std::string& error) {
    error.clear();
    if (text.empty() || text.size() > max_message_bytes) {
        error = "request size is outside the allowed range";
        return false;
    }
    try {
        const nlohmann::json value = nlohmann::json::parse(text);
        if (!value.is_object() || !value.contains("id") || !valid_id(value["id"]) ||
            !value.contains("tool") || !value["tool"].is_string()) {
            error = "request must contain string id and tool fields";
            return false;
        }
        request.id = value["id"].get<std::string>();
        request.tool = value["tool"].get<std::string>();
        if (request.tool.empty() || request.tool.size() > 128) {
            error = "tool name is outside the allowed range";
            return false;
        }
        request.arguments = value.value("arguments", nlohmann::json::object());
        if (!request.arguments.is_object()) {
            error = "arguments must be an object";
            return false;
        }
        return true;
    } catch (const nlohmann::json::exception&) {
        error = "request is not valid JSON";
        return false;
    }
}

bool parse_response(std::string_view text, Response& response, std::string& error) {
    error.clear();
    if (text.empty() || text.size() > max_message_bytes) {
        error = "response size is outside the allowed range";
        return false;
    }
    try {
        const nlohmann::json value = nlohmann::json::parse(text);
        if (!value.is_object() || !value.contains("id") || !valid_id(value["id"]) ||
            !value.contains("ok") || !value["ok"].is_boolean()) {
            error = "response is missing required fields";
            return false;
        }
        response.id = value["id"].get<std::string>();
        response.ok = value["ok"].get<bool>();
        response.result = value.value("result", nlohmann::json::object());
        response.error_code = value.value("error_code", std::string{});
        response.error_message = value.value("error_message", std::string{});
        return true;
    } catch (const nlohmann::json::exception&) {
        error = "response is not valid JSON";
        return false;
    }
}

std::string serialize(const Request& request) {
    return nlohmann::json{{"version", bridge_protocol_version}, {"id", request.id},
                          {"tool", request.tool}, {"arguments", request.arguments}}.dump();
}

std::string serialize(const Response& response) {
    nlohmann::json value{{"version", bridge_protocol_version}, {"id", response.id}, {"ok", response.ok}};
    if (response.ok)
        value["result"] = response.result;
    else {
        value["error_code"] = response.error_code;
        value["error_message"] = response.error_message;
    }
    redact_json(value);
    return value.dump();
}

Response failure(std::string id, std::string code, std::string message) {
    return Response{std::move(id), false, nlohmann::json::object(), std::move(code), std::move(message)};
}

} // namespace Explorer::Mcp

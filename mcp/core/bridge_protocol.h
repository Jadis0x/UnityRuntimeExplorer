// Copyright (c) 2026 Jadis0x. All rights reserved.
#pragma once

#include <nlohmann/json.hpp>

#include <string>

namespace Explorer::Mcp {

inline constexpr std::size_t max_message_bytes = 64 * 1024;
inline constexpr const char* bridge_protocol_version = "1";

struct Request {
    std::string id;
    std::string tool;
    nlohmann::json arguments = nlohmann::json::object();
};

struct Response {
    std::string id;
    bool ok = false;
    nlohmann::json result = nlohmann::json::object();
    std::string error_code;
    std::string error_message;
};

bool parse_request(std::string_view text, Request& request, std::string& error);
bool parse_response(std::string_view text, Response& response, std::string& error);
std::string serialize(const Request& request);
std::string serialize(const Response& response);
Response failure(std::string id, std::string code, std::string message);
std::string redact_sensitive_text(std::string_view text);

} // namespace Explorer::Mcp

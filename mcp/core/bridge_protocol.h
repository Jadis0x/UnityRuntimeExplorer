// Copyright (c) 2026 Jadis0x. All rights reserved.
#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>

namespace Explorer::Mcp {

inline constexpr std::size_t max_message_bytes = 64 * 1024;
inline constexpr const char* bridge_protocol_version = "2";

enum class Capability : std::uint32_t {
    Read = 1u << 0u,
    AutoDiscovery = 1u << 1u,
    PropertyAccess = 1u << 2u,
    Write = 1u << 3u,
    Trace = 1u << 4u,
    Invoke = 1u << 5u,
    Destructive = 1u << 6u,
};

using CapabilityMask = std::uint32_t;

constexpr CapabilityMask capability_bit(Capability capability) {
    return static_cast<CapabilityMask>(capability);
}

constexpr bool has_capability(CapabilityMask mask, Capability capability) {
    return (mask & capability_bit(capability)) != 0;
}

struct RequestContext {
    // Populated inside the injected bridge immediately before dispatch. This
    // field is deliberately absent from the serialized request format.
    CapabilityMask capabilities = 0;
};

struct Request {
    std::string id;
    std::string tool;
    nlohmann::json arguments = nlohmann::json::object();
    RequestContext context;
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

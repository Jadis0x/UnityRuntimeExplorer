// Copyright (c) 2026 Jadis0x. All rights reserved.
#pragma once

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <string_view>

namespace Explorer::Mcp {

struct RpcMessage {
    nlohmann::json id;
    std::string method;
    nlohmann::json params = nlohmann::json::object();
    bool notification = false;
};

class JsonRpcSession {
  public:
    enum class State { AwaitingInitialize, AwaitingInitialized, Operational, Closed };

    std::optional<RpcMessage> decode(std::string_view line, nlohmann::json& error_response) const;
    bool permits(const RpcMessage& message, nlohmann::json& error_response) const;
    nlohmann::json initialize(const RpcMessage& message);
    void initialized();
    void close() { state_ = State::Closed; }
    State state() const { return state_; }
    const std::string& protocol_version() const { return protocol_version_; }

    static nlohmann::json error(const nlohmann::json& id, int code, std::string message,
                                nlohmann::json data = nullptr);
    static nlohmann::json result(const nlohmann::json& id, nlohmann::json value);

  private:
    State state_ = State::AwaitingInitialize;
    std::string protocol_version_;
};

} // namespace Explorer::Mp

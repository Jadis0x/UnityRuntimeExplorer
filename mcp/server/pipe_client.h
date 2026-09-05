// Copyright (c) 2026 Jadis0x. All rights reserved.
#pragma once

#include "mcp/core/bridge_protocol.h"

#include <Windows.h>

#include <cstdint>
#include <optional>
#include <string>

namespace Explorer::Mcp {

class PipeClient {
  public:
    PipeClient() = default;
    ~PipeClient();
    PipeClient(const PipeClient&) = delete;
    PipeClient& operator=(const PipeClient&) = delete;

    bool connect(std::optional<std::uint32_t> game_pid, std::string& error);
    bool transact(const Request& request, Response& response, std::string& error);
    void close();
    std::uint32_t game_pid() const { return game_pid_; }
    // Permissions Explorer Config currently grants. The bridge republishes its
    // discovery document whenever a toggle changes, so this is re-read rather
    // than cached from connect time.
    CapabilityMask capabilities() const;

  private:
    HANDLE pipe_ = INVALID_HANDLE_VALUE;
    std::uint32_t game_pid_ = 0;
    // Bytes a read delivered past the newline it completed.
    std::string carry_;
};

} // namespace Explorer::Mcp

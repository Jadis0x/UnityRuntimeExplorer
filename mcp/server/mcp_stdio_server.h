// Copyright (c) 2026 Jadis0x. All rights reserved.
#pragma once

#include "pipe_client.h"

#include <cstdint>
#include <optional>

namespace Explorer::Mcp {

class StdioServer {
  public:
    explicit StdioServer(std::optional<std::uint32_t> game_pid) : game_pid_(game_pid) {}
    int run();

  private:
    bool ensure_connected(std::string& error);

    std::optional<std::uint32_t> game_pid_;
    PipeClient bridge_;
    std::uint64_t next_bridge_id_ = 1;
};

} // namespace Explorer::Mcp

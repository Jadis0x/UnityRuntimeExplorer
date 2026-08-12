// Copyright (c) 2026 Jadis0x. All rights reserved.
#pragma once

#include "pipe_client.h"
#include "json_rpc_session.h"

#include <cstdint>
#include <iosfwd>
#include <optional>

namespace Explorer::Mcp {

class StdioServer {
  public:
    StdioServer(std::optional<std::uint32_t> game_pid, bool allow_tracing, bool allow_invocation)
        : game_pid_(game_pid) { (void)allow_tracing; (void)allow_invocation; }
    int run();
    int run(std::istream& input, std::ostream& output);

  private:
    bool ensure_connected(std::string& error);
    nlohmann::json call_tool(std::string tool_name, nlohmann::json arguments);

    std::optional<std::uint32_t> game_pid_;
    bool connection_announced_ = false;
    PipeClient bridge_;
    std::uint64_t next_bridge_id_ = 1;
};

} // namespace Explorer::Mcp

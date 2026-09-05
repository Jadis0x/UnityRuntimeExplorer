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
    // What the catalog may expose right now, as granted by Explorer Config.
    struct ToolPermissions {
        bool tracing = true;
        bool invocation = true;
        bool mutation = true;
    };

    bool ensure_connected(std::string& error);
    nlohmann::json call_tool(std::string tool_name, nlohmann::json arguments);
    ToolPermissions permissions();
    // True when the granted permissions changed since the last catalog answer.
    bool catalog_changed();

    std::optional<std::uint32_t> game_pid_;
    bool connection_announced_ = false;
    CapabilityMask announced_capabilities_ = 0;
    bool announced_capabilities_valid_ = false;
    PipeClient bridge_;
    std::uint64_t next_bridge_id_ = 1;
};

} // namespace Explorer::Mcp

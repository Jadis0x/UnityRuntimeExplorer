// Copyright (c) 2026 Jadis0x. All rights reserved.
#pragma once

#include <nlohmann/json.hpp>

#include <string_view>

namespace Explorer::Mcp {

const nlohmann::json& tool_catalog();
bool is_read_only_tool(std::string_view name);

} // namespace Explorer::Mcp

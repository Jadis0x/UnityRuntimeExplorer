// Copyright (c) 2026 Jadis0x. All rights reserved.
#pragma once

#include <nlohmann/json.hpp>

#include <string_view>

namespace Explorer::Mcp {

const nlohmann::json& tool_catalog(bool include_instrumentation = false);
const nlohmann::json& tool_catalog(bool include_tracing, bool include_invocation);
const nlohmann::json& tool_catalog(bool include_tracing, bool include_invocation, bool include_mutation);
bool is_read_only_tool(std::string_view name);
bool is_base_tool(std::string_view name);
bool is_instrumentation_tool(std::string_view name);
bool is_invocation_tool(std::string_view name);
bool is_write_tool(std::string_view name);
bool is_destructive_tool(std::string_view name);
bool is_available_tool(std::string_view name, bool allow_instrumentation);
bool is_available_tool(std::string_view name, bool allow_tracing, bool allow_invocation);
bool is_available_tool(std::string_view name, bool allow_tracing, bool allow_invocation, bool allow_mutation);

} // namespace Explorer::Mcp

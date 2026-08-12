// Copyright (c) 2026 Jadis0x. All rights reserved.
#pragma once

#include <nlohmann/json.hpp>

#include <string>

namespace Explorer::Mcp {

bool validate_json_schema(const nlohmann::json& value, const nlohmann::json& schema,
                          std::string& error, std::string path = "arguments");

} // namespace Explorer::Mcp

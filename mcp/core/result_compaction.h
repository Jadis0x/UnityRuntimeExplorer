// Copyright (c) 2026 Jadis0x. All rights reserved.
#pragma once

#include <nlohmann/json.hpp>

namespace Explorer::Mcp {

// Rewrites arrays of uniform objects as {"cols":[...],"rows":[[...]]} tables.
// Only when smaller; a key a row lacks becomes a null cell.
nlohmann::json compact_tables(nlohmann::json value);

} // namespace Explorer::Mcp

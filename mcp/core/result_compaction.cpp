// Copyright (c) 2026 Jadis0x. All rights reserved.
#include "result_compaction.h"

#include <algorithm>
#include <string>
#include <vector>

namespace Explorer::Mcp {
namespace {
using Json = nlohmann::json;

constexpr std::size_t kMinimumRows = 3;
constexpr std::size_t kMinimumColumns = 2;
constexpr std::size_t kMaximumColumns = 32;

// Table form of `rows`, or null when not worth it.
Json tabulate(const Json& rows) {
    if (rows.size() < kMinimumRows)
        return nullptr;
    std::vector<std::string> columns;
    for (const Json& row : rows) {
        if (!row.is_object() || row.size() < kMinimumColumns)
            return nullptr;
        for (auto it = row.begin(); it != row.end(); ++it)
            if (std::find(columns.begin(), columns.end(), it.key()) == columns.end()) {
                columns.push_back(it.key());
                if (columns.size() > kMaximumColumns)
                    return nullptr;
            }
    }
    // A column that is null in every row carries nothing.
    std::erase_if(columns, [&](const std::string& column) {
        return std::all_of(rows.begin(), rows.end(), [&](const Json& row) {
            const auto found = row.find(column);
            return found == row.end() || found->is_null();
        });
    });
    if (columns.size() < kMinimumColumns)
        return nullptr;
    Json table_rows = Json::array();
    for (const Json& row : rows) {
        Json cells = Json::array();
        for (const std::string& column : columns) {
            const auto found = row.find(column);
            cells.push_back(found == row.end() ? Json(nullptr) : *found);
        }
        table_rows.push_back(std::move(cells));
    }
    Json table = {{"cols", std::move(columns)}, {"rows", std::move(table_rows)}};
    return table.dump().size() < rows.dump().size() ? table : Json(nullptr);
}
} // namespace

Json compact_tables(Json value) {
    if (value.is_object()) {
        for (auto it = value.begin(); it != value.end(); ++it)
            it.value() = compact_tables(std::move(it.value()));
        return value;
    }
    if (!value.is_array())
        return value;
    for (Json& child : value)
        child = compact_tables(std::move(child));
    Json table = tabulate(value);
    return table.is_null() ? value : table;
}

} // namespace Explorer::Mcp

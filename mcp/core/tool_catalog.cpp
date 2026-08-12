// Copyright (c) 2026 Jadis0x. All rights reserved.
#include "tool_catalog.h"

#include <algorithm>

namespace Explorer::Mcp {
namespace {
nlohmann::json schema(nlohmann::json properties = nlohmann::json::object(),
                      nlohmann::json required = nlohmann::json::array()) {
    return {{"type", "object"}, {"properties", std::move(properties)},
            {"required", std::move(required)}, {"additionalProperties", false}};
}

nlohmann::json tool(std::string_view name, std::string_view title, std::string_view description,
                    nlohmann::json input) {
    return {{"name", name}, {"title", title}, {"description", description},
            {"inputSchema", std::move(input)},
            {"annotations", {{"readOnlyHint", true}, {"destructiveHint", false},
                              {"idempotentHint", name != "export_diagnostic_bundle"},
                              {"openWorldHint", false}}}};
}
} // namespace

const nlohmann::json& tool_catalog() {
    static const nlohmann::json tools = nlohmann::json::array({
        tool("runtime_status", "Runtime status", "Return the live Explorer runtime, scene, GC, bridge, and diagnostic status.", schema()),
        tool("hierarchy_search", "Search hierarchy", "Search the immutable live Unity hierarchy. Returns opaque object references, never managed pointers.",
             schema({{"query", {{"type", "string"}, {"maxLength", 256}}},
                     {"limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 100}}},
                     {"active_only", {{"type", "boolean"}}}})),
        tool("get_selected_object", "Get selected object", "Return the GameObject currently selected in Explorer and an opaque reference.", schema()),
        tool("inspect_game_object", "Inspect GameObject", "Inspect a GameObject identified by an opaque reference, or the current selection when omitted.",
             schema({{"reference", {{"type", "string"}, {"minLength", 1}, {"maxLength", 128}}}})),
        tool("list_components", "List components", "List components on a GameObject without changing the Explorer selection.",
             schema({{"reference", {{"type", "string"}, {"minLength", 1}, {"maxLength", 128}}}})),
        tool("read_member", "Read member", "Explicitly sample one field or readable property from a component reference.",
             schema({{"component_reference", {{"type", "string"}, {"minLength", 1}, {"maxLength", 128}}},
                     {"member", {{"type", "string"}, {"minLength", 1}, {"maxLength", 256}}},
                     {"kind", {{"type", "string"}, {"enum", {"field", "property", "auto"}}}}},
                    {"component_reference", "member"})),
        tool("build_reference_graph", "Build reference graph", "Build the bounded reference graph for the current Explorer selection.", schema()),
        tool("get_watch_history", "Get watch history", "Return bounded value-watch history from the immutable Explorer snapshot.",
             schema({{"watch_id", {{"type", "integer"}, {"minimum", 1}}},
                     {"event_limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 100}}}})),
        tool("export_diagnostic_bundle", "Export diagnostic bundle", "Write an Explorer diagnostic bundle to its fixed local diagnostics directory and return the path.", schema())
    });
    return tools;
}

bool is_read_only_tool(std::string_view name) {
    return std::any_of(tool_catalog().begin(), tool_catalog().end(), [&](const nlohmann::json& entry) {
        return entry.value("name", std::string{}) == name;
    });
}

} // namespace Explorer::Mcp

// Copyright (c) 2026 Jadis0x. All rights reserved.
#include "tool_catalog.h"

#include <algorithm>
#include <array>

namespace Explorer::Mcp {
namespace {
nlohmann::json schema(nlohmann::json properties = nlohmann::json::object(),
                      nlohmann::json required = nlohmann::json::array()) {
    return {{"type", "object"}, {"properties", std::move(properties)},
            {"required", std::move(required)}, {"additionalProperties", false}};
}

nlohmann::json tool(std::string_view name, std::string_view title, std::string_view description,
                    nlohmann::json input, bool read_only = true, bool idempotent = true,
                    bool destructive = false, bool task_support = true) {
    return {{"name", name}, {"title", title}, {"description", description},
            {"inputSchema", std::move(input)},
            {"outputSchema", {{"type", "object"}}},
            {"annotations", {{"readOnlyHint", read_only}, {"destructiveHint", destructive},
                              {"idempotentHint", idempotent},
                              {"openWorldHint", false}}},
            {"execution", {{"taskSupport", task_support ? "optional" : "forbidden"}}}};
}

const nlohmann::json& read_only_catalog() {
    static const nlohmann::json tools = nlohmann::json::array({
        tool("runtime_status", "Runtime status", "Return the live Explorer runtime, scene, GC, bridge, diagnostic, and optional-capability permission status.", schema()),
        tool("discover_runtime", "Discover runtime",
             "Automatically search live GameObjects and loaded managed types in one bounded request. Use this first when the location or type of a target is unknown.",
             schema({{"query", {{"type", "string"}, {"maxLength", 256}}},
                     {"type_query", {{"type", "string"}, {"maxLength", 256}}},
                     {"limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 100}}}})),
        tool("hierarchy_search", "Search hierarchy", "Search the immutable live Unity hierarchy by name, path, tag, instance ID, component type, or dynamic behaviour type. Returns opaque object references, never managed pointers.",
             schema({{"query", {{"type", "string"}, {"maxLength", 256}}},
                     {"limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 100}}},
                     {"active_only", {{"type", "boolean"}}}})),
        tool("find_game_objects", "Find GameObjects",
             "Rank live GameObjects using name, path, component types, dynamic behaviour types, scene, activity, and an optional semantic role. Reports ambiguity instead of guessing.",
             schema({{"query", {{"type", "string"}, {"maxLength", 256}}},
                     {"scene", {{"type", "string"}, {"maxLength", 256}}},
                     {"role", {{"type", "string"}, {"enum", {"player"}}}},
                     {"required_components", {{"type", "array"}, {"maxItems", 16},
                                               {"items", {{"type", "string"}, {"minLength", 1}, {"maxLength", 256}}}}},
                     {"required_dynamic_behaviours", {{"type", "array"}, {"maxItems", 16},
                                                      {"items", {{"type", "string"}, {"minLength", 1}, {"maxLength", 256}}}}},
                     {"limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 100}}},
                     {"active_only", {{"type", "boolean"}}}})),
        tool("get_selected_object", "Get selected object", "Return the GameObject currently selected in Explorer and an opaque reference.", schema()),
        tool("inspect_game_object", "Inspect GameObject", "Inspect a GameObject identified by an opaque reference, or the current selection when omitted.",
             schema({{"reference", {{"type", "string"}, {"minLength", 1}, {"maxLength", 128}}}})),
        tool("list_components", "List components", "List components on a GameObject without changing the Explorer selection.",
             schema({{"reference", {{"type", "string"}, {"minLength", 1}, {"maxLength", 128}}}})),
        tool("read_member", "Read member", "Explicitly sample one field or readable property from a component reference. Property access executes game code and is permission controlled.",
             schema({{"component_reference", {{"type", "string"}, {"minLength", 1}, {"maxLength", 128}}},
                     {"member", {{"type", "string"}, {"minLength", 1}, {"maxLength", 256}}},
                     {"kind", {{"type", "string"}, {"enum", {"field", "property", "auto"}}}}},
                    {"component_reference", "member"}), false),
        tool("inspect_managed_object", "Inspect managed object",
             "Read bounded fields from a component or managed-object reference and continue through returned opaque references. Property getters are opt-in because they execute game code.",
             schema({{"reference", {{"type", "string"}, {"minLength", 1}, {"maxLength", 128}}},
                     {"member_query", {{"type", "string"}, {"maxLength", 256}}},
                     {"member_limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 250}}},
                     {"include_properties", {{"type", "boolean"}}}},
                    {"reference"}), false),
        tool("read_array", "Read managed array",
             "Read a bounded page from a managed array reference. Reference elements receive new opaque references.",
             schema({{"reference", {{"type", "string"}, {"minLength", 1}, {"maxLength", 128}}},
                     {"offset", {{"type", "integer"}, {"minimum", 0}}},
                     {"limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 250}}}},
                    {"reference"})),
        tool("decode_byte_array", "Decode byte array",
             "Copy and decode a bounded managed System.Byte array as MessagePack, JSON, UTF-8 text, compressed-payload detection, or hex without exposing managed memory.",
             schema({{"reference", {{"type", "string"}, {"minLength", 1}, {"maxLength", 128}}},
                     {"max_bytes", {{"type", "integer"}, {"minimum", 1}, {"maximum", 65536}}}},
                    {"reference"})),
        tool("start_instance_scan", "Start instance scan",
             "Start a bounded, time-sliced search for live instances of a managed type. UnityEngine.Object types use a direct engine query; ordinary managed types use reachable-root traversal.",
             schema({{"type_reference", {{"type", "string"}, {"minLength", 1}, {"maxLength", 128}}},
                     {"include_all_loaded", {{"type", "boolean"}}}},
                    {"type_reference"}), true, false),
        tool("get_instance_scan", "Get instance scan",
             "Read progress and a bounded page of opaque object references from an instance scan.",
             schema({{"scan_reference", {{"type", "string"}, {"minLength", 1}, {"maxLength", 128}}},
                     {"offset", {{"type", "integer"}, {"minimum", 0}}},
                     {"limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 100}}}},
                    {"scan_reference"})),
        tool("search_types", "Search managed types", "Search loaded Mono/IL2CPP metadata and return opaque type references.",
             schema({{"query", {{"type", "string"}, {"maxLength", 256}}},
                     {"image", {{"type", "string"}, {"maxLength", 256}}},
                     {"limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 100}}}})),
        tool("search_members", "Search managed members",
             "Search fields, properties, and methods across a bounded set of loaded managed types. Returns method and declaring-type references for follow-up inspection or invocation.",
             schema({{"query", {{"type", "string"}, {"minLength", 1}, {"maxLength", 256}}},
                     {"type_query", {{"type", "string"}, {"maxLength", 256}}},
                     {"kind", {{"type", "string"}, {"enum", {"any", "field", "property", "method"}}}},
                     {"type_limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 128}}},
                     {"limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 250}}}}, {"query"})),
        tool("inspect_type", "Inspect managed type", "Return bounded field, property, and method metadata for an opaque type reference.",
             schema({{"type_reference", {{"type", "string"}, {"minLength", 1}, {"maxLength", 128}}},
                     {"member_query", {{"type", "string"}, {"maxLength", 256}}},
                     {"member_limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 250}}}},
                    {"type_reference"})),
        tool("list_method_traces", "List method traces", "List active and retained method trace sessions without exposing native addresses.", schema()),
        tool("get_method_trace", "Get method trace", "Read bounded decoded calls from a method trace session.",
             schema({{"trace_reference", {{"type", "string"}, {"minLength", 1}, {"maxLength", 128}}},
                     {"after_sequence", {{"type", "integer"}, {"minimum", 0}}},
                     {"limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 200}}}},
                    {"trace_reference"})),
        tool("build_call_graph", "Build call graph", "Aggregate captured caller-to-target relationships across retained method traces.",
             schema({{"trace_reference", {{"type", "string"}, {"minLength", 1}, {"maxLength", 128}}},
                     {"limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 250}}}})),
        tool("get_activity_log", "Get activity log", "Return bounded recent Explorer activity and MCP audit events.",
             schema({{"after_sequence", {{"type", "integer"}, {"minimum", 0}}},
                     {"limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 200}}}})),
        tool("build_reference_graph", "Build reference graph", "Build the bounded reference graph for the current Explorer selection.", schema()),
        tool("get_watch_history", "Get watch history", "Return bounded value-watch history from the immutable Explorer snapshot.",
             schema({{"watch_id", {{"type", "integer"}, {"minimum", 1}}},
                     {"event_limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 100}}}})),
        tool("export_diagnostic_bundle", "Export diagnostic bundle", "Write an Explorer diagnostic bundle to its fixed local diagnostics directory and return the path.", schema(), false, false)
    });
    return tools;
}

const nlohmann::json& instrumentation_catalog() {
    static const nlohmann::json tools = nlohmann::json::array({
        tool("start_method_trace", "Start method trace", "Install a bounded native trace hook for a discovered method. Requires explicit permission in both Explorer and the helper.",
             schema({{"method_reference", {{"type", "string"}, {"minLength", 1}, {"maxLength", 128}}}},
                    {"method_reference"}), false, false, true),
        tool("stop_method_trace", "Stop method trace", "Detach an active method trace hook while retaining captured records.",
             schema({{"trace_reference", {{"type", "string"}, {"minLength", 1}, {"maxLength", 128}}}},
                    {"trace_reference"}), false, true),
        tool("clear_method_trace", "Clear method trace", "Clear captured records for a retained method trace session.",
             schema({{"trace_reference", {{"type", "string"}, {"minLength", 1}, {"maxLength", 128}}}},
                    {"trace_reference"}), false, false, true)
    });
    return tools;
}

const nlohmann::json& invocation_catalog() {
    static const nlohmann::json tools = nlohmann::json::array({
        tool("invoke_method", "Invoke managed method",
             "Invoke one discovered managed method on a component or managed-object reference with bounded JSON arguments. This executes game code and requires independent helper and in-game permissions.",
             schema({{"method_reference", {{"type", "string"}, {"minLength", 1}, {"maxLength", 128}}},
                     {"target_reference", {{"type", "string"}, {"minLength", 1}, {"maxLength", 128}}},
                     {"arguments", {{"type", "array"}, {"maxItems", 16}}}},
                    {"method_reference", "arguments"}), false, false, true)
    });
    return tools;
}

const nlohmann::json& mutation_catalog() {
    static const nlohmann::json tools = nlohmann::json::array({
        tool("write_member", "Write managed member",
             "Write a field or writable property on an opaque component or managed-object reference. Property setters execute game code.",
             schema({{"reference", {{"type", "string"}, {"minLength", 1}, {"maxLength", 128}}},
                     {"member", {{"type", "string"}, {"minLength", 1}, {"maxLength", 256}}},
                     {"kind", {{"type", "string"}, {"enum", {"field", "property", "auto"}}}},
                     {"value", nlohmann::json::object()}}, {"reference", "member", "value"}), false, false, true),
        tool("mutate_game_object", "Mutate GameObject",
             "Rename, retag, relayer, activate, transform, duplicate, or destroy a referenced GameObject.",
             schema({{"reference", {{"type", "string"}, {"minLength", 1}, {"maxLength", 128}}},
                     {"action", {{"type", "string"}, {"enum", {"rename", "set_tag", "set_layer", "set_static", "set_active", "set_position", "set_rotation", "set_scale", "duplicate", "destroy"}}}},
                     {"value", nlohmann::json::object()}}, {"reference", "action"}), false, false, true),
        tool("manage_component", "Manage component",
             "Add, remove, or enable a component using an opaque GameObject/component reference.",
             schema({{"action", {{"type", "string"}, {"enum", {"add", "remove", "set_enabled"}}}},
                     {"object_reference", {{"type", "string"}, {"minLength", 1}, {"maxLength", 128}}},
                     {"component_reference", {{"type", "string"}, {"minLength", 1}, {"maxLength", 128}}},
                     {"image", {{"type", "string"}, {"maxLength", 256}}},
                     {"namespace", {{"type", "string"}, {"maxLength", 256}}},
                     {"class", {{"type", "string"}, {"maxLength", 256}}},
                     {"enabled", {{"type", "boolean"}}}}, {"action"}), false, false, true),
        tool("load_scene", "Load scene",
             "Load a Unity build scene by build index or scene name. This can discard current runtime state.",
             schema({{"build_index", {{"type", "integer"}, {"minimum", -1}, {"maximum", 65535}}},
                     {"name", {{"type", "string"}, {"maxLength", 512}}}}), false, false, true)
    });
    return tools;
}
} // namespace

const nlohmann::json& tool_catalog(bool include_instrumentation) {
    if (!include_instrumentation)
        return read_only_catalog();
    static const nlohmann::json combined = [] {
        nlohmann::json result = read_only_catalog();
        for (const auto& entry : instrumentation_catalog())
            result.push_back(entry);
        return result;
    }();
    return combined;
}

const nlohmann::json& tool_catalog(bool include_tracing, bool include_invocation) {
    return tool_catalog(include_tracing, include_invocation, false);
}

const nlohmann::json& tool_catalog(bool include_tracing, bool include_invocation, bool include_mutation) {
    if (include_mutation) {
        static const std::array<nlohmann::json, 4> variants = [] {
            std::array<nlohmann::json, 4> result;
            for (std::size_t index = 0; index < result.size(); ++index) {
                result[index] = tool_catalog((index & 1u) != 0, (index & 2u) != 0);
                for (const auto& entry : mutation_catalog())
                    result[index].push_back(entry);
            }
            return result;
        }();
        const std::size_t index = (include_tracing ? 1u : 0u) | (include_invocation ? 2u : 0u);
        return variants[index];
    }
    if (!include_invocation)
        return tool_catalog(include_tracing);
    if (!include_tracing) {
        static const nlohmann::json invocation_only = [] {
            nlohmann::json result = read_only_catalog();
            for (const auto& entry : invocation_catalog())
                result.push_back(entry);
            return result;
        }();
        return invocation_only;
    }
    static const nlohmann::json all = [] {
        nlohmann::json result = read_only_catalog();
        for (const auto& entry : instrumentation_catalog())
            result.push_back(entry);
        for (const auto& entry : invocation_catalog())
            result.push_back(entry);
        return result;
    }();
    return all;
}

bool is_base_tool(std::string_view name) {
    return std::any_of(tool_catalog().begin(), tool_catalog().end(), [&](const nlohmann::json& entry) {
        return entry.value("name", std::string{}) == name;
    });
}

bool is_read_only_tool(std::string_view name) {
    const auto found = std::find_if(tool_catalog(true, true, true).begin(),
        tool_catalog(true, true, true).end(), [&](const nlohmann::json& entry) {
            return entry.value("name", std::string{}) == name;
        });
    return found != tool_catalog(true, true, true).end() &&
        (*found)["annotations"].value("readOnlyHint", false);
}

bool is_instrumentation_tool(std::string_view name) {
    return std::any_of(instrumentation_catalog().begin(), instrumentation_catalog().end(),
                       [&](const nlohmann::json& entry) {
                           return entry.value("name", std::string{}) == name;
                       });
}

bool is_invocation_tool(std::string_view name) {
    return std::any_of(invocation_catalog().begin(), invocation_catalog().end(),
                       [&](const nlohmann::json& entry) {
                           return entry.value("name", std::string{}) == name;
                       });
}

bool is_write_tool(std::string_view name) {
    return std::any_of(mutation_catalog().begin(), mutation_catalog().end(),
                       [&](const nlohmann::json& entry) {
                           return entry.value("name", std::string{}) == name;
                       });
}

bool is_destructive_tool(std::string_view name) {
    return name == "mutate_game_object" || name == "manage_component" || name == "load_scene" ||
           name == "invoke_method" || name == "start_method_trace" || name == "clear_method_trace" ||
           name == "write_member";
}

bool is_available_tool(std::string_view name, bool allow_instrumentation) {
    return is_base_tool(name) || (allow_instrumentation && is_instrumentation_tool(name));
}

bool is_available_tool(std::string_view name, bool allow_tracing, bool allow_invocation) {
    return is_base_tool(name) || (allow_tracing && is_instrumentation_tool(name)) ||
        (allow_invocation && is_invocation_tool(name));
}

bool is_available_tool(std::string_view name, bool allow_tracing, bool allow_invocation, bool allow_mutation) {
    return is_available_tool(name, allow_tracing, allow_invocation) ||
        (allow_mutation && is_write_tool(name));
}

} // namespace Explorer::Mcp

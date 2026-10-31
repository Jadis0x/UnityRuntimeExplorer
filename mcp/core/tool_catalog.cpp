// Copyright (c) 2026 Jadis0x. All rights reserved.
#include "tool_catalog.h"

#include <algorithm>
#include <array>

namespace Explorer::Mcp {
namespace {
nlohmann::json schema(nlohmann::json properties = nlohmann::json::object(),
                      nlohmann::json required = nlohmann::json::array()) {
    nlohmann::json result = {{"type", "object"}, {"properties", std::move(properties)},
                             {"additionalProperties", false}};
    // Omit an empty required list.
    if (!required.empty())
        result["required"] = std::move(required);
    return result;
}

// Titles and redundant hints are omitted to save tokens.
nlohmann::json tool(std::string_view name, [[maybe_unused]] std::string_view title,
                    std::string_view description, nlohmann::json input, bool read_only = true,
                    bool idempotent = true, bool destructive = false, bool task_support = true) {
    nlohmann::json annotations = {{"readOnlyHint", read_only}, {"openWorldHint", false}};
    if (!read_only) {
        annotations["destructiveHint"] = destructive;
        annotations["idempotentHint"] = idempotent;
    }
    return {{"name", name}, {"description", description},
            {"inputSchema", std::move(input)},
            {"outputSchema", {{"type", "object"}}},
            {"annotations", std::move(annotations)},
            {"execution", {{"taskSupport", task_support ? "optional" : "forbidden"}}}};
}

const nlohmann::json& read_only_catalog() {
    static const nlohmann::json tools = nlohmann::json::array({
        tool("runtime_status", "Runtime status", "Runtime, scene, GC, bridge and permission status.", schema()),
        tool("discover_runtime", "Discover runtime",
             "Search live GameObjects and loaded types at once. Use first when a target is unknown.",
             schema({{"query", {{"type", "string"}, {"maxLength", 256}}},
                     {"type_query", {{"type", "string"}, {"maxLength", 256}}},
                     {"limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 100}}}})),
        tool("hierarchy_search", "Search hierarchy", "Search the hierarchy by name, path, tag, instance ID or component type. Page with offset while has_more.",
             schema({{"query", {{"type", "string"}, {"maxLength", 256}}},
                     {"limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 100}}},
                     {"offset", {{"type", "integer"}, {"minimum", 0}}},
                     {"active_only", {{"type", "boolean"}}}})),
        tool("find_game_objects", "Find GameObjects",
             "Rank GameObjects by name, path, components, scene, activity and optional role. Reports ambiguity.",
             schema({{"query", {{"type", "string"}, {"maxLength", 256}}},
                     {"scene", {{"type", "string"}, {"maxLength", 256}}},
                     {"role", {{"type", "string"}, {"enum", {"player"}}}},
                     {"required_components", {{"type", "array"}, {"maxItems", 16},
                                               {"items", {{"type", "string"}, {"minLength", 1}, {"maxLength", 256}}}}},
                     {"required_dynamic_behaviours", {{"type", "array"}, {"maxItems", 16},
                                                      {"items", {{"type", "string"}, {"minLength", 1}, {"maxLength", 256}}}}},
                     {"limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 100}}},
                     {"offset", {{"type", "integer"}, {"minimum", 0}}},
                     {"active_only", {{"type", "boolean"}}}})),
        tool("get_selected_object", "Get selected object", "The GameObject selected in Explorer, with a reference.", schema()),
        tool("inspect_game_object", "Inspect GameObject", "Inspect a GameObject by reference, or the current selection.",
             schema({{"reference", {{"type", "string"}, {"minLength", 1}, {"maxLength", 128}}}})),
        tool("list_components", "List components", "List a GameObject's components.",
             schema({{"reference", {{"type", "string"}, {"minLength", 1}, {"maxLength", 128}}}})),
        tool("read_member", "Read member",
             "Read one field or property. reference = instance, type_reference = static. member may be a name, alias (f_7cda59) or label. Properties run game code.",
             schema({{"reference", {{"type", "string"}, {"minLength", 1}, {"maxLength", 128}}},
                     {"component_reference", {{"type", "string"}, {"minLength", 1}, {"maxLength", 128}}},
                     {"type_reference", {{"type", "string"}, {"minLength", 1}, {"maxLength", 128}}},
                     {"member", {{"type", "string"}, {"minLength", 1}, {"maxLength", 1024}}},
                     {"kind", {{"type", "string"}, {"enum", {"field", "property", "auto"}}}}},
                    {"member"}), false),
        tool("inspect_managed_object", "Inspect managed object",
             "Read fields of a component or managed object; follow returned references. Getters are opt-in.",
             schema({{"reference", {{"type", "string"}, {"minLength", 1}, {"maxLength", 128}}},
                     {"member_query", {{"type", "string"}, {"maxLength", 256}}},
                     {"member_limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 250}}},
                     {"include_properties", {{"type", "boolean"}}}},
                    {"reference"}), false),
        tool("read_array", "Read managed array",
             "Page through a managed array; reference elements get new references.",
             schema({{"reference", {{"type", "string"}, {"minLength", 1}, {"maxLength", 128}}},
                     {"offset", {{"type", "integer"}, {"minimum", 0}}},
                     {"limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 250}}}},
                    {"reference"})),
        tool("snapshot_object", "Snapshot object",
             "Remember the current member values of a component or managed object. Returns only a snapshot_reference; use diff_snapshot after the game changes.",
             schema({{"reference", {{"type", "string"}, {"minLength", 1}, {"maxLength", 128}}},
                     {"member_query", {{"type", "string"}, {"maxLength", 256}}},
                     {"member_limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 250}}},
                     {"include_properties", {{"type", "boolean"}}}},
                    {"reference"}), true, false),
        tool("diff_snapshot", "Diff snapshot",
             "Compare a snapshot with the object now (or with another snapshot via against) and return only what changed. rebase makes the current state the new baseline.",
             schema({{"snapshot_reference", {{"type", "string"}, {"minLength", 1}, {"maxLength", 128}}},
                     {"against", {{"type", "string"}, {"minLength", 1}, {"maxLength", 128}}},
                     {"rebase", {{"type", "boolean"}}},
                     {"epsilon", {{"type", "number"}, {"minimum", 0}}}},
                    {"snapshot_reference"}), true, false),
        tool("decode_byte_array", "Decode byte array",
             "Decode a System.Byte[] as MessagePack, JSON, text or hex.",
             schema({{"reference", {{"type", "string"}, {"minLength", 1}, {"maxLength", 128}}},
                     {"max_bytes", {{"type", "integer"}, {"minimum", 1}, {"maximum", 65536}}}},
                    {"reference"})),
        tool("start_instance_scan", "Start instance scan",
             "Start a time-sliced search for live instances of a type.",
             schema({{"type_reference", {{"type", "string"}, {"minLength", 1}, {"maxLength", 128}}},
                     {"include_all_loaded", {{"type", "boolean"}}}},
                    {"type_reference"}), true, false),
        tool("get_instance_scan", "Get instance scan",
             "Progress and a page of results from an instance scan.",
             schema({{"scan_reference", {{"type", "string"}, {"minLength", 1}, {"maxLength", 128}}},
                     {"offset", {{"type", "integer"}, {"minimum", 0}}},
                     {"limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 100}}}},
                    {"scan_reference"})),
        tool("cancel_instance_scan", "Cancel instance scan",
             "Cancel an instance scan.",
             schema({{"scan_reference", {{"type", "string"}, {"minLength", 1}, {"maxLength", 128}}}},
                    {"scan_reference"}), true, false),
        tool("search_types", "Search managed types", "Search loaded types. Filter by image and namespace; page with offset while has_more.",
             schema({{"query", {{"type", "string"}, {"maxLength", 256}}},
                     {"image", {{"type", "string"}, {"maxLength", 256}}},
                     {"namespace", {{"type", "string"}, {"maxLength", 256}}},
                     {"limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 100}}},
                     {"offset", {{"type", "integer"}, {"minimum", 0}}}})),
        tool("search_members", "Search managed members",
             "Search fields, properties and methods across loaded types.",
             schema({{"query", {{"type", "string"}, {"minLength", 1}, {"maxLength", 256}}},
                     {"type_query", {{"type", "string"}, {"maxLength", 256}}},
                     {"kind", {{"type", "string"}, {"enum", {"any", "field", "property", "method"}}}},
                     {"type_limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 128}}},
                     {"limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 250}}},
                     {"offset", {{"type", "integer"}, {"minimum", 0}}}}, {"query"})),
        tool("inspect_type", "Inspect managed type", "Fields, properties and methods of a type.",
             schema({{"type_reference", {{"type", "string"}, {"minLength", 1}, {"maxLength", 128}}},
                     {"member_query", {{"type", "string"}, {"maxLength", 256}}},
                     {"member_limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 250}}},
                     {"member_offset", {{"type", "integer"}, {"minimum", 0}}}},
                    {"type_reference"})),
        tool("list_method_traces", "List method traces", "List method trace sessions.", schema()),
        tool("get_method_trace", "Get method trace", "Decoded calls of a trace session; repeats are grouped.",
             schema({{"trace_reference", {{"type", "string"}, {"minLength", 1}, {"maxLength", 128}}},
                     {"after_sequence", {{"type", "integer"}, {"minimum", 0}}},
                     {"limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 200}}}},
                    {"trace_reference"})),
        tool("build_call_graph", "Build call graph", "Caller-to-target graph from retained traces.",
             schema({{"trace_reference", {{"type", "string"}, {"minLength", 1}, {"maxLength", 128}}},
                     {"limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 250}}}})),
        tool("get_activity_log", "Get activity log", "Recent Explorer activity and MCP audit events.",
             schema({{"after_sequence", {{"type", "integer"}, {"minimum", 0}}},
                     {"limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 200}}}})),
        tool("build_reference_graph", "Build reference graph", "Reference graph of the current selection.", schema()),
        tool("get_watch_history", "Get watch history", "Value-watch history.",
             schema({{"watch_id", {{"type", "integer"}, {"minimum", 1}}},
                     {"event_limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 100}}}})),
        tool("export_diagnostic_bundle", "Export diagnostic bundle", "Write a diagnostic bundle and return its path.", schema(), false, false),
        tool("list_scenes", "List scenes", "Loaded and build scenes. Start here to walk the hierarchy.", schema()),
        tool("browse_hierarchy", "Browse hierarchy",
             "Walk the tree one level: a scene's roots, or an object with its ancestors and a page of children.",
             schema({{"reference", {{"type", "string"}, {"minLength", 1}, {"maxLength", 128}}},
                     {"scene", {{"type", "string"}, {"maxLength", 256}}},
                     {"limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 100}}},
                     {"offset", {{"type", "integer"}, {"minimum", 0}}}})),
        tool("select_game_object", "Select GameObject", "Select a GameObject in the overlay (UI only).",
             schema({{"reference", {{"type", "string"}, {"minLength", 1}, {"maxLength", 128}}}}, {"reference"}), false),
        tool("describe_reference", "Describe reference",
             "What a reference points to and whether it is still valid.",
             schema({{"reference", {{"type", "string"}, {"minLength", 1}, {"maxLength", 128}}}}, {"reference"})),
        tool("list_assemblies", "List assemblies", "Loaded assemblies with type counts.",
             schema({{"query", {{"type", "string"}, {"maxLength", 256}}},
                     {"limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 500}}},
                     {"offset", {{"type", "integer"}, {"minimum", 0}}}})),
        tool("list_namespaces", "List namespaces", "Namespaces with type counts.",
             schema({{"image", {{"type", "string"}, {"maxLength", 256}}},
                     {"query", {{"type", "string"}, {"maxLength", 256}}},
                     {"limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 500}}},
                     {"offset", {{"type", "integer"}, {"minimum", 0}}}})),
        tool("get_type_hierarchy", "Get type hierarchy",
             "Base chain, interfaces and derived types of a type.",
             schema({{"type_reference", {{"type", "string"}, {"minLength", 1}, {"maxLength", 128}}},
                     {"derived_limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 100}}},
                     {"derived_offset", {{"type", "integer"}, {"minimum", 0}}}}, {"type_reference"})),
        tool("read_static_members", "Read static members",
             "Static fields (and properties if permitted) of a type.",
             schema({{"type_reference", {{"type", "string"}, {"minLength", 1}, {"maxLength", 128}}},
                     {"member_query", {{"type", "string"}, {"maxLength", 256}}},
                     {"member_limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 250}}},
                     {"include_properties", {{"type", "boolean"}}}}, {"type_reference"}), false),
        tool("batch", "Batch",
             "Run up to 10 read tools in one call and get every result at once. Prefer it over separate calls.",
             schema({{"calls", {{"type", "array"}, {"minItems", 1}, {"maxItems", 10},
                                {"items", {{"type", "object"}, {"additionalProperties", false},
                                           {"required", nlohmann::json::array({"tool"})},
                                           {"properties", {{"tool", {{"type", "string"}, {"minLength", 1}, {"maxLength", 64}}},
                                                           {"arguments", {{"type", "object"}}}}}}}}}},
                    {"calls"}), false),
        tool("list_labels", "List labels",
             "Saved labels and notes. Read before re-analysing obfuscated code.",
             schema({{"query", {{"type", "string"}, {"maxLength", 256}}},
                     {"limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 500}}}}))
    });
    return tools;
}

const nlohmann::json& instrumentation_catalog() {
    static const nlohmann::json tools = nlohmann::json::array({
        tool("start_method_trace", "Start method trace", "Install a trace hook on a method reference.",
             schema({{"method_reference", {{"type", "string"}, {"minLength", 1}, {"maxLength", 128}}}},
                    {"method_reference"}), false, false, true),
        tool("stop_method_trace", "Stop method trace", "Detach a trace hook; records are kept.",
             schema({{"trace_reference", {{"type", "string"}, {"minLength", 1}, {"maxLength", 128}}}},
                    {"trace_reference"}), false, true),
        tool("clear_method_trace", "Clear method trace", "Clear a trace session's records.",
             schema({{"trace_reference", {{"type", "string"}, {"minLength", 1}, {"maxLength", 128}}}},
                    {"trace_reference"}), false, false, true)
    });
    return tools;
}

const nlohmann::json& invocation_catalog() {
    static const nlohmann::json tools = nlohmann::json::array({
        tool("invoke_method", "Invoke managed method",
             "Invoke a method on a reference with JSON arguments. Runs game code.",
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
             "Write a field or property: reference = instance, type_reference = static. member may be a name, alias or label. Returns old and new value.",
             schema({{"reference", {{"type", "string"}, {"minLength", 1}, {"maxLength", 128}}},
                     {"type_reference", {{"type", "string"}, {"minLength", 1}, {"maxLength", 128}}},
                     {"member", {{"type", "string"}, {"minLength", 1}, {"maxLength", 1024}}},
                     {"kind", {{"type", "string"}, {"enum", {"field", "property", "auto"}}}},
                     {"value", nlohmann::json::object()}}, {"member", "value"}), false, false, true),
        tool("mutate_game_object", "Mutate GameObject",
             "Rename, retag, relayer, activate, move, duplicate or destroy a GameObject.",
             schema({{"reference", {{"type", "string"}, {"minLength", 1}, {"maxLength", 128}}},
                     {"action", {{"type", "string"}, {"enum", {"rename", "set_tag", "set_layer", "set_static", "set_active", "set_position", "set_rotation", "set_scale", "duplicate", "destroy"}}}},
                     {"value", nlohmann::json::object()}}, {"reference", "action"}), false, false, true),
        tool("manage_component", "Manage component",
             "Add, remove or enable a component.",
             schema({{"action", {{"type", "string"}, {"enum", {"add", "remove", "set_enabled"}}}},
                     {"object_reference", {{"type", "string"}, {"minLength", 1}, {"maxLength", 128}}},
                     {"component_reference", {{"type", "string"}, {"minLength", 1}, {"maxLength", 128}}},
                     {"image", {{"type", "string"}, {"maxLength", 256}}},
                     {"namespace", {{"type", "string"}, {"maxLength", 256}}},
                     {"class", {{"type", "string"}, {"maxLength", 256}}},
                     {"enabled", {{"type", "boolean"}}}}, {"action"}), false, false, true),
        tool("set_label", "Set label",
             "Save a name and note for a type or member. Identify it by type plus member, or by alias (m_7c1e2a). Methods are per overload unless all_overloads.",
             schema({{"kind", {{"type", "string"}, {"enum", {"type", "field", "property", "method"}}}},
                     {"type", {{"type", "string"}, {"minLength", 1}, {"maxLength", 1024}}},
                     {"member", {{"type", "string"}, {"minLength", 1}, {"maxLength", 1024}}},
                     {"alias", {{"type", "string"}, {"minLength", 3}, {"maxLength", 16}}},
                     {"parameter_types", {{"type", "array"}, {"maxItems", 32},
                                          {"items", {{"type", "string"}, {"maxLength", 1024}}}}},
                     {"all_overloads", {{"type", "boolean"}}},
                     {"label", {{"type", "string"}, {"minLength", 1}, {"maxLength", 128}}},
                     {"note", {{"type", "string"}, {"maxLength", 2048}}}}, {"kind", "type", "label"}), false, true),
        tool("remove_label", "Remove label", "Delete a saved label (same targeting as set_label).",
             schema({{"kind", {{"type", "string"}, {"enum", {"type", "field", "property", "method"}}}},
                     {"type", {{"type", "string"}, {"minLength", 1}, {"maxLength", 1024}}},
                     {"member", {{"type", "string"}, {"minLength", 1}, {"maxLength", 1024}}},
                     {"alias", {{"type", "string"}, {"minLength", 3}, {"maxLength", 16}}},
                     {"parameter_types", {{"type", "array"}, {"maxItems", 32},
                                          {"items", {{"type", "string"}, {"maxLength", 1024}}}}},
                     {"all_overloads", {{"type", "boolean"}}}}, {"kind", "type"}), false, true),
        tool("load_scene", "Load scene",
             "Load a build scene by index or name. Discards runtime state.",
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

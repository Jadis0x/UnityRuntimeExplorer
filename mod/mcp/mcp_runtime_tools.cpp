// Copyright (c) 2026 Jadis0x. All rights reserved.
#include "mcp_runtime_tools.h"

#include "explorer/diagnostic_bundle.h"
#include "explorer/explorer_model.h"
#include "explorer/method_trace_format.h"
#include "config/mod_config.h"
#include "mcp/core/object_discovery.h"
#include "mcp/core/tool_catalog.h"
#include "managed_method_invoker.h"
#include "managed_object_reader.h"

#include <Windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <limits>
#include <map>
#include <optional>
#include <set>

namespace Explorer::Mcp {
namespace {
using Json = nlohmann::json;
using namespace URK::Unity;

constexpr std::size_t kMaxReferences = 4096;

std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return value;
}

std::uint64_t retained_method_trace_calls(const MethodTracer::Snapshot& trace) {
    std::uint64_t count = 0;
    for (const MethodTracer::Record& record : trace.records)
        count += std::max<std::uint64_t>(1, record.repeat_count);
    return count;
}

std::uint64_t collapsed_method_trace_calls(const MethodTracer::Snapshot& trace) {
    const std::uint64_t retained = retained_method_trace_calls(trace);
    return retained >= trace.records.size() ? retained - trace.records.size() : 0;
}

Json vector_json(const Vector3& value) {
    return {{"x", value.x}, {"y", value.y}, {"z", value.z}};
}

std::optional<Vector3> vector_argument(const Json& value) {
    if (value.is_array() && value.size() == 3 && value[0].is_number() &&
        value[1].is_number() && value[2].is_number())
        return Vector3{value[0].get<float>(), value[1].get<float>(), value[2].get<float>()};
    if (value.is_object() && value.contains("x") && value.contains("y") && value.contains("z") &&
        value["x"].is_number() && value["y"].is_number() && value["z"].is_number())
        return Vector3{value["x"].get<float>(), value["y"].get<float>(), value["z"].get<float>()};
    return std::nullopt;
}

const HierarchyNode* find_hierarchy_node(const HierarchyInfo& hierarchy, int instance_id) {
    const auto visit = [&](const auto& self, const HierarchyNode& node) -> const HierarchyNode* {
        if (node.instance_id == instance_id)
            return &node;
        for (const HierarchyNode& child : node.children)
            if (const HierarchyNode* found = self(self, child))
                return found;
        return nullptr;
    };
    for (const SceneNode& scene : hierarchy.scenes)
        for (const HierarchyNode& root : scene.roots)
            if (const HierarchyNode* found = visit(visit, root))
                return found;
    return nullptr;
}

std::string value_kind(Inspect::ValueKind kind) {
    switch (kind) {
    case Inspect::ValueKind::Null: return "null";
    case Inspect::ValueKind::Boolean: return "boolean";
    case Inspect::ValueKind::SignedInteger: return "signed_integer";
    case Inspect::ValueKind::UnsignedInteger: return "unsigned_integer";
    case Inspect::ValueKind::FloatingPoint: return "floating_point";
    case Inspect::ValueKind::String: return "string";
    case Inspect::ValueKind::ObjectReference: return "object_reference";
    case Inspect::ValueKind::ArrayReference: return "array_reference";
    case Inspect::ValueKind::Enum: return "enum";
    case Inspect::ValueKind::Structured: return "structured";
    case Inspect::ValueKind::ValueType: return "value_type";
    default: return "unavailable";
    }
}

std::string graph_node_kind(Snapshot::ReferenceGraph::Node::Kind kind) {
    switch (kind) {
    case Snapshot::ReferenceGraph::Node::Kind::UnityObject: return "unity_object";
    case Snapshot::ReferenceGraph::Node::Kind::GameObject: return "game_object";
    case Snapshot::ReferenceGraph::Node::Kind::Component: return "component";
    case Snapshot::ReferenceGraph::Node::Kind::Array: return "array";
    default: return "managed_object";
    }
}

std::string graph_edge_kind(Snapshot::ReferenceGraph::Edge::Kind kind) {
    switch (kind) {
    case Snapshot::ReferenceGraph::Edge::Kind::ArrayElement: return "array_element";
    case Snapshot::ReferenceGraph::Edge::Kind::Component: return "component";
    case Snapshot::ReferenceGraph::Edge::Kind::Owner: return "owner";
    case Snapshot::ReferenceGraph::Edge::Kind::BackReference: return "back_reference";
    default: return "field";
    }
}

struct RootGuard {
    Inspect::ObjectHandle handle{};
    ~RootGuard() { Inspect::FreeObjectHandle(handle); }
};

std::optional<int> requested_limit(const Json& arguments, std::string_view key, int fallback, int maximum) {
    if (!arguments.contains(key))
        return fallback;
    if (!arguments.at(key).is_number_integer())
        return std::nullopt;
    if (arguments.at(key).is_number_unsigned()) {
        const std::uint64_t value = arguments.at(key).get<std::uint64_t>();
        return value >= 1 && value <= static_cast<std::uint64_t>(maximum)
            ? std::optional<int>(static_cast<int>(value)) : std::nullopt;
    }
    const std::int64_t value = arguments.at(key).get<std::int64_t>();
    return value >= 1 && value <= maximum ? std::optional<int>(static_cast<int>(value)) : std::nullopt;
}

std::optional<std::uint64_t> optional_sequence(const Json& arguments, std::string_view key) {
    if (!arguments.contains(key))
        return 0;
    const Json& value = arguments.at(key);
    if (value.is_number_unsigned())
        return value.get<std::uint64_t>();
    if (value.is_number_integer()) {
        const std::int64_t signed_value = value.get<std::int64_t>();
        if (signed_value >= 0)
            return static_cast<std::uint64_t>(signed_value);
    }
    return std::nullopt;
}

std::optional<std::size_t> optional_offset(const Json& arguments, std::string_view key) {
    const auto value = optional_sequence(arguments, key);
    if (!value || *value > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()))
        return std::nullopt;
    return static_cast<std::size_t>(*value);
}

std::optional<std::vector<std::string>> optional_string_array(const Json& arguments,
                                                               std::string_view key,
                                                               std::size_t maximum) {
    if (!arguments.contains(key))
        return std::vector<std::string>{};
    const Json& value = arguments.at(key);
    if (!value.is_array() || value.size() > maximum)
        return std::nullopt;
    std::vector<std::string> result;
    result.reserve(value.size());
    for (const Json& item : value) {
        if (!item.is_string())
            return std::nullopt;
        std::string text = item.get<std::string>();
        if (text.empty() || text.size() > 256)
            return std::nullopt;
        result.push_back(std::move(text));
    }
    return result;
}

std::string method_signature(const ComponentInfo::Method& method) {
    std::string result = method.return_type + " " + method.declaring_type + "." + method.name + "(";
    for (std::size_t index = 0; index < method.parameter_types.size(); ++index) {
        if (index != 0)
            result += ", ";
        result += method.parameter_types[index];
        if (index < method.parameter_names.size() && !method.parameter_names[index].empty())
            result += " " + method.parameter_names[index];
    }
    return result + ")";
}

const MethodTracer::Snapshot* find_trace(const Snapshot& snapshot, std::uint64_t id) {
    const auto found = std::find_if(snapshot.method_traces.begin(), snapshot.method_traces.end(),
                                    [&](const MethodTracer::Snapshot& trace) { return trace.id == id; });
    return found == snapshot.method_traces.end() ? nullptr : &*found;
}
} // namespace

void RuntimeTools::reset() {
    references_.clear();
    reference_order_.clear();
    scene_generation_ = 0;
    hierarchy_revision_ = 0;
}

void RuntimeTools::revoke_instrumentation(RuntimeModel& model, std::string_view reason) {
    if (model.mcp_method_trace_ids_.empty())
        return;
    for (const MethodTracer::TraceId id : model.mcp_method_trace_ids_)
        MethodTracer::stop(id);
    model.mcp_method_trace_ids_.clear();
    model.record_flight("MCP", "Revoke method tracing", std::string(reason));
    model.set_status("MCP method traces stopped: " + std::string(reason));
    model.publish();
}

void RuntimeTools::synchronize_generation(const RuntimeModel& model) {
    const auto snapshot = model.snapshot();
    const std::uint64_t generation = snapshot->hierarchy ? snapshot->hierarchy->scene_generation : 0;
    const std::uint64_t revision = snapshot->hierarchy ? snapshot->hierarchy->revision : 0;
    // References validate their own lifetime. Do not invalidate every object,
    // component, type, and trace merely because an immutable hierarchy census
    // published a newer revision.
    scene_generation_ = generation;
    hierarchy_revision_ = revision;
}

void RuntimeTools::release_reference_storage(RuntimeModel& model, const Reference& reference) {
    if (reference.kind == ReferenceKind::ManagedObject && reference.graph_token != 0)
        model.release_reference_handle(reference.graph_token);
}

std::string RuntimeTools::issue_reference(RuntimeModel& model, Reference reference) {
    while (references_.size() >= kMaxReferences && !reference_order_.empty()) {
        const auto found = references_.find(reference_order_.front());
        if (found != references_.end()) {
            release_reference_storage(model, found->second);
            references_.erase(found);
        }
        reference_order_.pop_front();
    }
    std::array<unsigned char, 16> random{};
    if (BCryptGenRandom(nullptr, random.data(), static_cast<ULONG>(random.size()),
                        BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
        return {};
    static constexpr char hex[] = "0123456789abcdef";
    std::string token = "urkref_";
    token.reserve(7 + random.size() * 2);
    for (const unsigned char byte : random) {
        token.push_back(hex[byte >> 4]);
        token.push_back(hex[byte & 0x0f]);
    }
    references_[token] = std::move(reference);
    reference_order_.push_back(token);
    return token;
}

const RuntimeTools::Reference* RuntimeTools::find_reference(std::string_view token, ReferenceKind kind,
                                                            const RuntimeModel& model,
                                                            ReferenceLookup* lookup) const {
    const auto report = [&](ReferenceLookup value) {
        if (lookup)
            *lookup = value;
    };
    const auto found = references_.find(std::string(token));
    if (found == references_.end()) {
        report(ReferenceLookup::Unknown);
        return nullptr;
    }
    if (found->second.kind != kind) {
        report(ReferenceLookup::WrongKind);
        return nullptr;
    }
    const bool scene_bound = kind == ReferenceKind::GameObject || kind == ReferenceKind::Component ||
        kind == ReferenceKind::GraphNode;
    const bool revision_bound = kind == ReferenceKind::GraphNode;
    const auto snapshot = model.snapshot();
    if ((scene_bound || revision_bound) && !snapshot->hierarchy) {
        report(ReferenceLookup::Expired);
        return nullptr;
    }
    if ((scene_bound && found->second.scene_generation != snapshot->hierarchy->scene_generation) ||
        (revision_bound && found->second.hierarchy_revision != snapshot->hierarchy->revision)) {
        report(ReferenceLookup::Expired);
        return nullptr;
    }
    report(ReferenceLookup::Found);
    return &found->second;
}

Response RuntimeTools::execute(RuntimeModel& model, const Request& request) {
    synchronize_generation(model);
    const bool instrumentation = is_instrumentation_tool(request.tool);
    const bool invocation = is_invocation_tool(request.tool);
    const bool mutation = is_write_tool(request.tool);
    auto fail = [&](std::string code, std::string message) {
        model.record_flight("MCP_ERROR", request.tool, code);
        model.publish();
        return failure(request.id, std::move(code), std::move(message));
    };
    auto succeed = [&](Json result) {
        model.record_flight("MCP", request.tool, invocation ? "invocation" :
                            instrumentation ? "instrumentation" : mutation ? "mutation" : "read-only");
        model.publish();
        return Response{request.id, true, std::move(result)};
    };
    if (!has_capability(request.context.capabilities, Capability::Read))
        return fail("mcp_disabled", "MCP access is disabled in the Explorer Config tab.");
    if (!is_available_tool(request.tool, true, true, true))
        return fail("tool_not_allowed", "The requested tool is not part of the MCP tool surface.");
    if ((mutation || request.tool == "export_diagnostic_bundle") &&
        !has_capability(request.context.capabilities, Capability::Write))
        return fail("write_permission_required", "Enable 'Allow managed member and object writes' in Explorer Config.");
    if (instrumentation) {
        if (!has_capability(request.context.capabilities, Capability::Trace))
            return fail("game_permission_required", "Enable 'Allow MCP method tracing' in the Explorer Config tab.");
    }
    if (invocation) {
        if (!has_capability(request.context.capabilities, Capability::Invoke))
            return fail("game_permission_required", "Enable 'Allow MCP method invocation' in the Explorer Config tab.");
    }
    if ((request.tool == "read_member" && request.arguments.value("kind", std::string("auto")) == "property") ||
        (request.tool == "inspect_managed_object" && request.arguments.value("include_properties", false))) {
        if (!has_capability(request.context.capabilities, Capability::PropertyAccess))
            return fail("property_permission_required", "Enable 'Allow managed property getters' in Explorer Config.");
    }

    const auto snapshot = model.snapshot();
    const auto hierarchy = snapshot->hierarchy;
    const auto object_reference = [&](int instance_id, std::string expected_name = {}) {
        Reference reference{ReferenceKind::GameObject, instance_id, 0, 0, scene_generation_, hierarchy_revision_};
        reference.expected_object_name = std::move(expected_name);
        return issue_reference(model, std::move(reference));
    };
    const auto capture_managed_reference = [&](Object object, std::string_view source,
                                               bool retain_strongly = false) -> std::string {
        if (!object)
            return {};
        clear_error();
        const Inspect::TypeInfo type = Inspect::TypeOf(object);
        if (!type.handle)
            return {};
        Inspect::ObjectHandle handle = type.is_value_type || retain_strongly
            ? Inspect::PinObject(object) : Inspect::WeakObject(object);
        if (!handle.handle)
            return {};
        std::uint64_t internal_token = 0;
        do {
            internal_token = 0xb000000000000000ull |
                (model.next_reference_token_++ & 0x0fffffffffffffffull);
        } while (model.reference_handles_.contains(internal_token));
        model.reference_handles_[internal_token] = handle;
        Reference reference{ReferenceKind::ManagedObject, 0, 0, internal_token,
                            scene_generation_, hierarchy_revision_};
        reference.expected_component_type = type.full_name;
        reference.expected_object_name = std::string(source);
        const std::string token = issue_reference(model, std::move(reference));
        if (token.empty())
            model.release_reference_handle(internal_token);
        return token;
    };
    const auto value_json = [&](Inspect::ValueInfo& value, std::string_view source,
                                bool retain_strongly = false) {
        Json result{{"type", value.type_name}, {"kind", value_kind(value.kind)},
                    {"display", value.display}, {"readable", value.readable}};
        switch (value.kind) {
        case Inspect::ValueKind::Null:
            result["value"] = nullptr;
            break;
        case Inspect::ValueKind::Boolean:
            result["value"] = value.bool_value;
            break;
        case Inspect::ValueKind::SignedInteger:
            result["value"] = value.signed_value;
            break;
        case Inspect::ValueKind::UnsignedInteger:
            result["value"] = value.unsigned_value;
            break;
        case Inspect::ValueKind::FloatingPoint:
            result["value"] = value.floating_value;
            break;
        case Inspect::ValueKind::String:
        case Inspect::ValueKind::Enum:
            result["value"] = value.display;
            break;
        case Inspect::ValueKind::Structured:
            result["components"] = Json::array();
            for (std::size_t index = 0; index < value.component_count; ++index)
                result["components"].push_back(value.components[index]);
            break;
        case Inspect::ValueKind::ArrayReference:
            result["array_length"] = value.array_length;
            [[fallthrough]];
        case Inspect::ValueKind::ObjectReference:
        case Inspect::ValueKind::ValueType:
            if (value.object) {
                const std::string token = capture_managed_reference(Object{value.object}, source,
                                                                    retain_strongly);
                if (!token.empty())
                    result["reference"] = token;
                else
                    result["reference_error"] = "The managed value could not be tracked safely.";
            }
            break;
        default:
            break;
        }
        value.object = nullptr;
        return result;
    };

    const auto resolve_target_reference = [&](std::string_view token, RootGuard& owner_root,
                                              std::string& code, std::string& message) -> Object {
        const auto raw = references_.find(std::string(token));
        if (raw == references_.end()) {
            code = "invalid_reference";
            message = "The opaque reference is unknown or was evicted from the bounded reference store.";
            return {};
        }
        if (raw->second.kind == ReferenceKind::ManagedObject) {
            const auto handle = model.reference_handles_.find(raw->second.graph_token);
            if (handle == model.reference_handles_.end()) {
                code = "reference_expired";
                message = "The managed reference was released by the runtime.";
                return {};
            }
            const Object object = Inspect::ResolveObjectHandle(handle->second);
            if (!object) {
                code = "reference_collected";
                message = "The managed object is no longer reachable and its weak reference was collected.";
                return {};
            }
            const Inspect::TypeInfo type = Inspect::TypeOf(object);
            if (!raw->second.expected_component_type.empty() && type.full_name != raw->second.expected_component_type) {
                code = "object_identity_changed";
                message = "The managed reference no longer resolves to the expected runtime type.";
                return {};
            }
            return object;
        }
        if (raw->second.kind != ReferenceKind::Component) {
            code = "reference_kind_mismatch";
            message = "The supplied opaque reference does not identify a component or managed object.";
            return {};
        }
        ReferenceLookup lookup = ReferenceLookup::Found;
        const Reference* reference = find_reference(token, ReferenceKind::Component, model, &lookup);
        if (!reference) {
            code = lookup == ReferenceLookup::Expired ? "reference_expired" : "invalid_reference";
            message = lookup == ReferenceLookup::Expired
                ? "The component reference belongs to an earlier scene generation."
                : "The component reference is no longer valid.";
            return {};
        }
        const GameObject owner = model.resolve_live_game_object(reference->owner_instance_id,
                                                                 owner_root.handle);
        if (!owner || (!reference->expected_object_name.empty() &&
                       owner.name() != reference->expected_object_name)) {
            code = "object_unavailable";
            message = "The component owner is unavailable or changed identity.";
            return {};
        }
        for (const Object& component : owner.GetComponentsRooted<Object>()) {
            if (!component || component.GetInstanceID() != reference->instance_id)
                continue;
            const Inspect::TypeInfo type = Inspect::TypeOf(component);
            if (!reference->expected_component_type.empty() &&
                reference->expected_component_type != "<unknown component>" &&
                !type.full_name.empty() && type.full_name != reference->expected_component_type) {
                code = "component_identity_changed";
                message = "The component instance ID now identifies a different runtime type.";
                return {};
            }
            return component;
        }
        code = "component_unavailable";
        message = "The referenced component is no longer attached to its owner.";
        return {};
    };

    if (request.tool == "runtime_status") {
        const std::size_t active_traces = static_cast<std::size_t>(std::count_if(
            snapshot->method_traces.begin(), snapshot->method_traces.end(),
            [](const MethodTracer::Snapshot& trace) { return trace.active; }));
        return succeed({{"connected", true},
                        {"mcp_enabled", ModConfig::enable_mcp.load()},
                        {"read_only", !ModConfig::enable_mcp_writes.load() &&
                                      !ModConfig::enable_mcp_tracing.load() &&
                                      !ModConfig::enable_mcp_invocation.load()},
                        {"mcp_auto_discovery_enabled", ModConfig::enable_mcp_auto_discovery.load()},
                        {"mcp_property_access_enabled", ModConfig::enable_mcp_property_access.load()},
                        {"mcp_writes_enabled", ModConfig::enable_mcp_writes.load()},
                        {"mcp_tracing_enabled_in_game", ModConfig::enable_mcp_tracing.load()},
                        {"mcp_invocation_enabled_in_game", ModConfig::enable_mcp_invocation.load()},
                        {"mcp_destructive_operations_enabled", ModConfig::enable_mcp_destructive_operations.load()},
                        {"active_method_traces", active_traces}, {"retained_method_traces", snapshot->method_traces.size()},
                        {"backend", snapshot->runtime_backend},
                        {"unity_version", snapshot->unity_version}, {"runtime_capabilities", snapshot->runtime_capabilities},
                        {"status", snapshot->status}, {"snapshot_revision", snapshot->revision},
                        {"scene_generation", hierarchy ? hierarchy->scene_generation : 0},
                        {"hierarchy_revision", hierarchy ? hierarchy->revision : 0},
                        {"scene_count", hierarchy ? hierarchy->scenes.size() : 0},
                        {"object_count", hierarchy ? hierarchy->objects : 0},
                        {"discovery_signature_failures", hierarchy ? hierarchy->discovery_signature_failures : 0},
                        {"selected", snapshot->selected_instance_id != 0},
                        {"managed_used_bytes", snapshot->managed_used_bytes},
                        {"managed_heap_bytes", snapshot->managed_heap_bytes},
                        {"diagnostic_count", snapshot->diagnostics.size()}});
    }

    if (request.tool == "discover_runtime") {
        if (!has_capability(request.context.capabilities, Capability::AutoDiscovery))
            return fail("discovery_permission_required",
                        "Enable 'Allow automatic runtime discovery' in Explorer Config.");
        const auto limit = requested_limit(request.arguments, "limit", 25, 100);
        if (!limit)
            return fail("invalid_arguments", "limit must be an integer between 1 and 100.");
        const std::string object_query = lowercase(request.arguments.value("query", std::string{}));
        const std::string type_query = lowercase(request.arguments.value("type_query", object_query));
        Json objects = Json::array();
        if (hierarchy) {
            const auto visit = [&](const auto& self, const HierarchyNode& node, const SceneNode& scene,
                                   const std::string& parent_path) -> void {
                if (objects.size() >= static_cast<std::size_t>(*limit))
                    return;
                const std::string path = parent_path.empty() ? node.name : parent_path + "/" + node.name;
                std::string searchable = node.name + " " + path + " " + node.tag;
                for (const std::string& type : node.component_types)
                    searchable += " " + type;
                for (const std::string& type : node.dynamic_behaviour_types)
                    searchable += " " + type;
                if (object_query.empty() || lowercase(std::move(searchable)).find(object_query) != std::string::npos) {
                    const std::string reference = object_reference(node.instance_id, node.name);
                    if (!reference.empty())
                        objects.push_back({{"reference", reference}, {"name", node.name}, {"path", path},
                                           {"scene", scene.name}, {"tag", node.tag}, {"active", node.active},
                                           {"components", node.component_types},
                                           {"dynamic_behaviour_types", node.dynamic_behaviour_types}});
                }
                for (const HierarchyNode& child : node.children)
                    self(self, child, scene, path);
            };
            for (const SceneNode& scene : hierarchy->scenes)
                for (const HierarchyNode& root : scene.roots)
                    visit(visit, root, scene, {});
        }
        if (!model.class_browser_catalog_)
            model.load_class_browser_catalog();
        Json types = Json::array();
        if (model.class_browser_catalog_) {
            for (const BrowserClassInfo& type : model.class_browser_catalog_->classes) {
                if (types.size() >= static_cast<std::size_t>(*limit))
                    break;
                const std::string searchable = lowercase(type.full_name + " " + type.image);
                if (!type_query.empty() && searchable.find(type_query) == std::string::npos)
                    continue;
                Reference reference{ReferenceKind::ManagedType, 0, 0, 0, scene_generation_, hierarchy_revision_};
                reference.image = type.image;
                reference.namespc = type.namespc;
                reference.class_name = type.class_name;
                const std::string token = issue_reference(model, std::move(reference));
                if (!token.empty())
                    types.push_back({{"type_reference", token}, {"full_name", type.full_name},
                                     {"image", type.image}, {"is_component", type.is_component},
                                     {"is_unity_object", type.is_unity_object}});
            }
        }
        return succeed({{"objects", std::move(objects)}, {"types", std::move(types)},
                        {"query", object_query}, {"type_query", type_query},
                        {"indexed_types", model.class_browser_catalog_ ? model.class_browser_catalog_->classes.size() : 0},
                        {"hierarchy_objects", hierarchy ? hierarchy->objects : 0},
                        {"next", "Inspect returned references; use inspect_type to discover methods and fields, and start_instance_scan to locate managed instances."}});
    }

    if (request.tool == "hierarchy_search") {
        if (!hierarchy)
            return fail("runtime_unavailable", "The Explorer hierarchy is not available yet.");
        if (request.arguments.contains("query") &&
            (!request.arguments["query"].is_string() ||
             request.arguments["query"].get_ref<const std::string&>().size() > 256))
            return fail("invalid_arguments", "query must be a string no longer than 256 characters.");
        if (request.arguments.contains("active_only") && !request.arguments["active_only"].is_boolean())
            return fail("invalid_arguments", "active_only must be a boolean.");
        const std::string query = lowercase(request.arguments.value("query", std::string{}));
        const auto limit = requested_limit(request.arguments, "limit", 25, 100);
        if (!limit)
            return fail("invalid_arguments", "limit must be an integer between 1 and 100.");
        const bool active_only = request.arguments.value("active_only", false);
        Json matches = Json::array();
        bool reference_failed = false;
        const auto visit = [&](const auto& self, const HierarchyNode& node, const SceneNode& scene,
                               const std::string& parent_path) -> void {
            if (matches.size() >= static_cast<std::size_t>(*limit))
                return;
            const std::string path = parent_path.empty() ? node.name : parent_path + "/" + node.name;
            std::string haystack = node.name + " " + path + " " + node.tag + " " +
                std::to_string(node.instance_id);
            for (const std::string& type : node.component_types)
                haystack += " " + type;
            for (const std::string& type : node.dynamic_behaviour_types)
                haystack += " " + type;
            haystack = lowercase(std::move(haystack));
            if ((!active_only || node.active) && (query.empty() || haystack.find(query) != std::string::npos)) {
                const std::string token = object_reference(node.instance_id, node.name);
                if (token.empty()) {
                    reference_failed = true;
                    return;
                }
                matches.push_back({{"reference", token}, {"instance_id", node.instance_id}, {"name", node.name},
                                   {"tag", node.tag}, {"active", node.active}, {"scene", scene.name}, {"path", path}});
            }
            for (const HierarchyNode& child : node.children)
                self(self, child, scene, path);
        };
        for (const SceneNode& scene : hierarchy->scenes)
            for (const HierarchyNode& root : scene.roots)
                visit(visit, root, scene, {});
        if (reference_failed)
            return fail("reference_unavailable", "A secure object reference could not be generated.");
        return succeed({{"matches", std::move(matches)}, {"limit", *limit},
                        {"hierarchy_revision", hierarchy->revision}});
    }

    if (request.tool == "find_game_objects") {
        if (!hierarchy)
            return fail("runtime_unavailable", "The Explorer hierarchy is not available yet.");
        for (const std::string_view key : {"query", "scene", "role"}) {
            if (request.arguments.contains(key) &&
                (!request.arguments[key].is_string() ||
                 request.arguments[key].get_ref<const std::string&>().size() > 256))
                return fail("invalid_arguments", std::string(key) + " must be a string no longer than 256 characters.");
        }
        if (request.arguments.contains("active_only") && !request.arguments["active_only"].is_boolean())
            return fail("invalid_arguments", "active_only must be a boolean.");
        const std::string role = lowercase(request.arguments.value("role", std::string{}));
        if (!role.empty() && role != "player")
            return fail("invalid_arguments", "role must be player when provided.");
        const auto limit = requested_limit(request.arguments, "limit", 25, 100);
        const auto required_components = optional_string_array(request.arguments, "required_components", 16);
        const auto required_dynamic = optional_string_array(request.arguments, "required_dynamic_behaviours", 16);
        if (!limit || !required_components || !required_dynamic)
            return fail("invalid_arguments", "limit must be 1..100 and component filters must contain at most sixteen non-empty strings.");

        std::vector<Discovery::ObjectCandidate> candidates;
        candidates.reserve(hierarchy->objects);
        const auto visit = [&](const auto& self, const HierarchyNode& node, const SceneNode& scene,
                               const std::string& parent_path) -> void {
            const std::string path = parent_path.empty() ? node.name : parent_path + "/" + node.name;
            candidates.push_back({node.instance_id, node.name, path, scene.name, node.tag, node.active,
                                  node.component_types, node.dynamic_behaviour_types,
                                  node.discovery_signature_complete});
            for (const HierarchyNode& child : node.children)
                self(self, child, scene, path);
        };
        for (const SceneNode& scene : hierarchy->scenes)
            for (const HierarchyNode& root : scene.roots)
                visit(visit, root, scene, {});

        Discovery::Query query{};
        query.text = request.arguments.value("query", std::string{});
        query.scene = request.arguments.value("scene", std::string{});
        query.role = role;
        query.active_only = request.arguments.value("active_only", false);
        query.required_components = *required_components;
        query.required_dynamic_behaviours = *required_dynamic;
        query.limit = static_cast<std::size_t>(*limit);
        const Discovery::SearchResult ranked = Discovery::rank(candidates, query);

        Json matches = Json::array();
        for (const Discovery::RankedObject& match : ranked.matches) {
            const Discovery::ObjectCandidate& candidate = candidates[match.candidate_index];
            const std::string token = object_reference(candidate.instance_id, std::string(candidate.name));
            if (token.empty())
                return fail("reference_unavailable", "A secure object reference could not be generated.");
            const std::vector<std::string> component_types(candidate.component_types.begin(),
                                                           candidate.component_types.end());
            const std::vector<std::string> dynamic_types(candidate.dynamic_behaviour_types.begin(),
                                                         candidate.dynamic_behaviour_types.end());
            matches.push_back({{"reference", token}, {"instance_id", candidate.instance_id},
                               {"name", candidate.name}, {"path", candidate.path},
                               {"scene", candidate.scene}, {"tag", candidate.tag},
                               {"active", candidate.active}, {"score", match.score},
                               {"confidence", Discovery::confidence_label(match.score)},
                               {"reasons", match.reasons}, {"components", component_types},
                               {"dynamic_behaviour_types", dynamic_types},
                               {"signature_complete", candidate.signature_complete}});
        }
        return succeed({{"matches", std::move(matches)}, {"ambiguous", ranked.ambiguous},
                        {"scanned", ranked.scanned}, {"eligible", ranked.eligible},
                        {"incomplete_signatures", ranked.incomplete_signatures},
                        {"hierarchy_revision", hierarchy->revision},
                        {"interpretation", ranked.matches.empty()
                            ? "No candidate matched the supplied evidence; this does not prove that the conceptual object does not exist."
                            : ranked.ambiguous
                                ? "Multiple candidates have comparable evidence; inspect or observe them before choosing one."
                                : "The first candidate has the strongest available evidence."}});
    }

    if (request.tool == "get_selected_object") {
        if (snapshot->selected_instance_id == 0 || !snapshot->inspector.valid)
            return succeed({{"selected", false}});
        const std::string token = object_reference(snapshot->selected_instance_id, snapshot->inspector.name);
        if (token.empty())
            return fail("reference_unavailable", "A secure object reference could not be generated.");
        return succeed({{"selected", true}, {"reference", token}, {"instance_id", snapshot->inspector.instance_id},
                        {"name", snapshot->inspector.name}, {"tag", snapshot->inspector.tag},
                        {"active", snapshot->inspector.active}, {"type", snapshot->inspector.type_name}});
    }

    ReferenceLookup requested_object_lookup = ReferenceLookup::Found;
    auto resolve_requested_object = [&]() -> std::optional<Reference> {
        if (request.arguments.contains("reference")) {
            if (!request.arguments["reference"].is_string()) {
                requested_object_lookup = ReferenceLookup::WrongKind;
                return std::nullopt;
            }
            const Reference* reference = find_reference(request.arguments["reference"].get<std::string>(),
                                                        ReferenceKind::GameObject, model,
                                                        &requested_object_lookup);
            return reference ? std::optional<Reference>(*reference) : std::nullopt;
        }
        if (snapshot->selected_instance_id == 0) {
            requested_object_lookup = ReferenceLookup::Unknown;
            return std::nullopt;
        }
        Reference reference{ReferenceKind::GameObject, snapshot->selected_instance_id, 0, 0,
                            scene_generation_, hierarchy_revision_};
        reference.expected_object_name = snapshot->inspector.name;
        return reference;
    };

    if (request.tool == "inspect_game_object" || request.tool == "list_components") {
        const auto reference = resolve_requested_object();
        if (!reference) {
            if (requested_object_lookup == ReferenceLookup::Expired)
                return fail("reference_expired", "The GameObject reference belongs to an earlier scene generation. Search again to obtain a current reference.");
            if (requested_object_lookup == ReferenceLookup::WrongKind)
                return fail("reference_kind_mismatch", "The supplied opaque reference does not identify a GameObject.");
            return fail("invalid_reference", "Provide a known opaque GameObject reference or select an object in Explorer.");
        }
        RootGuard root;
        const GameObject object = model.resolve_live_game_object(reference->instance_id, root.handle);
        if (!object)
            return fail("object_unavailable", "The referenced GameObject is no longer available.");
        const std::string live_object_name = object.name();
        if (!reference->expected_object_name.empty() && live_object_name != reference->expected_object_name)
            return fail("object_identity_changed", "The instance ID now resolves to a different GameObject. Search again before inspecting it.");
        Json components = Json::array();
        clear_error();
        const auto rooted_components = object.GetComponentsRooted<Object>();
        for (const Object& component : rooted_components) {
            if (!component)
                continue;
            const int component_id = component.GetInstanceID();
            if (component_id == 0)
                continue;
            const auto* component_class = static_cast<const URK::managed::Class*>(
                URK::managed::object_get_class(static_cast<URK::managed::Object*>(component.handle())));
            const Inspect::TypeInfo type = component_class ? Inspect::DescribeClass(component_class) : Inspect::TypeInfo{};
            Reference component_reference{ReferenceKind::Component, component_id,
                reference->instance_id, 0, scene_generation_, hierarchy_revision_};
            component_reference.expected_object_name = live_object_name;
            component_reference.expected_component_type =
                type.full_name.empty() ? "<unknown component>" : type.full_name;
            const std::string token = issue_reference(model, std::move(component_reference));
            if (token.empty())
                return fail("reference_unavailable", "A secure component reference could not be generated.");
            components.push_back({{"component_reference", token}, {"instance_id", component_id},
                                  {"type", type.full_name.empty() ? "<unknown component>" : type.full_name},
                                  {"namespace", type.namespc}, {"class", type.name}});
        }
        if (const char* error = last_error(); error && error[0]) {
            clear_error();
            return fail("component_query_failed", "Unity could not safely enumerate components for this object.");
        }
        clear_error();
        const std::string current_object_reference = object_reference(reference->instance_id, live_object_name);
        if (current_object_reference.empty())
            return fail("reference_unavailable", "A secure object reference could not be generated.");
        if (request.tool == "list_components") {
            const std::string object_name = live_object_name;
            if (const char* error = last_error(); error && error[0]) {
                clear_error();
                return fail("object_read_failed", "Unity could not safely read the component owner.");
            }
            return succeed({{"object_reference", current_object_reference},
                            {"object_name", object_name}, {"components", std::move(components)}});
        }
        const Transform transform = object.transform();
        const HierarchyNode* node = hierarchy ? find_hierarchy_node(*hierarchy, reference->instance_id) : nullptr;
        Json result{{"reference", current_object_reference},
                    {"instance_id", reference->instance_id}, {"name", live_object_name}, {"tag", object.tag()},
                    {"active", object.activeSelf()}, {"layer", object.GetProperty<int>("layer")},
                    {"is_static", object.GetProperty<bool>("isStatic")}, {"components", std::move(components)}};
        if (node)
            result["hierarchy_active"] = node->active;
        if (transform) {
            result["local_position"] = vector_json(transform.localPosition());
            result["local_rotation"] = vector_json(transform.GetProperty<Vector3>("localEulerAngles"));
            result["local_scale"] = vector_json(transform.localScale());
        }
        if (const char* error = last_error(); error && error[0]) {
            clear_error();
            return fail("object_read_failed", "Unity could not safely read all requested GameObject properties.");
        }
        return succeed(std::move(result));
    }

    if (request.tool == "read_member") {
        if (!request.arguments.contains("component_reference") || !request.arguments["component_reference"].is_string() ||
            !request.arguments.contains("member") || !request.arguments["member"].is_string())
            return fail("invalid_arguments", "component_reference and member are required strings.");
        ReferenceLookup component_lookup = ReferenceLookup::Found;
        const Reference* reference = find_reference(request.arguments["component_reference"].get<std::string>(),
                                                    ReferenceKind::Component, model, &component_lookup);
        if (!reference) {
            if (component_lookup == ReferenceLookup::Expired)
                return fail("reference_expired", "The component reference belongs to an earlier scene generation. Inspect its owner again.");
            if (component_lookup == ReferenceLookup::WrongKind)
                return fail("reference_kind_mismatch", "The supplied opaque reference does not identify a component.");
            return fail("invalid_reference", "The component reference is unknown or was evicted from the bounded reference store.");
        }
        RootGuard root;
        const GameObject owner = model.resolve_live_game_object(reference->owner_instance_id, root.handle);
        if (!owner)
            return fail("object_unavailable", "The component owner is no longer available.");
        if (!reference->expected_object_name.empty() && owner.name() != reference->expected_object_name)
            return fail("object_identity_changed", "The component owner instance ID now identifies a different GameObject.");
        Object target;
        const auto components = owner.GetComponentsRooted<Object>();
        for (const Object& component : components)
            if (component && component.GetInstanceID() == reference->instance_id) {
                target = component;
                break;
            }
        if (!target)
            return fail("component_unavailable", "The referenced component is no longer available.");
        if (!reference->expected_component_type.empty() && reference->expected_component_type != "<unknown component>") {
            const auto* target_class = static_cast<const URK::managed::Class*>(
                URK::managed::object_get_class(static_cast<URK::managed::Object*>(target.handle())));
            const Inspect::TypeInfo target_type = target_class ? Inspect::DescribeClass(target_class) : Inspect::TypeInfo{};
            if (!target_type.full_name.empty() && target_type.full_name != reference->expected_component_type)
                return fail("component_identity_changed", "The component instance ID now identifies a different component type.");
        }
        const std::string member_name = request.arguments["member"].get<std::string>();
        if (member_name.empty() || member_name.size() > 256)
            return fail("invalid_arguments", "member must contain between 1 and 256 characters.");
        if (request.arguments.contains("kind") && !request.arguments["kind"].is_string())
            return fail("invalid_arguments", "kind must be field, property, or auto.");
        const std::string kind = request.arguments.value("kind", std::string("auto"));
        if (kind != "field" && kind != "property" && kind != "auto")
            return fail("invalid_arguments", "kind must be field, property, or auto.");
        const auto* klass = static_cast<const URK::managed::Class*>(
            URK::managed::object_get_class(static_cast<URK::managed::Object*>(target.handle())));
        if (!klass)
            return fail("metadata_unavailable", "The component runtime class is unavailable.");
        Inspect::ValueInfo value{};
        std::string resolved_kind;
        clear_error();
        if (kind == "field" || kind == "auto") {
            const auto fields = Inspect::fields_from_class(klass, true);
            if (const auto found = std::find_if(fields.begin(), fields.end(), [&](const Inspect::FieldInfo& field) {
                    return field.name == member_name;
                }); found != fields.end()) {
                if (found->type_is_opaque)
                    return fail("member_unsafe", "The field has a runtime-specific opaque type and cannot be sampled safely.");
                value = Inspect::ReadField(target, *found);
                resolved_kind = "field";
            }
        }
        if (resolved_kind.empty() && (kind == "property" || kind == "auto")) {
            if (!has_capability(request.context.capabilities, Capability::PropertyAccess))
                return fail("property_permission_required",
                            "Enable managed property getters in Explorer Config.");
            const auto properties = Inspect::properties_from_class(klass, true);
            if (const auto found = std::find_if(properties.begin(), properties.end(), [&](const Inspect::PropertyInfo& property) {
                    return property.name == member_name;
                }); found != properties.end()) {
                if (!found->can_read || found->type_is_opaque)
                    return fail("member_unsafe", "The property is not safely readable through runtime metadata.");
                value = Inspect::ReadProperty(target, *found);
                resolved_kind = "property";
            }
        }
        if (resolved_kind.empty())
        {
            clear_error();
            return fail("member_not_found", "No matching field or readable property was found.");
        }
        Json result = value_json(value, reference->expected_component_type + "." + member_name);
        result["member"] = member_name;
        result["member_kind"] = resolved_kind;
        clear_error();
        return succeed(std::move(result));
    }

    if (request.tool == "inspect_managed_object") {
        if (!request.arguments.contains("reference") || !request.arguments["reference"].is_string())
            return fail("invalid_arguments", "reference is required.");
        if (request.arguments.contains("member_query") &&
            (!request.arguments["member_query"].is_string() ||
             request.arguments["member_query"].get_ref<const std::string&>().size() > 256))
            return fail("invalid_arguments", "member_query must be a string no longer than 256 characters.");
        if (request.arguments.contains("include_properties") &&
            !request.arguments["include_properties"].is_boolean())
            return fail("invalid_arguments", "include_properties must be a boolean.");
        const auto limit = requested_limit(request.arguments, "member_limit", 100, 250);
        if (!limit)
            return fail("invalid_arguments", "member_limit must be an integer between 1 and 250.");
        RootGuard owner_root;
        std::string code;
        std::string message;
        const Object target = resolve_target_reference(
            request.arguments["reference"].get_ref<const std::string&>(), owner_root, code, message);
        if (!target)
            return fail(std::move(code), std::move(message));
        const bool include_properties = request.arguments.value("include_properties", false);
        const ManagedRead::ObjectPage page = ManagedRead::read_object(
            target, request.arguments.value("member_query", std::string{}),
            static_cast<std::size_t>(*limit), include_properties);
        if (!page.type.handle)
            return fail("metadata_unavailable", "The managed object's runtime type could not be described.");
        if (Inspect::type_name_looks_array(page.type.full_name))
            return fail("array_requires_paging", "Use read_array for managed arrays.");
        Json fields = Json::array();
        for (ManagedRead::MemberValue member : page.fields) {
            Json entry = value_json(member.value, page.type.full_name + "." + member.name);
            entry["name"] = member.name;
            entry["declaring_type"] = member.declaring_type;
            entry["static"] = member.is_static;
            entry["runtime_safe"] = member.runtime_safe;
            if (!member.capability_reason.empty())
                entry["capability_reason"] = member.capability_reason;
            fields.push_back(std::move(entry));
        }
        Json properties = Json::array();
        for (ManagedRead::MemberValue member : page.properties) {
            Json entry = value_json(member.value, page.type.full_name + "." + member.name);
            entry["name"] = member.name;
            entry["declaring_type"] = member.declaring_type;
            entry["static"] = member.is_static;
            entry["runtime_safe"] = member.runtime_safe;
            if (!member.capability_reason.empty())
                entry["capability_reason"] = member.capability_reason;
            properties.push_back(std::move(entry));
        }
        return succeed({{"type", {{"assembly", page.assembly}, {"namespace", page.type.namespc},
                                    {"name", page.type.name}, {"full_name", page.type.full_name},
                                    {"is_value_type", page.type.is_value_type},
                                    {"is_enum", page.type.is_enum}}},
                        {"fields", std::move(fields)}, {"properties", std::move(properties)},
                        {"properties_executed", include_properties},
                        {"matching_members", page.matching_members}, {"truncated", page.truncated},
                        {"member_limit", *limit}});
    }

    if (request.tool == "read_array") {
        if (!request.arguments.contains("reference") || !request.arguments["reference"].is_string())
            return fail("invalid_arguments", "reference is required.");
        const auto offset = optional_offset(request.arguments, "offset");
        const auto limit = requested_limit(request.arguments, "limit", 100, 250);
        if (!offset || !limit)
            return fail("invalid_arguments", "offset must be non-negative and limit must be between 1 and 250.");
        RootGuard owner_root;
        std::string code;
        std::string message;
        const Object target = resolve_target_reference(
            request.arguments["reference"].get_ref<const std::string&>(), owner_root, code, message);
        if (!target)
            return fail(std::move(code), std::move(message));
        const ManagedRead::ArrayPage page = ManagedRead::read_array(target, *offset,
                                                                    static_cast<std::size_t>(*limit));
        if (!Inspect::type_name_looks_array(page.type.full_name))
            return fail("reference_kind_mismatch", "The managed reference does not identify an array.");
        Json values = Json::array();
        for (std::size_t index = 0; index < page.values.size(); ++index) {
            Inspect::ValueInfo value = page.values[index];
            Json entry = value_json(value, page.type.full_name + "[" +
                std::to_string(page.offset + index) + "]");
            entry["index"] = page.offset + index;
            values.push_back(std::move(entry));
        }
        return succeed({{"type", page.type.full_name}, {"element_type", page.element_type},
                        {"length", page.length}, {"offset", page.offset},
                        {"values", std::move(values)}, {"has_more", page.has_more}});
    }

    if (request.tool == "decode_byte_array") {
        if (!request.arguments.contains("reference") || !request.arguments["reference"].is_string())
            return fail("invalid_arguments", "reference is required.");
        const auto maximum = requested_limit(request.arguments, "max_bytes", 16384, 65536);
        if (!maximum)
            return fail("invalid_arguments", "max_bytes must be an integer between 1 and 65536.");
        RootGuard owner_root;
        std::string code;
        std::string message;
        const Object target = resolve_target_reference(
            request.arguments["reference"].get_ref<const std::string&>(), owner_root, code, message);
        if (!target)
            return fail(std::move(code), std::move(message));
        ManagedRead::ByteSnapshot bytes = ManagedRead::read_bytes(target,
                                                                  static_cast<std::size_t>(*maximum));
        if (!bytes.error.empty())
            return fail("byte_array_read_failed", std::move(bytes.error));
        constexpr std::size_t kMaxMcpDocumentCharacters = 48 * 1024;
        bool document_truncated = false;
        if (bytes.decoded.document.size() > kMaxMcpDocumentCharacters) {
            bytes.decoded.document.resize(kMaxMcpDocumentCharacters);
            document_truncated = true;
        }
        return succeed({{"total_length", bytes.total_length}, {"copied_bytes", bytes.bytes.size()},
                        {"truncated", bytes.truncated},
                        {"format", ByteData::format_name(bytes.decoded.format)},
                        {"summary", bytes.decoded.summary}, {"complete", bytes.decoded.complete},
                        {"consumed_bytes", bytes.decoded.consumed_bytes},
                        {"diagnostic", bytes.decoded.diagnostic},
                        {"document", bytes.decoded.document},
                        {"document_truncated", document_truncated},
                        {"hex_preview", ByteData::hex_dump(bytes.bytes, 512)}});
    }

    if (request.tool == "start_instance_scan") {
        if (!request.arguments.contains("type_reference") ||
            !request.arguments["type_reference"].is_string())
            return fail("invalid_arguments", "type_reference is required.");
        if (request.arguments.contains("include_all_loaded") &&
            !request.arguments["include_all_loaded"].is_boolean())
            return fail("invalid_arguments", "include_all_loaded must be a boolean.");
        const Reference* type_reference = find_reference(
            request.arguments["type_reference"].get<std::string>(), ReferenceKind::ManagedType, model);
        if (!type_reference)
            return fail("invalid_reference", "The type reference is invalid or expired.");
        if (!model.class_browser_catalog_)
            model.load_class_browser_catalog();
        if (!model.class_browser_catalog_)
            return fail("metadata_unavailable", "Loaded managed types could not be enumerated.");
        const auto type = std::find_if(model.class_browser_catalog_->classes.begin(),
                                       model.class_browser_catalog_->classes.end(),
            [&](const BrowserClassInfo& candidate) {
                return candidate.image == type_reference->image &&
                    candidate.namespc == type_reference->namespc &&
                    candidate.class_name == type_reference->class_name;
            });
        if (type == model.class_browser_catalog_->classes.end())
            return fail("type_unavailable", "The referenced managed type is no longer loaded.");
        Command command{.kind = CommandKind::FindClassInstances};
        command.image = type->image;
        command.namespc = type->namespc;
        command.class_name = type->class_name;
        command.int_value = type->is_component ? 1 : 0;
        command.class_is_unity_object = type->is_unity_object;
        command.bool_value = request.arguments.value("include_all_loaded", true);
        model.find_class_instances(command);
        active_instance_scan_id_ = next_instance_scan_id_++;
        Reference scan{ReferenceKind::InstanceScan, 0, 0, 0,
                       scene_generation_, hierarchy_revision_};
        scan.image = type->image;
        scan.namespc = type->namespc;
        scan.class_name = type->class_name;
        scan.trace_id = active_instance_scan_id_;
        const std::string token = issue_reference(model, std::move(scan));
        if (token.empty())
            return fail("reference_unavailable", "A secure instance-scan reference could not be generated.");
        return succeed({{"scan_reference", token}, {"type", type->full_name},
                        {"strategy", type->is_unity_object ? "unity_object_query" : "bounded_static_root_graph"},
                        {"scope", type->is_unity_object
                            ? (command.bool_value ? "all_loaded_unity_objects" : "active_unity_objects")
                            : "static_managed_roots"},
                        {"active", model.working_.class_browser_scan_active},
                        {"found", model.working_.class_browser_instances.size()},
                        {"scanned_objects", model.working_.class_browser_scanned_objects},
                        {"status", model.working_.status}});
    }

    if (request.tool == "get_instance_scan") {
        if (!request.arguments.contains("scan_reference") ||
            !request.arguments["scan_reference"].is_string())
            return fail("invalid_arguments", "scan_reference is required.");
        const Reference* scan = find_reference(request.arguments["scan_reference"].get<std::string>(),
                                               ReferenceKind::InstanceScan, model);
        if (!scan)
            return fail("invalid_reference", "The instance-scan reference is invalid or expired.");
        if (scan->trace_id != active_instance_scan_id_ ||
            model.working_.class_browser_query.image != scan->image ||
            model.working_.class_browser_query.namespc != scan->namespc ||
            model.working_.class_browser_query.class_name != scan->class_name)
            return fail("scan_superseded", "A newer instance scan replaced this scan.");
        const auto offset = optional_offset(request.arguments, "offset");
        const auto limit = requested_limit(request.arguments, "limit", 50, 100);
        if (!offset || !limit)
            return fail("invalid_arguments", "offset must be non-negative and limit must be between 1 and 100.");
        const std::size_t first = std::min(*offset, model.working_.class_browser_instances.size());
        const std::size_t last = std::min(model.working_.class_browser_instances.size(),
                                          first + static_cast<std::size_t>(*limit));
        Json instances = Json::array();
        for (std::size_t index = first; index < last; ++index) {
            const ClassBrowserInstanceInfo& instance = model.working_.class_browser_instances[index];
            Json entry{{"index", index}, {"name", instance.name}, {"type", instance.type_name},
                       {"source", instance.source}, {"available", false}};
            const auto handle = model.class_browser_handles_.find(instance.token);
            const Object object = handle == model.class_browser_handles_.end()
                ? Object{} : Inspect::ResolveObjectHandle(handle->second);
            if (object) {
                const std::string object_token = capture_managed_reference(
                    object, "instance scan " + model.working_.class_browser_query.full_name);
                if (!object_token.empty()) {
                    entry["reference"] = object_token;
                    entry["available"] = true;
                } else {
                    entry["reference_error"] = "The instance could not be tracked safely.";
                }
            } else {
                entry["reference_error"] = "The instance was released after discovery.";
            }
            if (instance.game_object_instance_id != 0) {
                entry["game_object_instance_id"] = instance.game_object_instance_id;
                entry["game_object_name"] = instance.game_object_name;
                const HierarchyNode* node = hierarchy
                    ? find_hierarchy_node(*hierarchy, instance.game_object_instance_id) : nullptr;
                if (node) {
                    const std::string owner_token = object_reference(node->instance_id, node->name);
                    if (!owner_token.empty())
                        entry["game_object_reference"] = owner_token;
                }
            }
            instances.push_back(std::move(entry));
        }
        return succeed({{"scan_reference", request.arguments["scan_reference"]},
                        {"type", model.working_.class_browser_query.full_name},
                        {"active", model.working_.class_browser_scan_active},
                        {"complete", !model.working_.class_browser_scan_active},
                        {"truncated", model.working_.class_browser_scan_truncated},
                        {"scanned_objects", model.working_.class_browser_scanned_objects},
                        {"static_roots", model.working_.class_browser_static_roots},
                        {"found", model.working_.class_browser_instances.size()},
                        {"offset", first}, {"instances", std::move(instances)},
                        {"has_more", last < model.working_.class_browser_instances.size()},
                        {"status", model.working_.status}});
    }

    if (request.tool == "cancel_instance_scan") {
        if (!request.arguments.contains("scan_reference") ||
            !request.arguments["scan_reference"].is_string())
            return fail("invalid_arguments", "scan_reference is required.");
        const Reference* scan = find_reference(request.arguments["scan_reference"].get<std::string>(),
                                               ReferenceKind::InstanceScan, model);
        if (!scan)
            return fail("invalid_reference", "The instance-scan reference is invalid or expired.");
        if (scan->trace_id != active_instance_scan_id_ ||
            model.working_.class_browser_query.image != scan->image ||
            model.working_.class_browser_query.namespc != scan->namespc ||
            model.working_.class_browser_query.class_name != scan->class_name)
            return fail("scan_superseded", "A newer instance scan replaced this scan.");
        const bool was_active = model.working_.class_browser_scan_active;
        model.clear_class_instance_scan();
        active_instance_scan_id_ = 0;
        model.set_status(was_active ? "Instance scan cancelled by MCP." : "Instance scan was already complete.");
        return succeed({{"cancelled", was_active}, {"active", false},
                        {"found", model.working_.class_browser_instances.size()},
                        {"scanned_objects", model.working_.class_browser_scanned_objects},
                        {"status", model.working_.status}});
    }

    if (request.tool == "search_types") {
        if (request.arguments.contains("query") &&
            (!request.arguments["query"].is_string() ||
             request.arguments["query"].get_ref<const std::string&>().size() > 256))
            return fail("invalid_arguments", "query must be a string no longer than 256 characters.");
        if (request.arguments.contains("image") &&
            (!request.arguments["image"].is_string() ||
             request.arguments["image"].get_ref<const std::string&>().size() > 256))
            return fail("invalid_arguments", "image must be a string no longer than 256 characters.");
        const auto limit = requested_limit(request.arguments, "limit", 25, 100);
        if (!limit)
            return fail("invalid_arguments", "limit must be an integer between 1 and 100.");
        if (!model.class_browser_catalog_)
            model.load_class_browser_catalog();
        if (!model.class_browser_catalog_)
            return fail("metadata_unavailable", "Loaded managed types could not be enumerated.");
        const std::string query = lowercase(request.arguments.value("query", std::string{}));
        const std::string image_query = lowercase(request.arguments.value("image", std::string{}));
        Json matches = Json::array();
        for (const BrowserClassInfo& type : model.class_browser_catalog_->classes) {
            if (matches.size() >= static_cast<std::size_t>(*limit))
                break;
            const std::string searchable = lowercase(type.full_name + " " + type.namespc + " " +
                                                     type.class_name + " " + type.image);
            if ((!query.empty() && searchable.find(query) == std::string::npos) ||
                (!image_query.empty() && lowercase(type.image).find(image_query) == std::string::npos))
                continue;
            Reference reference{ReferenceKind::ManagedType, 0, 0, 0, scene_generation_, hierarchy_revision_};
            reference.image = type.image;
            reference.namespc = type.namespc;
            reference.class_name = type.class_name;
            const std::string token = issue_reference(model, std::move(reference));
            if (token.empty())
                return fail("reference_unavailable", "A secure type reference could not be generated.");
            matches.push_back({{"type_reference", token}, {"image", type.image}, {"namespace", type.namespc},
                               {"name", type.class_name}, {"full_name", type.full_name},
                               {"parent", type.parent_name}, {"is_component", type.is_component},
                               {"is_unity_object", type.is_unity_object}, {"is_interface", type.is_interface},
                               {"is_value_type", type.is_value_type}, {"is_enum", type.is_enum},
                               {"is_static", type.is_static}, {"is_abstract", type.is_abstract}});
        }
        return succeed({{"matches", std::move(matches)}, {"indexed_types", model.class_browser_catalog_->classes.size()},
                        {"limit", *limit}});
    }

    if (request.tool == "search_members") {
        if (!has_capability(request.context.capabilities, Capability::AutoDiscovery))
            return fail("discovery_permission_required",
                        "Enable automatic runtime discovery in Explorer Config.");
        const auto limit = requested_limit(request.arguments, "limit", 100, 250);
        const auto type_limit = requested_limit(request.arguments, "type_limit", 32, 128);
        if (!limit || !type_limit)
            return fail("invalid_arguments", "limit must be 1..250 and type_limit must be 1..128.");
        const std::string query = lowercase(request.arguments.value("query", std::string{}));
        const std::string type_query = lowercase(request.arguments.value("type_query", std::string{}));
        const std::string kind = request.arguments.value("kind", std::string("any"));
        if (!model.class_browser_catalog_)
            model.load_class_browser_catalog();
        if (!model.class_browser_catalog_)
            return fail("metadata_unavailable", "Loaded managed types could not be enumerated.");
        Json matches = Json::array();
        std::size_t scanned_types = 0;
        for (const BrowserClassInfo& type : model.class_browser_catalog_->classes) {
            if (matches.size() >= static_cast<std::size_t>(*limit) ||
                scanned_types >= static_cast<std::size_t>(*type_limit))
                break;
            if (!type_query.empty() && lowercase(type.full_name + " " + type.image).find(type_query) == std::string::npos)
                continue;
            ++scanned_types;
            Command load{.kind = CommandKind::LoadClassBrowserMembers};
            load.image = type.image;
            load.namespc = type.namespc;
            load.class_name = type.class_name;
            model.load_class_browser_members(load);
            std::string type_token;
            const auto ensure_type_token = [&]() -> const std::string& {
                if (type_token.empty()) {
                    Reference reference{ReferenceKind::ManagedType, 0, 0, 0, scene_generation_, hierarchy_revision_};
                    reference.image = type.image;
                    reference.namespc = type.namespc;
                    reference.class_name = type.class_name;
                    type_token = issue_reference(model, std::move(reference));
                }
                return type_token;
            };
            if (kind == "any" || kind == "field") {
                for (const Inspect::FieldInfo& field : model.class_browser_reflection_.fields) {
                    if (matches.size() >= static_cast<std::size_t>(*limit)) break;
                    if (lowercase(field.name + " " + field.type_name).find(query) == std::string::npos) continue;
                    matches.push_back({{"kind", "field"}, {"name", field.name}, {"type", field.type_name},
                                       {"declaring_type", field.declaring_type.full_name},
                                       {"type_reference", ensure_type_token()},
                                       {"static", field.is_static}, {"writable", Inspect::FieldCanWrite(field)}});
                }
            }
            if (kind == "any" || kind == "property") {
                for (const Inspect::PropertyInfo& property : model.class_browser_reflection_.properties) {
                    if (matches.size() >= static_cast<std::size_t>(*limit)) break;
                    if (lowercase(property.name + " " + property.type_name).find(query) == std::string::npos) continue;
                    matches.push_back({{"kind", "property"}, {"name", property.name}, {"type", property.type_name},
                                       {"declaring_type", property.declaring_type.full_name},
                                       {"type_reference", ensure_type_token()}, {"static", property.is_static},
                                       {"readable", property.can_read}, {"writable", property.can_write}});
                }
            }
            if (kind == "any" || kind == "method") {
                for (std::size_t index = 0; index < model.class_browser_reflection_.methods.size(); ++index) {
                    if (matches.size() >= static_cast<std::size_t>(*limit)) break;
                    const Inspect::MethodInfo& method = model.class_browser_reflection_.methods[index];
                    std::string signature = method.name + " " + method.return_type;
                    for (const Inspect::MethodParamInfo& parameter : method.parameters)
                        signature += " " + parameter.type_name;
                    if (lowercase(std::move(signature)).find(query) == std::string::npos) continue;
                    Reference method_reference{ReferenceKind::ManagedMethod, 0, 0, 0,
                                               scene_generation_, hierarchy_revision_};
                    method_reference.image = type.image;
                    method_reference.namespc = type.namespc;
                    method_reference.class_name = type.class_name;
                    method_reference.member_index = static_cast<int>(index);
                    const std::string method_token = issue_reference(model, std::move(method_reference));
                    matches.push_back({{"kind", "method"}, {"name", method.name}, {"return_type", method.return_type},
                                       {"declaring_type", method.declaring_type.full_name},
                                       {"type_reference", ensure_type_token()}, {"method_reference", method_token},
                                       {"static", method.is_static}, {"parameter_count", method.parameters.size()}});
                }
            }
        }
        return succeed({{"matches", std::move(matches)}, {"scanned_types", scanned_types},
                        {"indexed_types", model.class_browser_catalog_->classes.size()},
                        {"truncated", scanned_types >= static_cast<std::size_t>(*type_limit)}});
    }

    if (request.tool == "inspect_type") {
        if (!request.arguments.contains("type_reference") || !request.arguments["type_reference"].is_string())
            return fail("invalid_arguments", "type_reference is required.");
        const Reference* reference = find_reference(request.arguments["type_reference"].get<std::string>(),
                                                    ReferenceKind::ManagedType, model);
        if (!reference)
            return fail("invalid_reference", "The type reference is invalid or expired.");
        const Reference resolved_type = *reference;
        if (request.arguments.contains("member_query") &&
            (!request.arguments["member_query"].is_string() ||
             request.arguments["member_query"].get_ref<const std::string&>().size() > 256))
            return fail("invalid_arguments", "member_query must be a string no longer than 256 characters.");
        const auto limit = requested_limit(request.arguments, "member_limit", 100, 250);
        if (!limit)
            return fail("invalid_arguments", "member_limit must be an integer between 1 and 250.");
        Command command{.kind = CommandKind::LoadClassBrowserMembers};
        command.image = resolved_type.image;
        command.namespc = resolved_type.namespc;
        command.class_name = resolved_type.class_name;
        model.load_class_browser_members(command);
        const auto members = model.working_.class_browser_members;
        if (!members)
            return fail("metadata_unavailable", "Members for the referenced type could not be loaded.");
        const std::string query = lowercase(request.arguments.value("member_query", std::string{}));
        std::size_t emitted = 0;
        bool truncated = false;
        Json fields = Json::array();
        Json properties = Json::array();
        Json methods = Json::array();
        const auto accepts = [&](std::string_view text) {
            return query.empty() || lowercase(std::string(text)).find(query) != std::string::npos;
        };
        for (const ComponentInfo::Field& field : members->fields) {
            if (!accepts(field.name + " " + field.type_name + " " + field.declaring_type))
                continue;
            if (emitted++ >= static_cast<std::size_t>(*limit)) { truncated = true; break; }
            fields.push_back({{"name", field.name}, {"type", field.type_name},
                              {"declaring_type", field.declaring_type}, {"static", field.is_static},
                              {"read_only", field.is_read_only}, {"runtime_safe", field.runtime_safe},
                              {"capability_reason", field.capability_reason}});
        }
        if (!truncated) {
            for (const ComponentInfo::Property& property : members->properties) {
                if (!accepts(property.name + " " + property.type_name + " " + property.declaring_type))
                    continue;
                if (emitted++ >= static_cast<std::size_t>(*limit)) { truncated = true; break; }
                properties.push_back({{"name", property.name}, {"type", property.type_name},
                                      {"declaring_type", property.declaring_type}, {"can_read", property.can_read},
                                      {"can_write", property.can_write}, {"runtime_safe", property.runtime_safe},
                                      {"capability_reason", property.capability_reason}});
            }
        }
        if (!truncated) {
            for (std::size_t index = 0; index < members->methods.size(); ++index) {
                const ComponentInfo::Method& method = members->methods[index];
                const std::string signature = method_signature(method);
                if (!accepts(signature))
                    continue;
                if (emitted++ >= static_cast<std::size_t>(*limit)) { truncated = true; break; }
                Reference method_reference{ReferenceKind::ManagedMethod, 0, 0, 0,
                                           scene_generation_, hierarchy_revision_};
                method_reference.image = resolved_type.image;
                method_reference.namespc = resolved_type.namespc;
                method_reference.class_name = resolved_type.class_name;
                method_reference.member_index = static_cast<int>(index);
                const std::string method_token = issue_reference(model, std::move(method_reference));
                if (method_token.empty())
                    return fail("reference_unavailable", "A secure method reference could not be generated.");
                methods.push_back({{"method_reference", method_token}, {"name", method.name},
                                   {"signature", signature}, {"declaring_type", method.declaring_type},
                                   {"return_type", method.return_type}, {"parameter_types", method.parameter_types},
                                   {"parameter_names", method.parameter_names}, {"static", method.is_static},
                                   {"trace_candidate", true},
                                   {"invocation_supported_in_ui", method.runtime_callable},
                                   {"capability_reason", method.capability_reason}});
            }
        }
        return succeed({{"type", {{"image", resolved_type.image}, {"namespace", resolved_type.namespc},
                                   {"name", resolved_type.class_name}}},
                        {"fields", std::move(fields)}, {"properties", std::move(properties)},
                        {"methods", std::move(methods)}, {"truncated", truncated}, {"member_limit", *limit}});
    }

    if (request.tool == "invoke_method") {
        if (!request.arguments.contains("method_reference") ||
            !request.arguments["method_reference"].is_string() ||
            !request.arguments.contains("arguments") || !request.arguments["arguments"].is_array())
            return fail("invalid_arguments", "method_reference and an arguments array are required.");
        if (request.arguments["arguments"].size() > 16)
            return fail("invalid_arguments", "At most sixteen method arguments are allowed.");
        const Reference* method_reference = find_reference(
            request.arguments["method_reference"].get<std::string>(),
            ReferenceKind::ManagedMethod, model);
        if (!method_reference)
            return fail("invalid_reference", "The method reference is invalid or expired.");
        Command load{.kind = CommandKind::LoadClassBrowserMembers};
        load.image = method_reference->image;
        load.namespc = method_reference->namespc;
        load.class_name = method_reference->class_name;
        model.load_class_browser_members(load);
        if (method_reference->member_index < 0 ||
            static_cast<std::size_t>(method_reference->member_index) >=
                model.class_browser_reflection_.methods.size())
            return fail("method_unavailable", "The referenced method could not be resolved again.");
        const Inspect::MethodInfo method =
            model.class_browser_reflection_.methods[method_reference->member_index];
        Object target{};
        RootGuard target_owner;
        if (!method.is_static) {
            if (!request.arguments.contains("target_reference") ||
                !request.arguments["target_reference"].is_string())
                return fail("invalid_arguments", "target_reference is required for an instance method.");
            std::string code;
            std::string message;
            target = resolve_target_reference(
                request.arguments["target_reference"].get_ref<const std::string&>(),
                target_owner, code, message);
            if (!target)
                return fail(std::move(code), std::move(message));
            const auto* actual = static_cast<const URK::managed::Class*>(
                URK::managed::object_get_class(static_cast<URK::managed::Object*>(target.handle())));
            if (!actual || !method.declaring_type.handle ||
                URK::managed::class_is_assignable_from(
                    static_cast<const URK::managed::Class*>(method.declaring_type.handle), actual) == 0)
                return fail("target_type_mismatch", "The target object is not compatible with the method's declaring type.");
        }
        const auto argument_resolver = [&](std::string_view token, std::string& error,
                                           Inspect::ObjectHandle& lifetime_root) -> Object {
            RootGuard owner;
            std::string code;
            Object object = resolve_target_reference(token, owner, code, error);
            if (owner.handle.handle) {
                lifetime_root = owner.handle;
                owner.handle = {};
            }
            if (!object && error.empty())
                error = code;
            return object;
        };
        ManagedInvoke::Result result = ManagedInvoke::invoke(
            target, method, request.arguments["arguments"], argument_resolver);
        if (!result.error.empty())
            return fail("invocation_failed", std::move(result.error));
        Json value = value_json(result.value,
                                method.declaring_type.full_name + "." + method.name + "() result",
                                true);
        return succeed({{"method", method.declaring_type.full_name + "." + method.name},
                        {"signature", method_signature(
                            model.working_.class_browser_members->methods[method_reference->member_index])},
                        {"side_effects_possible", true}, {"result", std::move(value)}});
    }

    if (request.tool == "list_method_traces") {
        model.publish();
        const auto fresh = model.snapshot();
        Json traces = Json::array();
        for (const MethodTracer::Snapshot& trace : fresh->method_traces) {
            Reference reference{ReferenceKind::MethodTrace, 0, 0, 0, scene_generation_, hierarchy_revision_};
            reference.trace_id = trace.id;
            const std::string token = issue_reference(model, std::move(reference));
            if (token.empty())
                return fail("reference_unavailable", "A secure trace reference could not be generated.");
            traces.push_back({{"trace_reference", token}, {"method", trace.declaring_type + "." + trace.method_name},
                              {"return_type", trace.return_type}, {"parameter_types", trace.parameter_types},
                              {"active", trace.active}, {"total_calls", trace.total_calls},
                              {"retained_calls", retained_method_trace_calls(trace)},
                              {"displayed_groups", trace.records.size()},
                              {"collapsed_calls", collapsed_method_trace_calls(trace)},
                              {"overwritten_calls", trace.overwritten_records}, {"native_faults", trace.native_faults},
                              {"error", trace.error}});
        }
        return succeed({{"traces", std::move(traces)},
                        {"tracing_enabled_in_game", ModConfig::enable_mcp_tracing.load()}});
    }

    if (request.tool == "get_method_trace") {
        if (!request.arguments.contains("trace_reference") || !request.arguments["trace_reference"].is_string())
            return fail("invalid_arguments", "trace_reference is required.");
        const Reference* reference = find_reference(request.arguments["trace_reference"].get<std::string>(),
                                                    ReferenceKind::MethodTrace, model);
        if (!reference)
            return fail("invalid_reference", "The trace reference is invalid or expired.");
        const auto limit = requested_limit(request.arguments, "limit", 50, 200);
        const auto after = optional_sequence(request.arguments, "after_sequence");
        if (!limit || !after)
            return fail("invalid_arguments", "limit must be 1..200 and after_sequence must be a non-negative integer.");
        model.publish();
        const auto fresh = model.snapshot();
        const MethodTracer::Snapshot* trace = find_trace(*fresh, reference->trace_id);
        if (!trace)
            return fail("trace_not_found", "The method trace is no longer retained.");
        Json calls = Json::array();
        bool has_more = false;
        for (const MethodTracer::Record& record : trace->records) {
            if (record.sequence <= *after)
                continue;
            if (calls.size() >= static_cast<std::size_t>(*limit)) { has_more = true; break; }
            const std::uint64_t repeat_count = std::max<std::uint64_t>(1, record.repeat_count);
            const std::uint64_t sequence_start = record.sequence_start == 0 ? record.sequence : record.sequence_start;
            Json arguments = Json::array();
            for (const MethodTraceFormat::ArgumentView& argument : MethodTraceFormat::arguments(*trace, record))
                arguments.push_back({{"index", argument.index}, {"name", argument.name}, {"type", argument.type},
                                     {"value", argument.value}, {"readable", argument.readable},
                                     {"inspectable_reference", argument.inspectable_reference}});
            calls.push_back({{"sequence_start", sequence_start}, {"sequence", record.sequence},
                             {"repeat_count", repeat_count},
                             {"seconds", MethodTraceFormat::elapsed_seconds(*trace, record)},
                             {"thread_id", record.thread_id}, {"caller", record.caller_display},
                             {"target", trace->declaring_type + "." + trace->method_name},
                             {"target_instance", record.target_display}, {"arguments", std::move(arguments)},
                             {"result", MethodTraceFormat::result(*trace, record)},
                             {"return_captured", record.return_captured}});
        }
        return succeed({{"method", trace->declaring_type + "." + trace->method_name}, {"active", trace->active},
                        {"total_calls", trace->total_calls},
                        {"retained_calls", retained_method_trace_calls(*trace)},
                        {"displayed_groups", trace->records.size()},
                        {"collapsed_calls", collapsed_method_trace_calls(*trace)},
                        {"overwritten_calls", trace->overwritten_records},
                        {"native_faults", trace->native_faults}, {"calls", std::move(calls)}, {"has_more", has_more}});
    }

    if (request.tool == "build_call_graph") {
        const auto limit = requested_limit(request.arguments, "limit", 100, 250);
        if (!limit)
            return fail("invalid_arguments", "limit must be an integer between 1 and 250.");
        std::uint64_t requested_trace = 0;
        if (request.arguments.contains("trace_reference")) {
            if (!request.arguments["trace_reference"].is_string())
                return fail("invalid_arguments", "trace_reference must be a string.");
            const Reference* reference = find_reference(request.arguments["trace_reference"].get<std::string>(),
                                                        ReferenceKind::MethodTrace, model);
            if (!reference)
                return fail("invalid_reference", "The trace reference is invalid or expired.");
            requested_trace = reference->trace_id;
        }
        model.publish();
        const auto fresh = model.snapshot();
        struct EdgeSummary { std::uint64_t calls = 0; double first = 0.0; double last = 0.0; std::set<std::uint32_t> threads; };
        std::map<std::pair<std::string, std::string>, EdgeSummary> summaries;
        for (const MethodTracer::Snapshot& trace : fresh->method_traces) {
            if (requested_trace != 0 && trace.id != requested_trace)
                continue;
            const std::string target = trace.declaring_type + "." + trace.method_name;
            for (const MethodTracer::Record& record : trace.records) {
                const std::string caller = record.caller_display.empty() ? "<unknown caller>" : record.caller_display;
                EdgeSummary& edge = summaries[{caller, target}];
                const double seconds = MethodTraceFormat::elapsed_seconds(trace, record);
                if (edge.calls == 0)
                    edge.first = seconds;
                edge.last = seconds;
                edge.calls += std::max<std::uint64_t>(1, record.repeat_count);
                edge.threads.insert(record.thread_id);
            }
        }
        std::vector<std::pair<std::pair<std::string, std::string>, EdgeSummary>> ordered(summaries.begin(), summaries.end());
        std::sort(ordered.begin(), ordered.end(), [](const auto& left, const auto& right) {
            return left.second.calls > right.second.calls;
        });
        Json edges = Json::array();
        const std::size_t count = std::min<std::size_t>(ordered.size(), static_cast<std::size_t>(*limit));
        for (std::size_t index = 0; index < count; ++index) {
            const auto& [key, edge] = ordered[index];
            edges.push_back({{"caller", key.first}, {"target", key.second}, {"calls", edge.calls},
                             {"thread_count", edge.threads.size()}, {"first_seconds", edge.first},
                             {"last_seconds", edge.last}});
        }
        return succeed({{"edges", std::move(edges)}, {"truncated", ordered.size() > count},
                        {"captured_relationships", ordered.size()}});
    }

    if (request.tool == "get_activity_log") {
        const auto limit = requested_limit(request.arguments, "limit", 50, 200);
        const auto after = optional_sequence(request.arguments, "after_sequence");
        if (!limit || !after)
            return fail("invalid_arguments", "limit must be 1..200 and after_sequence must be a non-negative integer.");
        Json events = Json::array();
        bool has_more = false;
        for (const Snapshot::FlightEvent& event : snapshot->flight_recorder) {
            if (event.sequence <= *after)
                continue;
            if (events.size() >= static_cast<std::size_t>(*limit)) { has_more = true; break; }
            events.push_back({{"sequence", event.sequence}, {"seconds", event.seconds_since_start},
                              {"stage", event.stage}, {"operation", event.operation}, {"detail", event.detail}});
        }
        return succeed({{"events", std::move(events)}, {"has_more", has_more}});
    }

    if (request.tool == "start_method_trace") {
        if (!request.arguments.contains("method_reference") || !request.arguments["method_reference"].is_string())
            return fail("invalid_arguments", "method_reference is required.");
        const Reference* reference = find_reference(request.arguments["method_reference"].get<std::string>(),
                                                    ReferenceKind::ManagedMethod, model);
        if (!reference)
            return fail("invalid_reference", "The method reference is invalid or expired.");
        Command load{.kind = CommandKind::LoadClassBrowserMembers};
        load.image = reference->image;
        load.namespc = reference->namespc;
        load.class_name = reference->class_name;
        model.load_class_browser_members(load);
        if (!model.working_.class_browser_members || reference->member_index < 0 ||
            static_cast<std::size_t>(reference->member_index) >= model.working_.class_browser_members->methods.size())
            return fail("method_unavailable", "The referenced method could not be resolved again.");
        const ComponentInfo::Method method = model.working_.class_browser_members->methods[reference->member_index];
        Command trace{.kind = CommandKind::SetMethodTrace};
        trace.member_index = reference->member_index;
        trace.bool_value = true;
        trace.class_browser_target = true;
        trace.image = reference->image;
        trace.namespc = reference->namespc;
        trace.class_name = reference->class_name;
        model.set_method_trace(trace);
        model.publish();
        const auto fresh = model.snapshot();
        const auto found = std::find_if(fresh->method_traces.rbegin(), fresh->method_traces.rend(),
            [&](const MethodTracer::Snapshot& item) {
                return item.active && item.method_name == method.name && item.declaring_type == method.declaring_type &&
                       item.parameter_types == method.parameter_types;
            });
        if (found == fresh->method_traces.rend())
            return fail("trace_start_failed", fresh->status.empty() ? "The trace hook could not be installed." : fresh->status);
        model.mcp_method_trace_ids_.insert(found->id);
        Reference trace_reference{ReferenceKind::MethodTrace, 0, 0, 0, scene_generation_, hierarchy_revision_};
        trace_reference.trace_id = found->id;
        const std::string token = issue_reference(model, std::move(trace_reference));
        if (token.empty())
            return fail("reference_unavailable", "A secure trace reference could not be generated.");
        return succeed({{"trace_reference", token}, {"method", found->declaring_type + "." + found->method_name},
                        {"active", found->active}, {"status", fresh->status}});
    }

    if (request.tool == "stop_method_trace" || request.tool == "clear_method_trace") {
        if (!request.arguments.contains("trace_reference") || !request.arguments["trace_reference"].is_string())
            return fail("invalid_arguments", "trace_reference is required.");
        const Reference* reference = find_reference(request.arguments["trace_reference"].get<std::string>(),
                                                    ReferenceKind::MethodTrace, model);
        if (!reference)
            return fail("invalid_reference", "The trace reference is invalid or expired.");
        if (request.tool == "stop_method_trace") {
            Command command{.kind = CommandKind::SetMethodTrace};
            command.bool_value = false;
            command.reference_token = reference->trace_id;
            model.set_method_trace(command);
            model.mcp_method_trace_ids_.erase(reference->trace_id);
        } else {
            model.clear_method_trace(reference->trace_id);
        }
        model.publish();
        const auto fresh = model.snapshot();
        const MethodTracer::Snapshot* trace = find_trace(*fresh, reference->trace_id);
        if (!trace)
            return fail("trace_not_found", "The method trace is no longer retained.");
        return succeed({{"trace_reference", request.arguments["trace_reference"]}, {"active", trace->active},
                        {"retained_calls", retained_method_trace_calls(*trace)},
                        {"displayed_groups", trace->records.size()},
                        {"collapsed_calls", collapsed_method_trace_calls(*trace)},
                        {"status", fresh->status}});
    }

    if (request.tool == "build_reference_graph") {
        if (snapshot->selected_instance_id == 0)
            return fail("selection_required", "Select a GameObject in Explorer before building a reference graph.");
        Command command{.kind = CommandKind::BuildReferenceGraph};
        model.build_reference_graph(command);
        Json nodes = Json::array();
        for (const Snapshot::ReferenceGraph::Node& node : model.working_.reference_graph.nodes) {
            const std::string token = issue_reference(model, {ReferenceKind::GraphNode, 0, 0, node.token,
                scene_generation_, hierarchy_revision_});
            if (token.empty())
                return fail("reference_unavailable", "A secure graph reference could not be generated.");
            nodes.push_back({{"reference", token}, {"label", node.label}, {"type", node.type_name},
                             {"kind", graph_node_kind(node.kind)}, {"depth", node.depth}});
        }
        Json edges = Json::array();
        for (const Snapshot::ReferenceGraph::Edge& edge : model.working_.reference_graph.edges)
            edges.push_back({{"from", edge.from}, {"to", edge.to}, {"label", edge.label},
                             {"kind", graph_edge_kind(edge.kind)}});
        return succeed({{"nodes", std::move(nodes)}, {"edges", std::move(edges)},
                        {"truncated", model.working_.reference_graph.truncated},
                        {"status", model.working_.reference_graph.status}});
    }

    if (request.tool == "get_watch_history") {
        const auto event_limit = requested_limit(request.arguments, "event_limit", 25, 100);
        if (!event_limit)
            return fail("invalid_arguments", "event_limit must be an integer between 1 and 100.");
        if (request.arguments.contains("watch_id") &&
            (!request.arguments["watch_id"].is_number_unsigned() &&
             !request.arguments["watch_id"].is_number_integer()))
            return fail("invalid_arguments", "watch_id must be a positive integer.");
        std::uint64_t requested_id = 0;
        if (request.arguments.contains("watch_id")) {
            if (request.arguments["watch_id"].is_number_unsigned())
                requested_id = request.arguments["watch_id"].get<std::uint64_t>();
            else {
                const std::int64_t signed_watch_id = request.arguments["watch_id"].get<std::int64_t>();
                if (signed_watch_id < 0)
                    return fail("invalid_arguments", "watch_id must be a positive integer.");
                requested_id = static_cast<std::uint64_t>(signed_watch_id);
            }
        }
        Json watches = Json::array();
        for (const Snapshot::FieldWatch& watch : snapshot->field_watches) {
            if (requested_id != 0 && watch.id != requested_id)
                continue;
            Json events = Json::array();
            const std::size_t first = watch.events.size() > static_cast<std::size_t>(*event_limit)
                ? watch.events.size() - static_cast<std::size_t>(*event_limit) : 0;
            for (std::size_t index = first; index < watch.events.size(); ++index) {
                const Snapshot::FieldWatchEvent& event = watch.events[index];
                events.push_back({{"sequence", event.sequence}, {"seconds", event.seconds_since_start},
                                  {"previous", event.previous_value}, {"current", event.current_value},
                                  {"source", event.source}, {"alarm_triggered", event.alarm_triggered}});
            }
            watches.push_back({{"watch_id", watch.id}, {"component_type", watch.component_type},
                               {"member", watch.field_name}, {"type", watch.field_type}, {"active", watch.active},
                               {"value_available", watch.value_available}, {"current", watch.current_value},
                               {"change_count", watch.change_count}, {"alarm_active", watch.alarm_active},
                               {"events", std::move(events)}});
        }
        if (requested_id != 0 && watches.empty())
            return fail("watch_not_found", "The requested watch does not exist.");
        return succeed({{"watches", std::move(watches)}});
    }

    if (request.tool == "write_member") {
        if (!request.arguments.contains("reference") || !request.arguments["reference"].is_string() ||
            !request.arguments.contains("member") || !request.arguments["member"].is_string() ||
            !request.arguments.contains("value"))
            return fail("invalid_arguments", "reference, member, and value are required.");
        RootGuard owner;
        std::string code;
        std::string message;
        Object target = resolve_target_reference(request.arguments["reference"].get_ref<const std::string&>(),
                                                 owner, code, message);
        if (!target)
            return fail(std::move(code), std::move(message));
        const std::string member_name = request.arguments["member"].get<std::string>();
        const std::string kind = request.arguments.value("kind", std::string("auto"));
        const Inspect::TypeInfo type = Inspect::TypeOf(target);
        if (!type.handle)
            return fail("metadata_unavailable", "The target type could not be resolved.");
        const auto resolver = [&](std::string_view token, std::string& resolver_error,
                                  Inspect::ObjectHandle& lifetime_root) -> Object {
            RootGuard argument_owner;
            std::string resolver_code;
            Object resolved = resolve_target_reference(token, argument_owner, resolver_code, resolver_error);
            if (argument_owner.handle.handle) {
                lifetime_root = argument_owner.handle;
                argument_owner.handle = {};
            }
            if (!resolved && resolver_error.empty())
                resolver_error = resolver_code;
            return resolved;
        };
        if (kind != "property") {
            const auto fields = Inspect::fields_from_class(
                static_cast<const URK::managed::Class*>(type.handle), true);
            const auto field = std::find_if(fields.begin(), fields.end(), [&](const Inspect::FieldInfo& value) {
                return value.name == member_name;
            });
            if (field != fields.end()) {
                if (!Inspect::FieldCanWrite(*field))
                    return fail("member_not_writable", "The requested field is readonly or opaque.");
                ManagedInvoke::PreparedValue prepared = ManagedInvoke::prepare_value(
                    field->name, field->type_name, field->type, field->is_value_type, field->is_enum,
                    request.arguments["value"], resolver);
                if (!prepared.error.empty())
                    return fail("invalid_value", std::move(prepared.error));
                clear_error();
                if (!Inspect::SetField(target, *field, prepared.value)) {
                    const char* detail = last_error();
                    return fail("write_failed", detail && detail[0] ? detail : "Managed field write failed.");
                }
                Inspect::ValueInfo observed = Inspect::ReadField(target, *field);
                return succeed({{"reference", request.arguments["reference"]}, {"member", member_name},
                                {"kind", "field"}, {"value", value_json(observed, member_name)}});
            }
            if (kind == "field")
                return fail("member_not_found", "The requested field was not found.");
        }
        if (!has_capability(request.context.capabilities, Capability::PropertyAccess))
            return fail("property_permission_required", "Enable managed property access in Explorer Config.");
        const auto properties = Inspect::properties_from_class(
            static_cast<const URK::managed::Class*>(type.handle), true);
        const auto property = std::find_if(properties.begin(), properties.end(),
            [&](const Inspect::PropertyInfo& value) { return value.name == member_name; });
        if (property == properties.end())
            return fail("member_not_found", "The requested property was not found.");
        if (!property->can_write || property->set_method_is_abstract || property->type_is_opaque)
            return fail("member_not_writable", "The requested property has no usable setter.");
        ManagedInvoke::PreparedValue prepared = ManagedInvoke::prepare_value(
            property->name, property->type_name, property->type, property->is_value_type,
            property->is_enum, request.arguments["value"], resolver);
        if (!prepared.error.empty())
            return fail("invalid_value", std::move(prepared.error));
        clear_error();
        if (!Inspect::SetProperty(target, *property, prepared.value)) {
            const char* detail = last_error();
            return fail("write_failed", detail && detail[0] ? detail : "Managed property write failed.");
        }
        Json result{{"reference", request.arguments["reference"]}, {"member", member_name},
                    {"kind", "property"}, {"written", true}};
        if (property->can_read) {
            Inspect::ValueInfo observed = Inspect::ReadProperty(target, *property);
            result["value"] = value_json(observed, member_name);
        }
        return succeed(std::move(result));
    }

    if (request.tool == "mutate_game_object") {
        if (!request.arguments.contains("reference") || !request.arguments["reference"].is_string() ||
            !request.arguments.contains("action") || !request.arguments["action"].is_string())
            return fail("invalid_arguments", "reference and action are required.");
        const Reference* reference = find_reference(request.arguments["reference"].get<std::string>(),
                                                    ReferenceKind::GameObject, model);
        if (!reference)
            return fail("invalid_reference", "The GameObject reference is invalid or expired.");
        const std::string action = request.arguments["action"].get<std::string>();
        if (action == "destroy" &&
            !has_capability(request.context.capabilities, Capability::Destructive))
            return fail("destructive_permission_required",
                        "Enable destructive Unity operations in Explorer Config.");
        Command command{};
        command.instance_id = reference->instance_id;
        command.scene_generation = reference->scene_generation;
        command.hierarchy_revision = reference->hierarchy_revision;
        const Json value = request.arguments.value("value", Json(nullptr));
        if (action == "rename" || action == "set_tag") {
            if (!value.is_string())
                return fail("invalid_arguments", "This action requires a string value.");
            command.kind = action == "rename" ? CommandKind::Rename : CommandKind::SetTag;
            command.text = value.get<std::string>();
        } else if (action == "set_layer") {
            if (!value.is_number_integer() && !value.is_number_unsigned())
                return fail("invalid_arguments", "set_layer requires an integer value.");
            command.kind = CommandKind::SetLayer;
            command.int_value = value.get<int>();
        } else if (action == "set_static" || action == "set_active") {
            if (!value.is_boolean())
                return fail("invalid_arguments", "This action requires a boolean value.");
            command.kind = action == "set_static" ? CommandKind::SetStatic : CommandKind::SetActive;
            command.bool_value = value.get<bool>();
        } else if (action == "set_position" || action == "set_rotation" || action == "set_scale") {
            const auto vector = vector_argument(value);
            if (!vector)
                return fail("invalid_arguments", "Transform actions require [x,y,z] or {x,y,z}.");
            command.kind = action == "set_position" ? CommandKind::SetLocalPosition :
                           action == "set_rotation" ? CommandKind::SetLocalRotation : CommandKind::SetLocalScale;
            command.vector_value = *vector;
        } else if (action == "duplicate") {
            command.kind = CommandKind::DuplicateObject;
        } else if (action == "destroy") {
            command.kind = CommandKind::DeleteObject;
        } else {
            return fail("invalid_arguments", "Unsupported GameObject action.");
        }
        model.process_command(command);
        model.publish();
        return succeed({{"reference", request.arguments["reference"]}, {"action", action},
                        {"status", model.working_.status}});
    }

    if (request.tool == "manage_component") {
        const std::string action = request.arguments.value("action", std::string{});
        if ((action == "remove") &&
            !has_capability(request.context.capabilities, Capability::Destructive))
            return fail("destructive_permission_required",
                        "Enable destructive Unity operations in Explorer Config.");
        Command command{};
        if (action == "add") {
            if (!request.arguments.contains("object_reference") ||
                !request.arguments["object_reference"].is_string() ||
                !request.arguments.contains("class") || !request.arguments["class"].is_string())
                return fail("invalid_arguments", "add requires object_reference and class.");
            const Reference* reference = find_reference(
                request.arguments["object_reference"].get<std::string>(), ReferenceKind::GameObject, model);
            if (!reference)
                return fail("invalid_reference", "The GameObject reference is invalid or expired.");
            command.kind = CommandKind::AddComponent;
            command.instance_id = reference->instance_id;
            command.scene_generation = reference->scene_generation;
            command.hierarchy_revision = reference->hierarchy_revision;
            command.image = request.arguments.value("image", std::string{});
            command.namespc = request.arguments.value("namespace", std::string{});
            command.class_name = request.arguments["class"].get<std::string>();
        } else if (action == "remove" || action == "set_enabled") {
            if (!request.arguments.contains("component_reference") ||
                !request.arguments["component_reference"].is_string())
                return fail("invalid_arguments", "component_reference is required.");
            const Reference* reference = find_reference(
                request.arguments["component_reference"].get<std::string>(), ReferenceKind::Component, model);
            if (!reference)
                return fail("invalid_reference", "The component reference is invalid or expired.");
            command.instance_id = reference->instance_id;
            if (action == "remove")
                command.kind = CommandKind::DeleteComponent;
            else {
                if (!request.arguments.contains("enabled") || !request.arguments["enabled"].is_boolean())
                    return fail("invalid_arguments", "set_enabled requires enabled.");
                command.kind = CommandKind::SetComponentEnabled;
                command.bool_value = request.arguments["enabled"].get<bool>();
            }
        } else {
            return fail("invalid_arguments", "Unsupported component action.");
        }
        model.process_command(command);
        model.publish();
        return succeed({{"action", action}, {"status", model.working_.status}});
    }

    if (request.tool == "load_scene") {
        if (!has_capability(request.context.capabilities, Capability::Destructive))
            return fail("destructive_permission_required",
                        "Enable destructive Unity operations in Explorer Config.");
        const int build_index = request.arguments.value("build_index", -1);
        const std::string name = request.arguments.value("name", std::string{});
        if (build_index < 0 && name.empty())
            return fail("invalid_arguments", "build_index or name is required.");
        Command command{.kind = CommandKind::LoadScene};
        command.int_value = build_index;
        command.text = name;
        model.process_command(command);
        model.publish();
        return succeed({{"build_index", build_index}, {"name", name}, {"status", model.working_.status}});
    }

    if (request.tool == "export_diagnostic_bundle") {
        model.working_.runtime_backend = snapshot->runtime_backend;
        model.working_.runtime_capabilities = snapshot->runtime_capabilities;
        model.working_.unity_version = snapshot->unity_version;
        const DiagnosticBundle::Result result = DiagnosticBundle::write(model.working_);
        if (!result.succeeded)
            return fail("export_failed", result.error.empty() ? "The diagnostic bundle could not be written." : result.error);
        model.working_.diagnostic_bundle_path = result.path;
        return succeed({{"path", result.path}});
    }

    return fail("tool_not_found", "The requested MCP tool is not available.");
}

} // namespace Explorer::Mcp

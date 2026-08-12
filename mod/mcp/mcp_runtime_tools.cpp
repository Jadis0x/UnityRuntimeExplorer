// Copyright (c) 2026 Jadis0x. All rights reserved.
#include "mcp_runtime_tools.h"

#include "explorer/diagnostic_bundle.h"
#include "explorer/explorer_model.h"
#include "mcp/core/tool_catalog.h"

#include <Windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <optional>

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

Json vector_json(const Vector3& value) {
    return {{"x", value.x}, {"y", value.y}, {"z", value.z}};
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
} // namespace

void RuntimeTools::reset() {
    references_.clear();
    scene_generation_ = 0;
    hierarchy_revision_ = 0;
}

void RuntimeTools::synchronize_generation(const RuntimeModel& model) {
    const auto snapshot = model.snapshot();
    const std::uint64_t generation = snapshot->hierarchy ? snapshot->hierarchy->scene_generation : 0;
    const std::uint64_t revision = snapshot->hierarchy ? snapshot->hierarchy->revision : 0;
    if (generation != scene_generation_ || revision != hierarchy_revision_) {
        references_.clear();
        scene_generation_ = generation;
        hierarchy_revision_ = revision;
    }
}

std::string RuntimeTools::issue_reference(Reference reference) {
    if (references_.size() >= kMaxReferences)
        references_.clear();
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
    references_[token] = reference;
    return token;
}

const RuntimeTools::Reference* RuntimeTools::find_reference(std::string_view token, ReferenceKind kind,
                                                            const RuntimeModel& model) const {
    const auto found = references_.find(std::string(token));
    if (found == references_.end() || found->second.kind != kind)
        return nullptr;
    const auto snapshot = model.snapshot();
    if (!snapshot->hierarchy || found->second.scene_generation != snapshot->hierarchy->scene_generation ||
        found->second.hierarchy_revision != snapshot->hierarchy->revision)
        return nullptr;
    return &found->second;
}

Response RuntimeTools::execute(RuntimeModel& model, const Request& request) {
    synchronize_generation(model);
    auto fail = [&](std::string code, std::string message) {
        model.record_flight("MCP_ERROR", request.tool, code);
        model.publish();
        return failure(request.id, std::move(code), std::move(message));
    };
    auto succeed = [&](Json result) {
        model.record_flight("MCP", request.tool, "read-only");
        model.publish();
        return Response{request.id, true, std::move(result)};
    };
    if (!is_read_only_tool(request.tool))
        return fail("tool_not_allowed", "The requested tool is not part of the read-only MCP surface.");

    const auto snapshot = model.snapshot();
    const auto hierarchy = snapshot->hierarchy;
    const auto object_reference = [&](int instance_id) {
        return issue_reference({ReferenceKind::GameObject, instance_id, 0, 0, scene_generation_, hierarchy_revision_});
    };

    if (request.tool == "runtime_status") {
        return succeed({{"connected", true}, {"read_only", true}, {"backend", snapshot->runtime_backend},
                        {"unity_version", snapshot->unity_version}, {"runtime_capabilities", snapshot->runtime_capabilities},
                        {"status", snapshot->status}, {"snapshot_revision", snapshot->revision},
                        {"scene_generation", hierarchy ? hierarchy->scene_generation : 0},
                        {"hierarchy_revision", hierarchy ? hierarchy->revision : 0},
                        {"scene_count", hierarchy ? hierarchy->scenes.size() : 0},
                        {"object_count", hierarchy ? hierarchy->objects : 0},
                        {"selected", snapshot->selected_instance_id != 0},
                        {"managed_used_bytes", snapshot->managed_used_bytes},
                        {"managed_heap_bytes", snapshot->managed_heap_bytes},
                        {"diagnostic_count", snapshot->diagnostics.size()}});
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
            const std::string haystack = lowercase(node.name + " " + node.tag + " " + std::to_string(node.instance_id));
            if ((!active_only || node.active) && (query.empty() || haystack.find(query) != std::string::npos)) {
                const std::string token = object_reference(node.instance_id);
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

    if (request.tool == "get_selected_object") {
        if (snapshot->selected_instance_id == 0 || !snapshot->inspector.valid)
            return succeed({{"selected", false}});
        const std::string token = object_reference(snapshot->selected_instance_id);
        if (token.empty())
            return fail("reference_unavailable", "A secure object reference could not be generated.");
        return succeed({{"selected", true}, {"reference", token}, {"instance_id", snapshot->inspector.instance_id},
                        {"name", snapshot->inspector.name}, {"tag", snapshot->inspector.tag},
                        {"active", snapshot->inspector.active}, {"type", snapshot->inspector.type_name}});
    }

    auto resolve_requested_object = [&]() -> std::optional<Reference> {
        if (request.arguments.contains("reference")) {
            if (!request.arguments["reference"].is_string())
                return std::nullopt;
            const Reference* reference = find_reference(request.arguments["reference"].get<std::string>(),
                                                        ReferenceKind::GameObject, model);
            return reference ? std::optional<Reference>(*reference) : std::nullopt;
        }
        if (snapshot->selected_instance_id == 0)
            return std::nullopt;
        return Reference{ReferenceKind::GameObject, snapshot->selected_instance_id, 0, 0,
                         scene_generation_, hierarchy_revision_};
    };

    if (request.tool == "inspect_game_object" || request.tool == "list_components") {
        const auto reference = resolve_requested_object();
        if (!reference)
            return fail("invalid_reference", "Provide a current opaque GameObject reference or select an object in Explorer.");
        RootGuard root;
        const GameObject object = model.resolve_live_game_object(reference->instance_id, root.handle);
        if (!object)
            return fail("object_unavailable", "The referenced GameObject is no longer available.");
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
            const std::string token = issue_reference({ReferenceKind::Component, component_id,
                reference->instance_id, 0, scene_generation_, hierarchy_revision_});
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
        const std::string current_object_reference = object_reference(reference->instance_id);
        if (current_object_reference.empty())
            return fail("reference_unavailable", "A secure object reference could not be generated.");
        if (request.tool == "list_components") {
            const std::string object_name = object.name();
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
                    {"instance_id", reference->instance_id}, {"name", object.name()}, {"tag", object.tag()},
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
        const Reference* reference = find_reference(request.arguments["component_reference"].get<std::string>(),
                                                    ReferenceKind::Component, model);
        if (!reference)
            return fail("invalid_reference", "The component reference is invalid or expired.");
        RootGuard root;
        const GameObject owner = model.resolve_live_game_object(reference->owner_instance_id, root.handle);
        if (!owner)
            return fail("object_unavailable", "The component owner is no longer available.");
        Object target;
        const auto components = owner.GetComponentsRooted<Object>();
        for (const Object& component : components)
            if (component && component.GetInstanceID() == reference->instance_id) {
                target = component;
                break;
            }
        if (!target)
            return fail("component_unavailable", "The referenced component is no longer available.");
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
        Json result{{"member", member_name}, {"kind", resolved_kind}, {"type", value.type_name},
                    {"value_kind", value_kind(value.kind)}, {"display", value.display},
                    {"readable", value.readable}};
        if (value.kind == Inspect::ValueKind::ArrayReference)
            result["array_length"] = value.array_length;
        if (value.kind == Inspect::ValueKind::ObjectReference || value.kind == Inspect::ValueKind::ArrayReference)
            result["has_reference"] = value.object != nullptr;
        value.object = nullptr;
        clear_error();
        return succeed(std::move(result));
    }

    if (request.tool == "build_reference_graph") {
        if (snapshot->selected_instance_id == 0)
            return fail("selection_required", "Select a GameObject in Explorer before building a reference graph.");
        Command command{.kind = CommandKind::BuildReferenceGraph};
        model.build_reference_graph(command);
        Json nodes = Json::array();
        for (const Snapshot::ReferenceGraph::Node& node : model.working_.reference_graph.nodes) {
            const std::string token = issue_reference({ReferenceKind::GraphNode, 0, 0, node.token,
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

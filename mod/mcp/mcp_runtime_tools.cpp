// Copyright (c) 2026 Jadis0x. All rights reserved.
#include "mcp_runtime_tools.h"

#include "explorer/diagnostic_bundle.h"
#include "explorer/explorer_model.h"
#include "explorer/member_labels.h"
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
#include <cstdlib>
#include <cmath>
#include <limits>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <optional>
#include <set>
#include <tuple>

namespace Explorer::Mcp {
namespace {
using Json = nlohmann::json;
using namespace URK::Unity;

constexpr std::size_t kMaxReferences = 4096;
// Keep list results small; page for the rest.
constexpr std::size_t kResultBudgetBytes = 24 * 1024;
// Total response ceiling; the largest array is trimmed to fit.
constexpr std::size_t kResponseBudgetBytes = 28 * 1024;

std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return value;
}

// Adds what Explorer knows about a name: the alias the overlay shows for an
// unreadable one, and any saved label. The alias doubles as a handle, since a
// client can pass it back to set_label instead of retyping the original.
void annotate_label(Json& value, Labels::Kind kind, std::string_view type_name, std::string_view member = {},
                    std::string_view signature = {}) {
    const std::string_view name = kind == Labels::Kind::Type ? type_name : member;
    if (Labels::unreadable(name))
        value["alias"] = kind == Labels::Kind::Type ? Labels::humanize(name) : Labels::alias(kind, name);
    if (const std::optional<Labels::Label> label = Labels::find(kind, type_name, member, signature)) {
        value["label"] = label->name;
        if (!label->note.empty())
            value["label_note"] = label->note;
        if (label->stale)
            value["label_stale"] = true;
    }
}

// Drops trailing array elements, largest array first, until the result fits the
// bridge limit, and says which arrays were cut so a client can page for the rest.
void fit_to_budget(Json& result) {
    if (!result.is_object())
        return;
    Json trimmed = Json::array();
    for (int round = 0; round < 48; ++round) {
        const std::size_t size = result.dump().size();
        if (size <= kResponseBudgetBytes)
            break;
        Json* target = nullptr;
        std::string name;
        std::size_t largest = 0;
        const auto consider = [&](Json& value, const std::string& label) {
            if (!value.is_array() || value.size() < 2)
                return;
            const std::size_t bytes = value.dump().size();
            if (bytes > largest) {
                largest = bytes;
                target = &value;
                name = label;
            }
        };
        for (auto& [key, value] : result.items()) {
            consider(value, key);
            if (value.is_object())
                for (auto& [inner_key, inner] : value.items())
                    consider(inner, key + "." + inner_key);
        }
        if (!target)
            break;
        const std::size_t average = largest / target->size() + 1;
        std::size_t drop = (size - kResponseBudgetBytes) / average + 1;
        drop = std::min(drop, target->size() - 1);
        for (std::size_t index = 0; index < drop; ++index)
            target->erase(target->size() - 1);
        if (std::find(trimmed.begin(), trimmed.end(), name) == trimmed.end())
            trimmed.push_back(name);
    }
    if (!trimmed.empty()) {
        result["truncated"] = true;
        result["trimmed_arrays"] = std::move(trimmed);
        result["trimmed_note"] = "Result cut to the response budget. Page with offset or narrow the query.";
    }
}

// Whether `requested` names this member: its real name, the alias the overlay
// shows for an unreadable one, or the label somebody saved for it.
bool member_name_matches(Labels::Kind kind, std::string_view owner, std::string_view actual,
                         std::string_view requested, bool exact_only) {
    if (actual == requested)
        return true;
    if (exact_only)
        return false;
    if (Labels::unreadable(actual) && Labels::alias(kind, actual) == requested)
        return true;
    const std::optional<Labels::Label> label = Labels::find(kind, owner, actual);
    return label && label->name == requested;
}

template <class Member, class OwnerOf>
const Member* find_named_member(const std::vector<Member>& members, Labels::Kind kind, std::string_view requested,
                                OwnerOf owner_of) {
    for (const bool exact_only : {true, false})
        for (const Member& member : members)
            if (member_name_matches(kind, owner_of(member), member.name, requested, exact_only))
                return &member;
    return nullptr;
}

// Root-first chain of nodes from a scene root down to `instance_id`.
bool hierarchy_path(const HierarchyNode& node, int instance_id, std::vector<const HierarchyNode*>& path) {
    path.push_back(&node);
    if (node.instance_id == instance_id)
        return true;
    for (const HierarchyNode& child : node.children)
        if (hierarchy_path(child, instance_id, path))
            return true;
    path.pop_back();
    return false;
}

const SceneNode* locate_in_hierarchy(const HierarchyInfo& hierarchy, int instance_id,
                                     std::vector<const HierarchyNode*>& path) {
    for (const SceneNode& scene : hierarchy.scenes)
        for (const HierarchyNode& root : scene.roots) {
            path.clear();
            if (hierarchy_path(root, instance_id, path))
                return &scene;
        }
    path.clear();
    return nullptr;
}

std::string join_path(const std::vector<const HierarchyNode*>& path) {
    std::string text;
    for (const HierarchyNode* node : path) {
        if (!text.empty())
            text += '/';
        text += node->name;
    }
    return text;
}

Json label_json(const Labels::Entry& entry) {
    Json value = {{"kind", Labels::kind_name(entry.kind)}, {"type", entry.type_name},
                  {"label", entry.label.name}, {"source", entry.label.source}};
    if (!entry.member.empty())
        value["member"] = entry.member;
    if (!entry.signature.empty())
        value["signature"] = entry.signature;
    if (!entry.label.note.empty())
        value["note"] = entry.label.note;
    if (entry.label.stale)
        value["stale"] = true;
    const std::string_view name = entry.kind == Labels::Kind::Type ? std::string_view(entry.type_name)
                                                                     : std::string_view(entry.member);
    if (Labels::unreadable(name))
        value["alias"] = entry.kind == Labels::Kind::Type ? Labels::humanize(name) : Labels::alias(entry.kind, name);
    return value;
}

std::vector<std::string> inspect_parameter_types(const Inspect::MethodInfo& method) {
    std::vector<std::string> types;
    types.reserve(method.parameters.size());
    for (const Inspect::MethodParamInfo& parameter : method.parameters)
        types.push_back(parameter.type_name);
    return types;
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
    snapshots_.clear();
    snapshot_order_.clear();
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
        fit_to_budget(result);
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
        ((request.tool == "inspect_managed_object" || request.tool == "snapshot_object") &&
         request.arguments.value("include_properties", false))) {
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
        // Lean output: no display for scalars, readable only when false.
        Json result{{"type", value.type_name}, {"kind", value_kind(value.kind)}};
        if (!value.readable)
            result["readable"] = false;
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
        case Inspect::ValueKind::FloatingPoint: {
            // Prefer the float's short text over the widened double.
            double number = value.floating_value;
            if (value.type_name.find("Single") != std::string::npos || value.type_name == "float") {
                char* end = nullptr;
                const double shown = std::strtod(value.display.c_str(), &end);
                if (end != value.display.c_str() && *end == '\0' &&
                    static_cast<float>(shown) == static_cast<float>(number))
                    number = shown;
            }
            result["value"] = number;
            break;
        }
        case Inspect::ValueKind::String:
        case Inspect::ValueKind::Enum:
            result["value"] = value.display;
            break;
        case Inspect::ValueKind::Structured:
            result["display"] = value.display;
            result["components"] = Json::array();
            for (std::size_t index = 0; index < value.component_count; ++index)
                result["components"].push_back(value.components[index]);
            break;
        case Inspect::ValueKind::ArrayReference:
            result["array_length"] = value.array_length;
            [[fallthrough]];
        case Inspect::ValueKind::ObjectReference:
        case Inspect::ValueKind::ValueType:
            result["display"] = value.display;
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
            result["display"] = value.display;
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
                reference.metadata_address = type.metadata_address;
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
        const auto search_offset = optional_offset(request.arguments, "offset");
        if (!search_offset)
            return fail("invalid_arguments", "offset must be a non-negative integer.");
        Json matches = Json::array();
        bool reference_failed = false;
        bool has_more = false;
        std::size_t seen = 0;
        const auto visit = [&](const auto& self, const HierarchyNode& node, const SceneNode& scene,
                               const std::string& parent_path) -> void {
            if (has_more)
                return;
            const std::string path = parent_path.empty() ? node.name : parent_path + "/" + node.name;
            std::string haystack = node.name + " " + path + " " + node.tag + " " +
                std::to_string(node.instance_id);
            for (const std::string& type : node.component_types)
                haystack += " " + type;
            for (const std::string& type : node.dynamic_behaviour_types)
                haystack += " " + type;
            haystack = lowercase(std::move(haystack));
            if ((!active_only || node.active) && (query.empty() || haystack.find(query) != std::string::npos) &&
                seen++ >= *search_offset) {
                if (matches.size() >= static_cast<std::size_t>(*limit)) {
                    has_more = true;
                    return;
                }
                const std::string token = object_reference(node.instance_id, node.name);
                if (token.empty()) {
                    reference_failed = true;
                    return;
                }
                matches.push_back({{"reference", token}, {"instance_id", node.instance_id}, {"name", node.name},
                                   {"tag", node.tag}, {"active", node.active}, {"scene", scene.name}, {"path", path},
                                   {"child_count", node.children.size()}});
            }
            for (const HierarchyNode& child : node.children)
                self(self, child, scene, path);
        };
        for (const SceneNode& scene : hierarchy->scenes)
            for (const HierarchyNode& root : scene.roots)
                visit(visit, root, scene, {});
        if (reference_failed)
            return fail("reference_unavailable", "A secure object reference could not be generated.");
        return succeed({{"matches", std::move(matches)}, {"limit", *limit}, {"offset", *search_offset},
                        {"has_more", has_more}, {"hierarchy_revision", hierarchy->revision}});
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
        const auto find_offset = optional_offset(request.arguments, "offset");
        if (!find_offset)
            return fail("invalid_arguments", "offset must be a non-negative integer.");
        query.limit = static_cast<std::size_t>(*limit) + *find_offset + 1;
        Discovery::SearchResult ranked = Discovery::rank(candidates, query);
        const bool find_has_more = ranked.matches.size() > *find_offset + static_cast<std::size_t>(*limit);
        if (ranked.matches.size() > *find_offset + static_cast<std::size_t>(*limit))
            ranked.matches.resize(*find_offset + static_cast<std::size_t>(*limit));
        ranked.matches.erase(ranked.matches.begin(),
                             ranked.matches.begin() + static_cast<std::ptrdiff_t>(std::min(*find_offset, ranked.matches.size())));

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
                        {"offset", *find_offset}, {"has_more", find_has_more},
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
            Json component_entry{{"component_reference", token}, {"instance_id", component_id},
                                 {"type", type.full_name.empty() ? "<unknown component>" : type.full_name},
                                 {"namespace", type.namespc}, {"class", type.name}};
            annotate_label(component_entry, Labels::Kind::Type, type.full_name);
            components.push_back(std::move(component_entry));
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
        std::vector<const HierarchyNode*> placement;
        if (hierarchy)
            if (const SceneNode* scene = locate_in_hierarchy(*hierarchy, reference->instance_id, placement)) {
                result["scene"] = scene->name;
                result["path"] = join_path(placement);
                result["child_count"] = placement.back()->children.size();
                if (placement.size() > 1) {
                    const HierarchyNode& parent = *placement[placement.size() - 2];
                    result["parent_reference"] = object_reference(parent.instance_id, parent.name);
                    result["parent_name"] = parent.name;
                }
            }
        if (transform) {
            result["local_position"] = vector_json(transform.localPosition());
            result["local_rotation"] = vector_json(transform.localEulerAngles());
            result["local_scale"] = vector_json(transform.localScale());
        }
        if (const char* error = last_error(); error && error[0]) {
            clear_error();
            return fail("object_read_failed", "Unity could not safely read all requested GameObject properties.");
        }
        return succeed(std::move(result));
    }

    if (request.tool == "read_member") {
        const char* reference_key = request.arguments.contains("reference") ? "reference" : "component_reference";
        const bool by_object = request.arguments.contains(reference_key);
        const bool by_type = request.arguments.contains("type_reference");
        if (!request.arguments.contains("member") || !request.arguments["member"].is_string() || by_object == by_type)
            return fail("invalid_arguments",
                        "member and exactly one of reference (a component or managed object) or type_reference (static members) are required.");
        if ((by_object && !request.arguments[reference_key].is_string()) ||
            (by_type && !request.arguments["type_reference"].is_string()))
            return fail("invalid_arguments", "The reference must be a string.");
        const std::string member_name = request.arguments["member"].get<std::string>();
        if (member_name.empty() || member_name.size() > 1024)
            return fail("invalid_arguments", "member must contain between 1 and 1024 characters.");
        if (request.arguments.contains("kind") && !request.arguments["kind"].is_string())
            return fail("invalid_arguments", "kind must be field, property, or auto.");
        const std::string kind = request.arguments.value("kind", std::string("auto"));
        if (kind != "field" && kind != "property" && kind != "auto")
            return fail("invalid_arguments", "kind must be field, property, or auto.");

        Object target;
        RootGuard root;
        const URK::managed::Class* klass = nullptr;
        if (by_object) {
            std::string code;
            std::string message;
            target = resolve_target_reference(request.arguments[reference_key].get_ref<const std::string&>(), root,
                                              code, message);
            if (!target)
                return fail(std::move(code), std::move(message));
            klass = static_cast<const URK::managed::Class*>(
                URK::managed::object_get_class(static_cast<URK::managed::Object*>(target.handle())));
        } else {
            const Reference* type_ref = find_reference(request.arguments["type_reference"].get<std::string>(),
                                                       ReferenceKind::ManagedType, model);
            if (!type_ref)
                return fail("invalid_reference", "The type reference is invalid or expired.");
            Command locate{};
            locate.metadata_address = type_ref->metadata_address;
            locate.image = type_ref->image;
            locate.namespc = type_ref->namespc;
            locate.class_name = type_ref->class_name;
            klass = model.browser_class(locate);
        }
        if (!klass)
            return fail("metadata_unavailable", "The runtime class is unavailable.");
        const std::string type_full_name = Inspect::DescribeClass(klass).full_name;
        const auto owner_of = [&](const auto& member) {
            return member.declaring_type.full_name.empty() ? type_full_name : member.declaring_type.full_name;
        };
        const Object read_target = by_object ? target : Object{};
        Inspect::ValueInfo value{};
        std::string resolved_kind;
        std::string resolved_name;
        std::string resolved_owner;
        clear_error();
        if (kind == "field" || kind == "auto") {
            const auto fields = Inspect::fields_from_class(klass, true);
            if (const Inspect::FieldInfo* found = find_named_member(fields, Labels::Kind::Field, member_name, owner_of)) {
                if (by_type && !found->is_static)
                    return fail("member_not_static", "That field belongs to instances; use reference instead of type_reference.");
                if (found->type_is_opaque)
                    return fail("member_unsafe", "The field has a runtime-specific opaque type and cannot be sampled safely.");
                value = Inspect::ReadField(read_target, *found);
                resolved_kind = "field";
                resolved_name = found->name;
                resolved_owner = owner_of(*found);
            }
        }
        if (resolved_kind.empty() && (kind == "property" || kind == "auto")) {
            if (!has_capability(request.context.capabilities, Capability::PropertyAccess))
                return fail("property_permission_required", "Enable managed property getters in Explorer Config.");
            const auto properties = Inspect::properties_from_class(klass, true);
            if (const Inspect::PropertyInfo* found =
                    find_named_member(properties, Labels::Kind::Property, member_name, owner_of)) {
                if (by_type && !found->is_static)
                    return fail("member_not_static", "That property belongs to instances; use reference instead of type_reference.");
                if (!found->can_read || found->type_is_opaque)
                    return fail("member_unsafe", "The property is not safely readable through runtime metadata.");
                value = Inspect::ReadProperty(read_target, *found);
                resolved_kind = "property";
                resolved_name = found->name;
                resolved_owner = owner_of(*found);
            }
        }
        if (resolved_kind.empty()) {
            clear_error();
            return fail("member_not_found",
                        "No matching field or readable property. The member may be named by its real name, its alias (e.g. f_7cda59) or a saved label.");
        }
        Json result = value_json(value, type_full_name + "." + resolved_name);
        result["member"] = resolved_name;
        result["member_kind"] = resolved_kind;
        result["declaring_type"] = resolved_owner;
        annotate_label(result, resolved_kind == "field" ? Labels::Kind::Field : Labels::Kind::Property, resolved_owner,
                       resolved_name);
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
        // A component can lead back to the GameObject that carries it.
        std::string owner_reference_token;
        if (const auto raw = references_.find(request.arguments["reference"].get<std::string>());
            raw != references_.end() && raw->second.kind == ReferenceKind::Component)
            owner_reference_token = object_reference(raw->second.owner_instance_id, raw->second.expected_object_name);
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
            // Omit default values.
            if (!member.declaring_type.empty() && member.declaring_type != page.type.full_name)
                entry["declaring_type"] = member.declaring_type;
            if (member.is_static)
                entry["static"] = true;
            if (!member.runtime_safe)
                entry["runtime_safe"] = false;
            annotate_label(entry, member.property ? Labels::Kind::Property : Labels::Kind::Field,
                           member.declaring_type.empty() ? page.type.full_name : member.declaring_type, member.name);
            if (!member.capability_reason.empty())
                entry["capability_reason"] = member.capability_reason;
            fields.push_back(std::move(entry));
        }
        Json properties = Json::array();
        for (ManagedRead::MemberValue member : page.properties) {
            Json entry = value_json(member.value, page.type.full_name + "." + member.name);
            entry["name"] = member.name;
            // Omit default values.
            if (!member.declaring_type.empty() && member.declaring_type != page.type.full_name)
                entry["declaring_type"] = member.declaring_type;
            if (member.is_static)
                entry["static"] = true;
            if (!member.runtime_safe)
                entry["runtime_safe"] = false;
            annotate_label(entry, member.property ? Labels::Kind::Property : Labels::Kind::Field,
                           member.declaring_type.empty() ? page.type.full_name : member.declaring_type, member.name);
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
                        {"owner_reference", owner_reference_token.empty() ? Json(nullptr) : Json(owner_reference_token)},
                        {"matching_members", page.matching_members}, {"truncated", page.truncated},
                        {"member_limit", *limit}});
    }

    if (request.tool == "snapshot_object" || request.tool == "diff_snapshot") {
        constexpr std::size_t kMaxSnapshots = 16;
        constexpr std::size_t kMaxReportedChanges = 200;
        const bool diffing = request.tool == "diff_snapshot";
        const auto string_argument = [&](const char* key) -> const std::string* {
            const auto found = request.arguments.find(key);
            return found != request.arguments.end() && found->is_string()
                ? &found->get_ref<const std::string&>() : nullptr;
        };

        // Compare plain values; reference tokens change on every read.
        const auto plain_value = [](Inspect::ValueInfo& value) -> Json {
            Json result;
            switch (value.kind) {
            case Inspect::ValueKind::Null: result = nullptr; break;
            case Inspect::ValueKind::Boolean: result = value.bool_value; break;
            case Inspect::ValueKind::SignedInteger: result = value.signed_value; break;
            case Inspect::ValueKind::UnsignedInteger: result = value.unsigned_value; break;
            case Inspect::ValueKind::FloatingPoint: result = value.floating_value; break;
            case Inspect::ValueKind::Structured:
                result = Json::array();
                for (std::size_t index = 0; index < value.component_count; ++index)
                    result.push_back(value.components[index]);
                break;
            default: result = value.display; break;
            }
            value.object = nullptr;
            return result;
        };
        const auto read_snapshot = [&](Object target,
                                       const ObjectSnapshot& shape,
                                       ObjectSnapshot& out,
                                       bool& truncated,
                                       std::string& code,
                                       std::string& message) {
            ManagedRead::ObjectPage page = ManagedRead::read_object(
                target, shape.member_query, shape.member_limit, shape.include_properties);
            if (!page.type.handle) {
                code = "metadata_unavailable";
                message = "The managed object's runtime type could not be described.";
                return false;
            }
            if (Inspect::type_name_looks_array(page.type.full_name)) {
                code = "array_requires_paging";
                message = "Snapshots cover objects; use read_array for managed arrays.";
                return false;
            }
            out = shape;
            out.type_name = page.type.full_name;
            out.members.clear();
            for (auto* group : {&page.fields, &page.properties}) {
                for (ManagedRead::MemberValue& member : *group) {
                    Json value = plain_value(member.value);
                    out.members.push_back({member.name, member.declaring_type, std::move(value)});
                }
            }
            truncated = page.truncated;
            return true;
        };

        if (!diffing) {
            const std::string* reference = string_argument("reference");
            if (!reference)
                return fail("invalid_arguments", "reference is required.");
            if (request.arguments.contains("member_query") &&
                (!request.arguments["member_query"].is_string() ||
                 request.arguments["member_query"].get_ref<const std::string&>().size() > 256))
                return fail("invalid_arguments", "member_query must be a string of at most 256 characters.");
            if (request.arguments.contains("include_properties") &&
                !request.arguments["include_properties"].is_boolean())
                return fail("invalid_arguments", "include_properties must be a boolean.");
            const auto limit = requested_limit(request.arguments, "member_limit", 250, 250);
            if (!limit)
                return fail("invalid_arguments", "member_limit must be between 1 and 250.");
            ObjectSnapshot shape;
            shape.origin_reference = *reference;
            shape.member_query = request.arguments.value("member_query", std::string{});
            shape.member_limit = static_cast<std::size_t>(*limit);
            shape.include_properties = request.arguments.value("include_properties", false);
            RootGuard owner_root;
            std::string code;
            std::string message;
            const Object target = resolve_target_reference(*reference, owner_root, code, message);
            if (!target)
                return fail(std::move(code), std::move(message));
            ObjectSnapshot taken;
            bool truncated = false;
            if (!read_snapshot(target, shape, taken, truncated, code, message))
                return fail(std::move(code), std::move(message));
            std::array<unsigned char, 8> random{};
            if (BCryptGenRandom(nullptr, random.data(), static_cast<ULONG>(random.size()),
                                BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
                return fail("reference_unavailable", "A snapshot reference could not be generated.");
            static constexpr char hex[] = "0123456789abcdef";
            std::string token = "urksnap_";
            for (const unsigned char byte : random) {
                token.push_back(hex[byte >> 4]);
                token.push_back(hex[byte & 0x0f]);
            }
            while (snapshots_.size() >= kMaxSnapshots && !snapshot_order_.empty()) {
                snapshots_.erase(snapshot_order_.front());
                snapshot_order_.pop_front();
            }
            const std::size_t count = taken.members.size();
            const std::string type_name = taken.type_name;
            snapshots_[token] = std::move(taken);
            snapshot_order_.push_back(token);
            return succeed({{"snapshot_reference", token}, {"type", type_name}, {"members", count},
                            {"truncated", truncated}});
        }

        const std::string* baseline_token = string_argument("snapshot_reference");
        if (!baseline_token)
            return fail("invalid_arguments", "snapshot_reference is required.");
        if (request.arguments.contains("against") && !string_argument("against"))
            return fail("invalid_arguments", "against must be a snapshot reference.");
        if (request.arguments.contains("rebase") && !request.arguments["rebase"].is_boolean())
            return fail("invalid_arguments", "rebase must be a boolean.");
        if (request.arguments.contains("epsilon") && !request.arguments["epsilon"].is_number())
            return fail("invalid_arguments", "epsilon must be a non-negative number.");
        const double epsilon = request.arguments.value("epsilon", 0.0);
        const auto baseline_found = snapshots_.find(*baseline_token);
        if (baseline_found == snapshots_.end())
            return fail("invalid_reference", "Unknown or evicted snapshot; take a new one.");
        ObjectSnapshot& baseline = baseline_found->second;
        const std::string* against_token = string_argument("against");
        ObjectSnapshot current;
        bool truncated = false;
        if (against_token) {
            const auto other = snapshots_.find(*against_token);
            if (other == snapshots_.end())
                return fail("invalid_reference", "Unknown or evicted comparison snapshot.");
            current = other->second;
        } else {
            const bool needs_properties = baseline.include_properties;
            if (needs_properties && !has_capability(request.context.capabilities, Capability::PropertyAccess))
                return fail("property_permission_required", "Enable managed property getters in the MCP menu.");
            RootGuard owner_root;
            std::string code;
            std::string message;
            const Object target =
                resolve_target_reference(baseline.origin_reference, owner_root, code, message);
            if (!target)
                return fail(std::move(code), std::move(message));
            if (!read_snapshot(target, baseline, current, truncated, code, message))
                return fail(std::move(code), std::move(message));
        }

        const auto key_of = [](const SnapshotMember& member) {
            return member.declaring_type + "." + member.name;
        };
        std::unordered_map<std::string, const SnapshotMember*> before;
        for (const SnapshotMember& member : baseline.members)
            before.emplace(key_of(member), &member);
        const auto same = [&](const Json& left, const Json& right) {
            if (left == right)
                return true;
            return epsilon > 0.0 && left.is_number() && right.is_number() &&
                std::fabs(left.get<double>() - right.get<double>()) <= epsilon;
        };
        Json changed = Json::array();
        Json added = Json::array();
        Json removed = Json::array();
        std::size_t unchanged = 0;
        bool changes_truncated = false;
        std::unordered_set<std::string> seen;
        for (const SnapshotMember& member : current.members) {
            const std::string key = key_of(member);
            seen.insert(key);
            const auto old = before.find(key);
            if (old == before.end()) {
                added.push_back(member.name);
            } else if (same(old->second->value, member.value)) {
                ++unchanged;
            } else if (changed.size() < kMaxReportedChanges) {
                changed.push_back({{"member", member.name},
                                   {"from", old->second->value},
                                   {"to", member.value}});
            } else {
                changes_truncated = true;
            }
        }
        for (const SnapshotMember& member : baseline.members)
            if (!seen.contains(key_of(member)))
                removed.push_back(member.name);

        Json result = {{"type", current.type_name},
                       {"changed", std::move(changed)},
                       {"unchanged", unchanged}};
        if (!added.empty())
            result["added"] = std::move(added);
        if (!removed.empty())
            result["removed"] = std::move(removed);
        if (changes_truncated || truncated)
            result["truncated"] = true;
        if (request.arguments.value("rebase", false) && !against_token)
            baseline.members = std::move(current.members);
        return succeed(std::move(result));
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
                return type_reference->metadata_address != 0
                    ? candidate.metadata_address == type_reference->metadata_address
                    : candidate.image == type_reference->image &&
                        candidate.namespc == type_reference->namespc &&
                        candidate.class_name == type_reference->class_name;
            });
        if (type == model.class_browser_catalog_->classes.end())
            return fail("type_unavailable", "The referenced managed type is no longer loaded.");
        Command command{.kind = CommandKind::FindClassInstances};
        command.image = type->image;
        command.namespc = type->namespc;
        command.class_name = type->class_name;
        command.metadata_address = type->metadata_address;
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

    if (request.tool == "list_labels") {
        const auto limit = requested_limit(request.arguments, "limit", 100, 500);
        if (!limit)
            return fail("invalid_arguments", "limit must be an integer between 1 and 500.");
        const std::string query = lowercase(request.arguments.value("query", std::string{}));
        std::vector<Labels::Entry> entries = Labels::list();
        std::sort(entries.begin(), entries.end(), [](const Labels::Entry& left, const Labels::Entry& right) {
            return std::tie(left.type_name, left.member, left.signature) <
                   std::tie(right.type_name, right.member, right.signature);
        });
        Json labels = Json::array();
        std::size_t matched = 0;
        std::size_t result_bytes = 0;
        bool truncated = false;
        for (const Labels::Entry& entry : entries) {
            Json item = label_json(entry);
            const std::string text = item.dump();
            if (!query.empty() && lowercase(text).find(query) == std::string::npos)
                continue;
            ++matched;
            result_bytes += text.size();
            if (labels.size() >= static_cast<std::size_t>(*limit) || result_bytes > kResultBudgetBytes) {
                truncated = true;
                continue;
            }
            labels.push_back(std::move(item));
        }
        return succeed({{"labels", std::move(labels)}, {"matched", matched}, {"total", entries.size()},
                        {"truncated", truncated}, {"storage_path", Labels::storage_path()}});
    }

    if (request.tool == "set_label" || request.tool == "remove_label") {
        // Resolves the target against loaded metadata, so a garbled or mistyped
        // obfuscated name fails here instead of saving a label nothing matches.
        const std::optional<Labels::Kind> kind = Labels::parse_kind(request.arguments.value("kind", std::string{}));
        if (!kind)
            return fail("invalid_arguments", "kind must be type, field, property or method.");
        const std::string requested_type = request.arguments.value("type", std::string{});
        const std::string requested_member = request.arguments.value("member", std::string{});
        const std::string requested_alias = request.arguments.value("alias", std::string{});
        const bool all_overloads = request.arguments.value("all_overloads", false);
        std::optional<std::vector<std::string>> parameter_types;
        if (request.arguments.contains("parameter_types")) {
            const Json& values = request.arguments["parameter_types"];
            if (!values.is_array())
                return fail("invalid_arguments", "parameter_types must be an array of type names.");
            parameter_types.emplace();
            for (const Json& value : values) {
                if (!value.is_string())
                    return fail("invalid_arguments", "parameter_types must be an array of type names.");
                parameter_types->push_back(value.get<std::string>());
            }
        }
        if (requested_type.empty())
            return fail("invalid_arguments", "type is required.");
        if (*kind != Labels::Kind::Type && requested_member.empty() && requested_alias.empty())
            return fail("invalid_arguments", "member or alias is required for a field, property or method label.");

        if (!model.class_browser_catalog_)
            model.load_class_browser_catalog();
        if (!model.class_browser_catalog_)
            return fail("metadata_unavailable", "Loaded managed types could not be enumerated.");
        const BrowserClassInfo* type_info = nullptr;
        for (const BrowserClassInfo& candidate : model.class_browser_catalog_->classes)
            if (candidate.full_name == requested_type) {
                type_info = &candidate;
                break;
            }
        if (!type_info)
            for (const BrowserClassInfo& candidate : model.class_browser_catalog_->classes)
                if (Labels::humanize(candidate.full_name) == requested_type) {
                    type_info = &candidate;
                    break;
                }
        if (!type_info)
            return fail("type_not_found",
                        "No loaded type has that full name. Use the full_name (or alias) that search_types reports.");

        std::string label_type = type_info->full_name;
        std::string label_member;
        std::string label_signature;
        if (*kind != Labels::Kind::Type) {
            Command load{.kind = CommandKind::LoadClassBrowserMembers};
            load.metadata_address = type_info->metadata_address;
            load.image = type_info->image;
            load.namespc = type_info->namespc;
            load.class_name = type_info->class_name;
            model.load_class_browser_members(load);
            const auto members = model.working_.class_browser_members;
            if (!members)
                return fail("metadata_unavailable", "Members for the type could not be loaded.");
            const auto names_match = [&](const std::string& name) {
                return !requested_member.empty() ? name == requested_member
                                                 : Labels::alias(*kind, name) == requested_alias;
            };
            const auto owner = [&](const std::string& declaring_type) {
                return declaring_type.empty() ? type_info->full_name : declaring_type;
            };
            bool found = false;
            if (*kind == Labels::Kind::Field) {
                for (const ComponentInfo::Field& field : members->fields)
                    if (names_match(field.name)) {
                        label_type = owner(field.declaring_type);
                        label_member = field.name;
                        found = true;
                        break;
                    }
            } else if (*kind == Labels::Kind::Property) {
                for (const ComponentInfo::Property& property : members->properties)
                    if (names_match(property.name)) {
                        label_type = owner(property.declaring_type);
                        label_member = property.name;
                        found = true;
                        break;
                    }
            } else {
                std::vector<const ComponentInfo::Method*> overloads;
                for (const ComponentInfo::Method& method : members->methods)
                    if (names_match(method.name))
                        overloads.push_back(&method);
                const auto same_parameters = [&](const ComponentInfo::Method& method) {
                    if (method.parameter_types.size() != parameter_types->size())
                        return false;
                    for (std::size_t index = 0; index < method.parameter_types.size(); ++index) {
                        const std::string& wanted = (*parameter_types)[index];
                        if (method.parameter_types[index] != wanted &&
                            Labels::humanize(method.parameter_types[index]) != wanted)
                            return false;
                    }
                    return true;
                };
                const ComponentInfo::Method* chosen = nullptr;
                if (parameter_types) {
                    for (const ComponentInfo::Method* method : overloads)
                        if (same_parameters(*method)) {
                            chosen = method;
                            break;
                        }
                    if (!overloads.empty() && !chosen)
                        return fail("overload_not_found", "No overload of that method has those parameter_types.");
                } else if (overloads.size() == 1 || (all_overloads && !overloads.empty())) {
                    chosen = overloads.front();
                } else if (overloads.size() > 1) {
                    std::string signatures;
                    for (const ComponentInfo::Method* method : overloads)
                        signatures += "\n(" + Labels::method_signature(method->parameter_types) + ")";
                    return fail("ambiguous_overload",
                                "That method has several overloads; pass parameter_types or all_overloads=true:" +
                                    signatures);
                }
                if (chosen) {
                    label_type = owner(chosen->declaring_type);
                    label_member = chosen->name;
                    label_signature =
                        all_overloads ? std::string{} : Labels::method_signature(chosen->parameter_types);
                    found = true;
                }
            }
            if (!found)
                return fail("member_not_found",
                            std::string("No ") + Labels::kind_name(*kind) +
                                " with that name on the type. Pass the alias that inspect_type reports for unreadable names.");
        }

        Json target = {{"kind", Labels::kind_name(*kind)}, {"type", label_type}};
        if (!label_member.empty())
            target["member"] = label_member;
        if (!label_signature.empty())
            target["signature"] = label_signature;
        std::string error;
        if (request.tool == "remove_label") {
            // A method label may live on the overload or be shared by all of them.
            const bool removed =
                Labels::remove(*kind, label_type, label_member, label_signature, &error) ||
                (!label_signature.empty() && Labels::remove(*kind, label_type, label_member, {}, &error));
            if (!removed)
                return fail("label_not_found", error.empty() ? "No label is saved for that target." : error);
            target["removed"] = true;
            return succeed(std::move(target));
        }
        Labels::Entry entry;
        entry.kind = *kind;
        entry.type_name = label_type;
        entry.member = label_member;
        entry.signature = label_signature;
        entry.label.name = request.arguments.value("label", std::string{});
        entry.label.note = request.arguments.value("note", std::string{});
        entry.label.source = "mcp";
        if (!Labels::set(std::move(entry), &error))
            return fail("label_not_saved", error);
        annotate_label(target, *kind, label_type, label_member, label_signature);
        target["saved"] = true;
        return succeed(std::move(target));
    }

    const auto issue_type_reference = [&](const BrowserClassInfo& type) {
        Reference reference{ReferenceKind::ManagedType, 0, 0, 0, scene_generation_, hierarchy_revision_};
        reference.image = type.image;
        reference.namespc = type.namespc;
        reference.class_name = type.class_name;
        reference.metadata_address = type.metadata_address;
        return issue_reference(model, std::move(reference));
    };
    const auto ensure_catalog = [&]() {
        if (!model.class_browser_catalog_)
            model.load_class_browser_catalog();
        return model.class_browser_catalog_ != nullptr;
    };
    const auto object_summary = [&](const HierarchyNode& node, const std::string& path, const SceneNode* scene) {
        Json entry{{"reference", object_reference(node.instance_id, node.name)}, {"instance_id", node.instance_id},
                   {"name", node.name}, {"path", path}, {"active", node.active}, {"tag", node.tag},
                   {"child_count", node.children.size()}, {"component_count", node.component_types.size()}};
        if (scene)
            entry["scene"] = scene->name;
        Json components = Json::array();
        for (std::size_t index = 0; index < node.component_types.size() && index < 8; ++index)
            components.push_back(node.component_types[index]);
        entry["components"] = std::move(components);
        if (node.component_types.size() > 8)
            entry["components_truncated"] = true;
        return entry;
    };
    const auto reference_kind_name = [](ReferenceKind kind) -> const char* {
        switch (kind) {
        case ReferenceKind::GameObject: return "game_object";
        case ReferenceKind::Component: return "component";
        case ReferenceKind::ManagedObject: return "managed_object";
        case ReferenceKind::GraphNode: return "graph_node";
        case ReferenceKind::ManagedType: return "type";
        case ReferenceKind::ManagedMethod: return "method";
        case ReferenceKind::MethodTrace: return "method_trace";
        case ReferenceKind::InstanceScan: return "instance_scan";
        }
        return "unknown";
    };

    if (request.tool == "list_scenes") {
        if (!hierarchy)
            return fail("runtime_unavailable", "The Explorer hierarchy is not available yet.");
        Json scenes = Json::array();
        for (const SceneNode& scene : hierarchy->scenes)
            scenes.push_back({{"name", scene.name}, {"handle", scene.handle}, {"active", scene.active},
                              {"dont_destroy_on_load", scene.dont_destroy_on_load},
                              {"hide_and_dont_save", scene.hide_and_dont_save}, {"root_count", scene.roots.size()}});
        Json build_scenes = Json::array();
        for (const SceneLoadInfo& scene : hierarchy->available_scenes)
            build_scenes.push_back({{"build_index", scene.build_index}, {"name", scene.name}, {"path", scene.path},
                                    {"loaded", scene.loaded}, {"active", scene.active}});
        return succeed({{"scenes", std::move(scenes)}, {"build_scenes", std::move(build_scenes)},
                        {"object_count", hierarchy->objects}, {"hierarchy_revision", hierarchy->revision},
                        {"next", "Use browse_hierarchy with scene=<name> to list a scene's roots, or with reference=<GameObject> to step into it."}});
    }

    if (request.tool == "browse_hierarchy") {
        if (!hierarchy)
            return fail("runtime_unavailable", "The Explorer hierarchy is not available yet.");
        const auto offset = optional_offset(request.arguments, "offset");
        const auto limit = requested_limit(request.arguments, "limit", 50, 100);
        if (!offset || !limit)
            return fail("invalid_arguments", "offset must be non-negative and limit must be between 1 and 100.");
        if (request.arguments.contains("scene") && !request.arguments["scene"].is_string())
            return fail("invalid_arguments", "scene must be a string.");
        const std::string scene_filter = lowercase(request.arguments.value("scene", std::string{}));
        Json result = Json::object();
        std::vector<std::pair<const HierarchyNode*, const SceneNode*>> entries;
        std::string parent_path;
        if (request.arguments.contains("reference")) {
            const auto reference = resolve_requested_object();
            if (!reference) {
                if (requested_object_lookup == ReferenceLookup::Expired)
                    return fail("reference_expired", "The GameObject reference belongs to an earlier scene generation. Browse from a scene root again.");
                if (requested_object_lookup == ReferenceLookup::WrongKind)
                    return fail("reference_kind_mismatch", "The supplied opaque reference does not identify a GameObject.");
                return fail("invalid_reference", "The GameObject reference is unknown or was evicted.");
            }
            std::vector<const HierarchyNode*> path;
            const SceneNode* scene = locate_in_hierarchy(*hierarchy, reference->instance_id, path);
            if (!scene)
                return fail("object_not_in_hierarchy",
                            "The object is not in the current hierarchy snapshot. It may be new or destroyed; refresh and browse again.");
            const HierarchyNode& node = *path.back();
            parent_path = join_path(path);
            result["object"] = object_summary(node, parent_path, scene);
            result["scene"] = scene->name;
            if (path.size() > 1) {
                std::vector<const HierarchyNode*> parent_chain(path.begin(), path.end() - 1);
                result["parent"] = object_summary(*parent_chain.back(), join_path(parent_chain), scene);
            } else {
                result["parent"] = nullptr;
            }
            Json ancestors = Json::array();
            const std::size_t first_ancestor = path.size() > 25 ? path.size() - 25 : 0;
            for (std::size_t index = first_ancestor; index + 1 < path.size(); ++index)
                ancestors.push_back({{"reference", object_reference(path[index]->instance_id, path[index]->name)},
                                     {"name", path[index]->name}});
            result["ancestors"] = std::move(ancestors);
            for (const HierarchyNode& child : node.children)
                entries.emplace_back(&child, scene);
        } else {
            bool scene_found = scene_filter.empty();
            for (const SceneNode& scene : hierarchy->scenes) {
                if (!scene_filter.empty() && lowercase(scene.name) != scene_filter)
                    continue;
                scene_found = true;
                for (const HierarchyNode& root : scene.roots)
                    entries.emplace_back(&root, &scene);
            }
            if (!scene_found)
                return fail("scene_not_found", "No loaded scene has that name. Call list_scenes for the names.");
            result["scene"] = scene_filter.empty() ? Json(nullptr) : Json(request.arguments["scene"]);
        }
        const std::size_t first = std::min(*offset, entries.size());
        const std::size_t last = std::min(entries.size(), first + static_cast<std::size_t>(*limit));
        Json children = Json::array();
        for (std::size_t index = first; index < last; ++index) {
            const std::string path = parent_path.empty() ? entries[index].first->name
                                                         : parent_path + "/" + entries[index].first->name;
            children.push_back(object_summary(*entries[index].first, path, entries[index].second));
        }
        result["children"] = std::move(children);
        result["total_children"] = entries.size();
        result["offset"] = first;
        result["has_more"] = last < entries.size();
        result["hierarchy_revision"] = hierarchy->revision;
        return succeed(std::move(result));
    }

    if (request.tool == "select_game_object") {
        const auto reference = resolve_requested_object();
        if (!reference || !request.arguments.contains("reference"))
            return fail("invalid_reference", "Provide a known GameObject reference.");
        Command command{.kind = CommandKind::Select};
        command.instance_id = reference->instance_id;
        command.scene_generation = reference->scene_generation;
        command.hierarchy_revision = reference->hierarchy_revision;
        model.process_command(command);
        model.publish();
        return succeed({{"reference", request.arguments["reference"]}, {"status", model.working_.status},
                        {"selected", model.working_.selected_instance_id == reference->instance_id}});
    }

    if (request.tool == "describe_reference") {
        if (!request.arguments.contains("reference") || !request.arguments["reference"].is_string())
            return fail("invalid_arguments", "reference is required.");
        const std::string token = request.arguments["reference"].get<std::string>();
        const auto raw = references_.find(token);
        if (raw == references_.end())
            return succeed({{"valid", false}, {"reason", "unknown_or_evicted"},
                            {"hint", "Search again to obtain a fresh reference."}});
        const Reference& described = raw->second;
        Json result{{"valid", true}, {"kind", reference_kind_name(described.kind)}};
        if (described.kind == ReferenceKind::GameObject || described.kind == ReferenceKind::Component) {
            ReferenceLookup lookup = ReferenceLookup::Found;
            find_reference(token, described.kind, model, &lookup);
            if (lookup == ReferenceLookup::Expired) {
                result["valid"] = false;
                result["reason"] = "expired_scene_changed";
            }
        }
        if (described.kind == ReferenceKind::GameObject) {
            result["name"] = described.expected_object_name;
            result["instance_id"] = described.instance_id;
            std::vector<const HierarchyNode*> path;
            if (hierarchy && locate_in_hierarchy(*hierarchy, described.instance_id, path)) {
                result["path"] = join_path(path);
                result["child_count"] = path.back()->children.size();
            }
        } else if (described.kind == ReferenceKind::Component) {
            result["type"] = described.expected_component_type;
            result["instance_id"] = described.instance_id;
            result["owner_name"] = described.expected_object_name;
            if (result.value("valid", false)) {
                const std::string owner = object_reference(described.owner_instance_id, described.expected_object_name);
                if (!owner.empty())
                    result["owner_reference"] = owner;
            }
        } else if (described.kind == ReferenceKind::ManagedObject) {
            RootGuard root;
            std::string code;
            std::string message;
            const Object object = resolve_target_reference(token, root, code, message);
            result["source"] = described.expected_object_name;
            result["expected_type"] = described.expected_component_type;
            if (!object) {
                result["valid"] = false;
                result["reason"] = code;
                result["detail"] = message;
            } else {
                const Inspect::TypeInfo type = Inspect::TypeOf(object);
                result["type"] = type.full_name;
                result["is_array"] = Inspect::type_name_looks_array(type.full_name);
                result["is_value_type"] = type.is_value_type;
            }
        } else if (described.kind == ReferenceKind::ManagedType || described.kind == ReferenceKind::ManagedMethod) {
            result["image"] = described.image;
            result["namespace"] = described.namespc;
            result["class"] = described.class_name;
            if (described.kind == ReferenceKind::ManagedMethod)
                result["member_index"] = described.member_index;
            if (ensure_catalog())
                for (const BrowserClassInfo& candidate : model.class_browser_catalog_->classes)
                    if (described.metadata_address != 0 ? candidate.metadata_address == described.metadata_address
                                                        : candidate.image == described.image &&
                                                              candidate.namespc == described.namespc &&
                                                              candidate.class_name == described.class_name) {
                        result["full_name"] = candidate.full_name;
                        break;
                    }
        }
        return succeed(std::move(result));
    }

    if (request.tool == "list_assemblies") {
        const auto offset = optional_offset(request.arguments, "offset");
        const auto limit = requested_limit(request.arguments, "limit", 100, 500);
        if (!offset || !limit)
            return fail("invalid_arguments", "offset must be non-negative and limit must be between 1 and 500.");
        if (!ensure_catalog())
            return fail("metadata_unavailable", "Loaded managed types could not be enumerated.");
        const std::string query = lowercase(request.arguments.value("query", std::string{}));
        std::map<std::string, std::array<std::size_t, 2>> images;
        for (const BrowserClassInfo& type : model.class_browser_catalog_->classes) {
            auto& counts = images[type.image];
            ++counts[0];
            if (type.is_component)
                ++counts[1];
        }
        Json assemblies = Json::array();
        std::size_t matched = 0;
        for (const auto& [image, counts] : images) {
            if (!query.empty() && lowercase(image).find(query) == std::string::npos)
                continue;
            if (matched++ < *offset || assemblies.size() >= static_cast<std::size_t>(*limit))
                continue;
            assemblies.push_back({{"image", image}, {"type_count", counts[0]}, {"component_count", counts[1]}});
        }
        const bool more_assemblies = matched > *offset + assemblies.size();
        return succeed({{"assemblies", std::move(assemblies)}, {"matched", matched}, {"offset", *offset},
                        {"has_more", more_assemblies},
                        {"next", "list_namespaces(image) shows an assembly's namespaces; search_types(image, namespace) lists its types."}});
    }

    if (request.tool == "list_namespaces") {
        const auto offset = optional_offset(request.arguments, "offset");
        const auto limit = requested_limit(request.arguments, "limit", 100, 500);
        if (!offset || !limit)
            return fail("invalid_arguments", "offset must be non-negative and limit must be between 1 and 500.");
        if (!ensure_catalog())
            return fail("metadata_unavailable", "Loaded managed types could not be enumerated.");
        const std::string image_query = lowercase(request.arguments.value("image", std::string{}));
        const std::string query = lowercase(request.arguments.value("query", std::string{}));
        std::map<std::pair<std::string, std::string>, std::size_t> namespaces;
        for (const BrowserClassInfo& type : model.class_browser_catalog_->classes) {
            if (!image_query.empty() && lowercase(type.image).find(image_query) == std::string::npos)
                continue;
            if (!query.empty() && lowercase(type.namespc).find(query) == std::string::npos)
                continue;
            ++namespaces[{type.image, type.namespc}];
        }
        Json items = Json::array();
        std::size_t matched = 0;
        for (const auto& [key, count] : namespaces) {
            if (matched++ < *offset || items.size() >= static_cast<std::size_t>(*limit))
                continue;
            items.push_back({{"image", key.first}, {"namespace", key.second}, {"type_count", count}});
        }
        const bool more_namespaces = matched > *offset + items.size();
        return succeed({{"namespaces", std::move(items)}, {"matched", matched}, {"offset", *offset},
                        {"has_more", more_namespaces}});
    }

    if (request.tool == "get_type_hierarchy") {
        if (!request.arguments.contains("type_reference") || !request.arguments["type_reference"].is_string())
            return fail("invalid_arguments", "type_reference is required.");
        const Reference* reference = find_reference(request.arguments["type_reference"].get<std::string>(),
                                                    ReferenceKind::ManagedType, model);
        if (!reference)
            return fail("invalid_reference", "The type reference is invalid or expired.");
        const auto offset = optional_offset(request.arguments, "derived_offset");
        const auto limit = requested_limit(request.arguments, "derived_limit", 50, 100);
        if (!offset || !limit)
            return fail("invalid_arguments", "derived_offset must be non-negative and derived_limit must be between 1 and 100.");
        if (!ensure_catalog())
            return fail("metadata_unavailable", "Loaded managed types could not be enumerated.");
        const auto& classes = model.class_browser_catalog_->classes;
        const auto self = std::find_if(classes.begin(), classes.end(), [&](const BrowserClassInfo& candidate) {
            return reference->metadata_address != 0
                ? candidate.metadata_address == reference->metadata_address
                : candidate.image == reference->image && candidate.namespc == reference->namespc &&
                      candidate.class_name == reference->class_name;
        });
        if (self == classes.end())
            return fail("type_unavailable", "The referenced managed type is no longer loaded.");
        std::unordered_map<std::string, const BrowserClassInfo*> by_name;
        by_name.reserve(classes.size());
        for (const BrowserClassInfo& candidate : classes)
            by_name.emplace(candidate.full_name, &candidate);
        const auto describe = [&](const BrowserClassInfo& type) {
            Json entry{{"type_reference", issue_type_reference(type)}, {"full_name", type.full_name}, {"image", type.image},
                       {"is_component", type.is_component}, {"is_abstract", type.is_abstract},
                       {"is_interface", type.is_interface}};
            annotate_label(entry, Labels::Kind::Type, type.full_name);
            return entry;
        };
        Json base_chain = Json::array();
        const BrowserClassInfo* cursor = &*self;
        for (int depth = 0; depth < 32 && !cursor->parent_name.empty(); ++depth) {
            const auto parent = by_name.find(cursor->parent_name);
            if (parent == by_name.end()) {
                base_chain.push_back({{"full_name", cursor->parent_name}, {"loaded", false}});
                break;
            }
            base_chain.push_back(describe(*parent->second));
            cursor = parent->second;
        }
        Json derived = Json::array();
        std::size_t derived_total = 0;
        for (const BrowserClassInfo& candidate : classes) {
            if (candidate.parent_name != self->full_name)
                continue;
            if (derived_total++ < *offset || derived.size() >= static_cast<std::size_t>(*limit))
                continue;
            derived.push_back(describe(candidate));
        }
        Json result = describe(*self);
        result["parent"] = self->parent_name;
        result["interfaces"] = self->interfaces;
        result["declaring_type"] = self->declaring_type_name;
        result["is_value_type"] = self->is_value_type;
        result["is_enum"] = self->is_enum;
        result["is_static"] = self->is_static;
        result["base_chain"] = std::move(base_chain);
        result["derived"] = std::move(derived);
        result["derived_total"] = derived_total;
        result["derived_offset"] = *offset;
        result["derived_has_more"] = derived_total > *offset + result["derived"].size();
        return succeed(std::move(result));
    }

    if (request.tool == "read_static_members") {
        if (!request.arguments.contains("type_reference") || !request.arguments["type_reference"].is_string())
            return fail("invalid_arguments", "type_reference is required.");
        const Reference* reference = find_reference(request.arguments["type_reference"].get<std::string>(),
                                                    ReferenceKind::ManagedType, model);
        if (!reference)
            return fail("invalid_reference", "The type reference is invalid or expired.");
        const auto limit = requested_limit(request.arguments, "member_limit", 100, 250);
        if (!limit)
            return fail("invalid_arguments", "member_limit must be an integer between 1 and 250.");
        if (request.arguments.contains("include_properties") && !request.arguments["include_properties"].is_boolean())
            return fail("invalid_arguments", "include_properties must be a boolean.");
        const bool include_properties = request.arguments.value("include_properties", false);
        if (include_properties && !has_capability(request.context.capabilities, Capability::PropertyAccess))
            return fail("property_permission_required", "Enable 'Allow managed property getters' in Explorer Config.");
        Command locate{};
        locate.metadata_address = reference->metadata_address;
        locate.image = reference->image;
        locate.namespc = reference->namespc;
        locate.class_name = reference->class_name;
        const auto* klass = model.browser_class(locate);
        if (!klass)
            return fail("type_unavailable", "The referenced managed type is no longer loaded.");
        const Inspect::TypeInfo type = Inspect::DescribeClass(klass);
        const std::string query = lowercase(request.arguments.value("member_query", std::string{}));
        const auto accepts = [&](Labels::Kind kind, const std::string& name, const std::string& type_name,
                                 const std::string& declaring) {
            if (query.empty())
                return true;
            return lowercase(name + " " + type_name + " " + declaring + " " +
                             Labels::search_text(kind, declaring.empty() ? type.full_name : declaring, name))
                       .find(query) != std::string::npos;
        };
        Json fields = Json::array();
        Json properties = Json::array();
        std::size_t matching = 0;
        bool truncated = false;
        for (const Inspect::FieldInfo& field : Inspect::fields_from_class(klass, true)) {
            if (!field.is_static || !accepts(Labels::Kind::Field, field.name, field.type_name, field.declaring_type.full_name))
                continue;
            ++matching;
            if (fields.size() + properties.size() >= static_cast<std::size_t>(*limit)) {
                truncated = true;
                continue;
            }
            clear_error();
            Inspect::ValueInfo value = field.type_is_opaque
                ? Inspect::unavailable_value(field.type_name, "Runtime-specific type; generic reading is unsafe.")
                : Inspect::ReadField({}, field);
            Json entry = value_json(value, type.full_name + "." + field.name);
            entry["name"] = field.name;
            entry["declaring_type"] = field.declaring_type.full_name;
            entry["writable"] = Inspect::FieldCanWrite(field);
            annotate_label(entry, Labels::Kind::Field, field.declaring_type.full_name, field.name);
            fields.push_back(std::move(entry));
        }
        if (include_properties)
            for (const Inspect::PropertyInfo& property : Inspect::properties_from_class(klass, true)) {
                if (!property.is_static ||
                    !accepts(Labels::Kind::Property, property.name, property.type_name, property.declaring_type.full_name))
                    continue;
                ++matching;
                if (fields.size() + properties.size() >= static_cast<std::size_t>(*limit)) {
                    truncated = true;
                    continue;
                }
                clear_error();
                Inspect::ValueInfo value = !property.can_read || property.type_is_opaque
                    ? Inspect::unavailable_value(property.type_name, "The property cannot be read safely.")
                    : Inspect::ReadProperty({}, property);
                Json entry = value_json(value, type.full_name + "." + property.name);
                entry["name"] = property.name;
                entry["declaring_type"] = property.declaring_type.full_name;
                entry["writable"] = property.can_write;
                annotate_label(entry, Labels::Kind::Property, property.declaring_type.full_name, property.name);
                properties.push_back(std::move(entry));
            }
        clear_error();
        return succeed({{"type", type.full_name}, {"fields", std::move(fields)}, {"properties", std::move(properties)},
                        {"properties_executed", include_properties}, {"matching_members", matching},
                        {"truncated", truncated}, {"member_limit", *limit}});
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
        const std::string namespace_query = lowercase(request.arguments.value("namespace", std::string{}));
        const bool has_namespace_filter = request.arguments.contains("namespace");
        const auto type_offset = optional_offset(request.arguments, "offset");
        if (!type_offset)
            return fail("invalid_arguments", "offset must be a non-negative integer.");
        Json matches = Json::array();
        std::size_t type_seen = 0;
        bool type_has_more = false;
        for (const BrowserClassInfo& type : model.class_browser_catalog_->classes) {
            if (type_has_more)
                break;
            const std::string searchable = lowercase(type.full_name + " " + type.namespc + " " + type.class_name +
                                                     " " + type.image + " " +
                                                     Labels::search_text(Labels::Kind::Type, type.full_name));
            if ((!query.empty() && searchable.find(query) == std::string::npos) ||
                (!image_query.empty() && lowercase(type.image).find(image_query) == std::string::npos))
                continue;
            if (has_namespace_filter) {
                const std::string type_namespace = lowercase(type.namespc);
                if (type_namespace != namespace_query &&
                    !(type_namespace.size() > namespace_query.size() &&
                      type_namespace.compare(0, namespace_query.size(), namespace_query) == 0 &&
                      type_namespace[namespace_query.size()] == '.'))
                    continue;
            }
            if (type_seen++ < *type_offset)
                continue;
            if (matches.size() >= static_cast<std::size_t>(*limit)) {
                type_has_more = true;
                break;
            }
            Reference reference{ReferenceKind::ManagedType, 0, 0, 0, scene_generation_, hierarchy_revision_};
            reference.image = type.image;
            reference.namespc = type.namespc;
            reference.class_name = type.class_name;
            reference.metadata_address = type.metadata_address;
            const std::string token = issue_reference(model, std::move(reference));
            if (token.empty())
                return fail("reference_unavailable", "A secure type reference could not be generated.");
            Json match = {{"type_reference", token}, {"image", type.image}, {"namespace", type.namespc},
                          {"name", type.class_name}, {"full_name", type.full_name},
                          {"parent", type.parent_name}, {"is_component", type.is_component},
                          {"is_unity_object", type.is_unity_object}, {"is_interface", type.is_interface},
                          {"is_value_type", type.is_value_type}, {"is_enum", type.is_enum},
                          {"is_static", type.is_static}, {"is_abstract", type.is_abstract}};
            annotate_label(match, Labels::Kind::Type, type.full_name);
            matches.push_back(std::move(match));
        }
        return succeed({{"matches", std::move(matches)}, {"indexed_types", model.class_browser_catalog_->classes.size()},
                        {"limit", *limit}, {"offset", *type_offset}, {"has_more", type_has_more}});
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
        std::size_t result_bytes = 0;
        bool size_truncated = false;
        const auto member_offset = optional_offset(request.arguments, "offset");
        if (!member_offset)
            return fail("invalid_arguments", "offset must be a non-negative integer.");
        std::size_t member_seen = 0;
        const auto add_match = [&](Json match) {
            if (member_seen++ < *member_offset)
                return;
            result_bytes += match.dump().size();
            if (result_bytes > kResultBudgetBytes) {
                size_truncated = true;
                return;
            }
            matches.push_back(std::move(match));
        };
        for (const BrowserClassInfo& type : model.class_browser_catalog_->classes) {
            if (size_truncated || matches.size() >= static_cast<std::size_t>(*limit) ||
                scanned_types >= static_cast<std::size_t>(*type_limit))
                break;
            if (!type_query.empty() && lowercase(type.full_name + " " + type.image).find(type_query) == std::string::npos)
                continue;
            ++scanned_types;
            Command load{.kind = CommandKind::LoadClassBrowserMembers};
            load.metadata_address = type.metadata_address;
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
                    reference.metadata_address = type.metadata_address;
                    type_token = issue_reference(model, std::move(reference));
                }
                return type_token;
            };
            if (kind == "any" || kind == "field") {
                for (const Inspect::FieldInfo& field : model.class_browser_reflection_.fields) {
                    if (size_truncated || matches.size() >= static_cast<std::size_t>(*limit)) break;
                    const std::string label_text =
                        Labels::search_text(Labels::Kind::Field, field.declaring_type.full_name, field.name);
                    if (lowercase(field.name + " " + field.type_name + " " + label_text).find(query) == std::string::npos)
                        continue;
                    Json match = {{"kind", "field"}, {"name", field.name}, {"type", field.type_name},
                                  {"declaring_type", field.declaring_type.full_name},
                                  {"type_reference", ensure_type_token()},
                                  {"static", field.is_static}, {"writable", Inspect::FieldCanWrite(field)}};
                    annotate_label(match, Labels::Kind::Field, field.declaring_type.full_name, field.name);
                    add_match(std::move(match));
                }
            }
            if (kind == "any" || kind == "property") {
                for (const Inspect::PropertyInfo& property : model.class_browser_reflection_.properties) {
                    if (size_truncated || matches.size() >= static_cast<std::size_t>(*limit)) break;
                    const std::string label_text =
                        Labels::search_text(Labels::Kind::Property, property.declaring_type.full_name, property.name);
                    if (lowercase(property.name + " " + property.type_name + " " + label_text).find(query) ==
                        std::string::npos)
                        continue;
                    Json match = {{"kind", "property"}, {"name", property.name}, {"type", property.type_name},
                                  {"declaring_type", property.declaring_type.full_name},
                                  {"type_reference", ensure_type_token()}, {"static", property.is_static},
                                  {"readable", property.can_read}, {"writable", property.can_write}};
                    annotate_label(match, Labels::Kind::Property, property.declaring_type.full_name, property.name);
                    add_match(std::move(match));
                }
            }
            if (kind == "any" || kind == "method") {
                for (std::size_t index = 0; index < model.class_browser_reflection_.methods.size(); ++index) {
                    if (size_truncated || matches.size() >= static_cast<std::size_t>(*limit)) break;
                    const Inspect::MethodInfo& method = model.class_browser_reflection_.methods[index];
                    const std::string label_signature = Labels::method_signature(inspect_parameter_types(method));
                    std::string signature = method.name + " " + method.return_type;
                    for (const Inspect::MethodParamInfo& parameter : method.parameters)
                        signature += " " + parameter.type_name;
                    signature += " " + Labels::search_text(Labels::Kind::Method, method.declaring_type.full_name,
                                                           method.name, label_signature);
                    if (lowercase(std::move(signature)).find(query) == std::string::npos) continue;
                    Reference method_reference{ReferenceKind::ManagedMethod, 0, 0, 0,
                                               scene_generation_, hierarchy_revision_};
                    method_reference.image = type.image;
                    method_reference.namespc = type.namespc;
                    method_reference.class_name = type.class_name;
                    method_reference.member_index = static_cast<int>(index);
                    method_reference.metadata_address = type.metadata_address;
                    const std::string method_token = issue_reference(model, std::move(method_reference));
                    Json match = {{"kind", "method"}, {"name", method.name}, {"return_type", method.return_type},
                                  {"declaring_type", method.declaring_type.full_name},
                                  {"parameter_types", inspect_parameter_types(method)},
                                  {"type_reference", ensure_type_token()}, {"method_reference", method_token},
                                  {"static", method.is_static}, {"parameter_count", method.parameters.size()},
                                  {"signature_complete", method.signature_complete}};
                    annotate_label(match, Labels::Kind::Method, method.declaring_type.full_name, method.name,
                                   label_signature);
                    add_match(std::move(match));
                }
            }
        }
        return succeed({{"matches", std::move(matches)}, {"scanned_types", scanned_types},
                        {"indexed_types", model.class_browser_catalog_->classes.size()},
                        {"offset", *member_offset},
                        {"truncated", size_truncated || scanned_types >= static_cast<std::size_t>(*type_limit)}});
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
        command.metadata_address = resolved_type.metadata_address;
        command.image = resolved_type.image;
        command.namespc = resolved_type.namespc;
        command.class_name = resolved_type.class_name;
        model.load_class_browser_members(command);
        const auto members = model.working_.class_browser_members;
        if (!members)
            return fail("metadata_unavailable", "Members for the referenced type could not be loaded.");
        std::string resolved_type_name =
            resolved_type.namespc.empty() ? resolved_type.class_name : resolved_type.namespc + "." + resolved_type.class_name;
        if (model.class_browser_catalog_) {
            for (const BrowserClassInfo& candidate : model.class_browser_catalog_->classes) {
                if (resolved_type.metadata_address != 0 ? candidate.metadata_address == resolved_type.metadata_address
                                                        : candidate.image == resolved_type.image &&
                                                              candidate.namespc == resolved_type.namespc &&
                                                              candidate.class_name == resolved_type.class_name) {
                    resolved_type_name = candidate.full_name;
                    break;
                }
            }
        }
        const std::string query = lowercase(request.arguments.value("member_query", std::string{}));
        const auto member_offset = optional_offset(request.arguments, "member_offset");
        if (!member_offset)
            return fail("invalid_arguments", "member_offset must be a non-negative integer.");
        std::size_t skipped = 0;
        std::size_t emitted = 0;
        bool truncated = false;
        Json fields = Json::array();
        Json properties = Json::array();
        Json methods = Json::array();
        const auto accepts = [&](std::string_view text) {
            return query.empty() || lowercase(std::string(text)).find(query) != std::string::npos;
        };
        std::size_t result_bytes = 0;
        const auto fits = [&](const Json& item) {
            result_bytes += item.dump().size();
            return result_bytes <= kResultBudgetBytes;
        };
        const auto owner_of = [&](const std::string& declaring_type) -> const std::string& {
            return declaring_type.empty() ? resolved_type_name : declaring_type;
        };
        for (const ComponentInfo::Field& field : members->fields) {
            if (!accepts(field.name + " " + field.type_name + " " + field.declaring_type + " " +
                         Labels::search_text(Labels::Kind::Field, owner_of(field.declaring_type), field.name)))
                continue;
            if (skipped++ < *member_offset) continue; if (emitted++ >= static_cast<std::size_t>(*limit)) { truncated = true; break; }
            Json item = {{"name", field.name}, {"type", field.type_name},
                         {"declaring_type", field.declaring_type}, {"static", field.is_static},
                         {"read_only", field.is_read_only}, {"runtime_safe", field.runtime_safe},
                         {"capability_reason", field.capability_reason}};
            annotate_label(item, Labels::Kind::Field, owner_of(field.declaring_type), field.name);
            if (!fits(item)) { truncated = true; break; }
            fields.push_back(std::move(item));
        }
        if (!truncated) {
            for (const ComponentInfo::Property& property : members->properties) {
                if (!accepts(property.name + " " + property.type_name + " " + property.declaring_type + " " +
                             Labels::search_text(Labels::Kind::Property, owner_of(property.declaring_type),
                                                 property.name)))
                    continue;
                if (skipped++ < *member_offset) continue; if (emitted++ >= static_cast<std::size_t>(*limit)) { truncated = true; break; }
                Json item = {{"name", property.name}, {"type", property.type_name},
                             {"declaring_type", property.declaring_type}, {"can_read", property.can_read},
                             {"can_write", property.can_write}, {"runtime_safe", property.runtime_safe},
                             {"capability_reason", property.capability_reason}};
                annotate_label(item, Labels::Kind::Property, owner_of(property.declaring_type), property.name);
                if (!fits(item)) { truncated = true; break; }
                properties.push_back(std::move(item));
            }
        }
        if (!truncated) {
            for (std::size_t index = 0; index < members->methods.size(); ++index) {
                const ComponentInfo::Method& method = members->methods[index];
                const std::string signature = method_signature(method);
                const std::string label_signature = Labels::method_signature(method.parameter_types);
                if (!accepts(signature + " " +
                             Labels::search_text(Labels::Kind::Method, owner_of(method.declaring_type), method.name,
                                                 label_signature)))
                    continue;
                if (skipped++ < *member_offset) continue; if (emitted++ >= static_cast<std::size_t>(*limit)) { truncated = true; break; }
                Reference method_reference{ReferenceKind::ManagedMethod, 0, 0, 0,
                                           scene_generation_, hierarchy_revision_};
                method_reference.image = resolved_type.image;
                method_reference.namespc = resolved_type.namespc;
                method_reference.class_name = resolved_type.class_name;
                method_reference.member_index = static_cast<int>(index);
                method_reference.metadata_address = resolved_type.metadata_address;
                const std::string method_token = issue_reference(model, std::move(method_reference));
                if (method_token.empty())
                    return fail("reference_unavailable", "A secure method reference could not be generated.");
                Json item = {{"method_reference", method_token}, {"name", method.name},
                             {"signature", signature}, {"declaring_type", method.declaring_type},
                             {"return_type", method.return_type}, {"parameter_types", method.parameter_types},
                             {"parameter_names", method.parameter_names}, {"static", method.is_static},
                             {"signature_complete", method.signature_complete},
                             {"trace_candidate", method.signature_complete},
                             {"invocation_supported_in_ui", method.runtime_callable},
                             {"capability_reason", method.capability_reason}};
                annotate_label(item, Labels::Kind::Method, owner_of(method.declaring_type), method.name,
                               label_signature);
                if (!fits(item)) { truncated = true; break; }
                methods.push_back(std::move(item));
            }
        }
        Json type_json = {{"image", resolved_type.image}, {"namespace", resolved_type.namespc},
                          {"name", resolved_type.class_name}, {"full_name", resolved_type_name}};
        annotate_label(type_json, Labels::Kind::Type, resolved_type_name);
        return succeed({{"type", std::move(type_json)},
                        {"fields", std::move(fields)}, {"properties", std::move(properties)},
                        {"methods", std::move(methods)}, {"truncated", truncated}, {"member_limit", *limit},
                        {"member_offset", *member_offset}});
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
        load.metadata_address = method_reference->metadata_address;
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
        load.metadata_address = reference->metadata_address;
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
        trace.metadata_address = reference->metadata_address;
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
        const bool by_object = request.arguments.contains("reference");
        const bool by_type = request.arguments.contains("type_reference");
        if (!request.arguments.contains("member") || !request.arguments["member"].is_string() ||
            !request.arguments.contains("value") || by_object == by_type)
            return fail("invalid_arguments",
                        "member, value and exactly one of reference (instance members) or type_reference (static members) are required.");
        if ((by_object && !request.arguments["reference"].is_string()) ||
            (by_type && !request.arguments["type_reference"].is_string()))
            return fail("invalid_arguments", "The reference must be a string.");
        RootGuard owner;
        Object target;
        const URK::managed::Class* klass = nullptr;
        if (by_object) {
            std::string code;
            std::string message;
            target = resolve_target_reference(request.arguments["reference"].get_ref<const std::string&>(), owner,
                                              code, message);
            if (!target)
                return fail(std::move(code), std::move(message));
            const Inspect::TypeInfo type = Inspect::TypeOf(target);
            klass = static_cast<const URK::managed::Class*>(type.handle);
        } else {
            const Reference* type_ref = find_reference(request.arguments["type_reference"].get<std::string>(),
                                                       ReferenceKind::ManagedType, model);
            if (!type_ref)
                return fail("invalid_reference", "The type reference is invalid or expired.");
            Command locate{};
            locate.metadata_address = type_ref->metadata_address;
            locate.image = type_ref->image;
            locate.namespc = type_ref->namespc;
            locate.class_name = type_ref->class_name;
            klass = model.browser_class(locate);
        }
        if (!klass)
            return fail("metadata_unavailable", "The target type could not be resolved.");
        const std::string member_name = request.arguments["member"].get<std::string>();
        if (member_name.empty() || member_name.size() > 1024)
            return fail("invalid_arguments", "member must contain between 1 and 1024 characters.");
        const std::string kind = request.arguments.value("kind", std::string("auto"));
        if (kind != "field" && kind != "property" && kind != "auto")
            return fail("invalid_arguments", "kind must be field, property, or auto.");
        const std::string type_full_name = Inspect::DescribeClass(klass).full_name;
        const auto owner_of = [&](const auto& member) {
            return member.declaring_type.full_name.empty() ? type_full_name : member.declaring_type.full_name;
        };
        const Object write_target = by_object ? target : Object{};
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
        const auto target_json = [&](Json result) {
            if (by_object)
                result["reference"] = request.arguments["reference"];
            else
                result["type_reference"] = request.arguments["type_reference"];
            return result;
        };
        if (kind != "property") {
            const auto fields = Inspect::fields_from_class(klass, true);
            if (const Inspect::FieldInfo* field = find_named_member(fields, Labels::Kind::Field, member_name, owner_of)) {
                if (by_type && !field->is_static)
                    return fail("member_not_static", "That field belongs to instances; use reference instead of type_reference.");
                if (!Inspect::FieldCanWrite(*field))
                    return fail("member_not_writable", "The requested field is readonly or opaque.");
                ManagedInvoke::PreparedValue prepared = ManagedInvoke::prepare_value(
                    field->name, field->type_name, field->type, field->is_value_type, field->is_enum,
                    request.arguments["value"], resolver);
                if (!prepared.error.empty())
                    return fail("invalid_value", std::move(prepared.error));
                clear_error();
                Inspect::ValueInfo before = Inspect::ReadField(write_target, *field);
                const std::string previous = before.display;
                before.object = nullptr;
                clear_error();
                if (!Inspect::SetField(write_target, *field, prepared.value)) {
                    const char* detail = last_error();
                    return fail("write_failed", detail && detail[0] ? detail : "Managed field write failed.");
                }
                Inspect::ValueInfo observed = Inspect::ReadField(write_target, *field);
                Json result = target_json({{"member", field->name}, {"kind", "field"}, {"previous", previous},
                                           {"value", value_json(observed, field->name)}});
                annotate_label(result, Labels::Kind::Field, owner_of(*field), field->name);
                return succeed(std::move(result));
            }
            if (kind == "field")
                return fail("member_not_found", "No field matches that name, alias or label.");
        }
        if (!has_capability(request.context.capabilities, Capability::PropertyAccess))
            return fail("property_permission_required", "Enable managed property access in Explorer Config.");
        const auto properties = Inspect::properties_from_class(klass, true);
        const Inspect::PropertyInfo* property =
            find_named_member(properties, Labels::Kind::Property, member_name, owner_of);
        if (!property)
            return fail("member_not_found", "No field or property matches that name, alias or label.");
        if (by_type && !property->is_static)
            return fail("member_not_static", "That property belongs to instances; use reference instead of type_reference.");
        if (!property->can_write || property->set_method_is_abstract || property->type_is_opaque)
            return fail("member_not_writable", "The requested property has no usable setter.");
        ManagedInvoke::PreparedValue prepared = ManagedInvoke::prepare_value(
            property->name, property->type_name, property->type, property->is_value_type,
            property->is_enum, request.arguments["value"], resolver);
        if (!prepared.error.empty())
            return fail("invalid_value", std::move(prepared.error));
        std::string previous;
        if (property->can_read) {
            clear_error();
            Inspect::ValueInfo before = Inspect::ReadProperty(write_target, *property);
            previous = before.display;
            before.object = nullptr;
        }
        clear_error();
        if (!Inspect::SetProperty(write_target, *property, prepared.value)) {
            const char* detail = last_error();
            return fail("write_failed", detail && detail[0] ? detail : "Managed property write failed.");
        }
        Json result = target_json({{"member", property->name}, {"kind", "property"}, {"written", true}});
        if (property->can_read) {
            Inspect::ValueInfo observed = Inspect::ReadProperty(write_target, *property);
            result["previous"] = previous;
            result["value"] = value_json(observed, property->name);
        }
        annotate_label(result, Labels::Kind::Property, owner_of(*property), property->name);
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

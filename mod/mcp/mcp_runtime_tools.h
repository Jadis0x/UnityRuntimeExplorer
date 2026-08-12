// Copyright (c) 2026 Jadis0x. All rights reserved.
#pragma once

#include "mcp/core/bridge_protocol.h"

#include <cstdint>
#include <deque>
#include <string>
#include <unordered_map>

namespace Explorer {
class RuntimeModel;

namespace Mcp {

class RuntimeTools {
  public:
    Response execute(RuntimeModel& model, const Request& request);
    void revoke_instrumentation(RuntimeModel& model, std::string_view reason);
    void reset();

  private:
    enum class ReferenceKind {
        GameObject,
        Component,
        ManagedObject,
        GraphNode,
        ManagedType,
        ManagedMethod,
        MethodTrace,
        InstanceScan,
    };
    enum class ReferenceLookup { Found, Unknown, WrongKind, Expired };
    struct Reference {
        ReferenceKind kind = ReferenceKind::GameObject;
        int instance_id = 0;
        int owner_instance_id = 0;
        std::uint64_t graph_token = 0;
        std::uint64_t scene_generation = 0;
        std::uint64_t hierarchy_revision = 0;
        std::string image;
        std::string namespc;
        std::string class_name;
        int member_index = -1;
        std::uint64_t trace_id = 0;
        std::string expected_object_name;
        std::string expected_component_type;
    };

    std::string issue_reference(RuntimeModel& model, Reference reference);
    void release_reference_storage(RuntimeModel& model, const Reference& reference);
    const Reference* find_reference(std::string_view token, ReferenceKind kind,
                                    const RuntimeModel& model,
                                    ReferenceLookup* lookup = nullptr) const;
    void synchronize_generation(const RuntimeModel& model);

    std::unordered_map<std::string, Reference> references_;
    std::deque<std::string> reference_order_;
    std::uint64_t scene_generation_ = 0;
    std::uint64_t hierarchy_revision_ = 0;
    std::uint64_t next_instance_scan_id_ = 1;
    std::uint64_t active_instance_scan_id_ = 0;
};

} // namespace Mcp
} // namespace Explorer

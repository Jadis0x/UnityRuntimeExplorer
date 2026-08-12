// Copyright (c) 2026 Jadis0x. All rights reserved.
#pragma once

#include "mcp/core/bridge_protocol.h"

#include <cstdint>
#include <string>
#include <unordered_map>

namespace Explorer {
class RuntimeModel;

namespace Mcp {

class RuntimeTools {
  public:
    Response execute(RuntimeModel& model, const Request& request);
    void reset();

  private:
    enum class ReferenceKind { GameObject, Component, GraphNode };
    struct Reference {
        ReferenceKind kind = ReferenceKind::GameObject;
        int instance_id = 0;
        int owner_instance_id = 0;
        std::uint64_t graph_token = 0;
        std::uint64_t scene_generation = 0;
        std::uint64_t hierarchy_revision = 0;
    };

    std::string issue_reference(Reference reference);
    const Reference* find_reference(std::string_view token, ReferenceKind kind,
                                    const RuntimeModel& model) const;
    void synchronize_generation(const RuntimeModel& model);

    std::unordered_map<std::string, Reference> references_;
    std::uint64_t scene_generation_ = 0;
    std::uint64_t hierarchy_revision_ = 0;
};

} // namespace Mcp
} // namespace Explorer

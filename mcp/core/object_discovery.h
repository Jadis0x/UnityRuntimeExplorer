// Copyright (c) 2026 Jadis0x. All rights reserved.
#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Explorer::Mcp::Discovery {

struct ObjectCandidate {
    int instance_id = 0;
    std::string_view name;
    std::string path;
    std::string_view scene;
    std::string_view tag;
    bool active = false;
    std::span<const std::string> component_types;
    std::span<const std::string> dynamic_behaviour_types;
    bool signature_complete = true;
};

struct Query {
    std::string text;
    std::string scene;
    std::string role;
    bool active_only = false;
    std::vector<std::string> required_components;
    std::vector<std::string> required_dynamic_behaviours;
    std::size_t limit = 25;
};

struct RankedObject {
    std::size_t candidate_index = 0;
    int score = 0;
    std::vector<std::string> reasons;
};

struct SearchResult {
    std::vector<RankedObject> matches;
    std::size_t scanned = 0;
    std::size_t eligible = 0;
    std::size_t incomplete_signatures = 0;
    bool ambiguous = false;
};

SearchResult rank(const std::vector<ObjectCandidate>& candidates, const Query& query);
const char* confidence_label(int score);

} // namespace Explorer::Mcp::Discovery

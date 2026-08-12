// Copyright (c) 2026 Jadis0x. All rights reserved.
#include "object_discovery.h"

#include <algorithm>
#include <cctype>
#include <string_view>

namespace Explorer::Mcp::Discovery {
namespace {

std::string normalize(std::string_view value) {
    std::string result;
    result.reserve(value.size());
    for (const unsigned char character : value) {
        if (std::isalnum(character))
            result.push_back(static_cast<char>(std::tolower(character)));
        else if (!result.empty() && result.back() != ' ')
            result.push_back(' ');
    }
    while (!result.empty() && result.back() == ' ')
        result.pop_back();
    return result;
}

bool contains(std::string_view value, std::string_view query) {
    return !query.empty() && value.find(query) != std::string_view::npos;
}

bool type_matches(std::string_view candidate, std::string_view requested) {
    const std::string normalized_candidate = normalize(candidate);
    const std::string normalized_requested = normalize(requested);
    if (normalized_candidate == normalized_requested)
        return true;
    if (normalized_requested.empty() || normalized_candidate.size() <= normalized_requested.size())
        return false;
    const std::size_t offset = normalized_candidate.size() - normalized_requested.size();
    return normalized_candidate[offset - 1] == ' ' &&
        normalized_candidate.compare(offset, normalized_requested.size(), normalized_requested) == 0;
}

bool contains_type(std::span<const std::string> values, std::string_view requested) {
    return std::any_of(values.begin(), values.end(), [&](const std::string& value) {
        return type_matches(value, requested);
    });
}

bool contains_fragment(std::span<const std::string> values, std::string_view query) {
    return std::any_of(values.begin(), values.end(), [&](const std::string& value) {
        return contains(normalize(value), query);
    });
}

void add_score(RankedObject& result, int score, std::string reason) {
    result.score += score;
    result.reasons.push_back(std::move(reason));
}

bool role_supported(std::string_view role) {
    return role.empty() || role == "player";
}

} // namespace

const char* confidence_label(int score) {
    if (score >= 75)
        return "high";
    if (score >= 45)
        return "medium";
    return "low";
}

SearchResult rank(const std::vector<ObjectCandidate>& candidates, const Query& query) {
    SearchResult result{};
    result.scanned = candidates.size();

    const std::string text = normalize(query.text);
    const std::string requested_scene = normalize(query.scene);
    const std::string role = normalize(query.role);
    if (!role_supported(role))
        return result;

    for (std::size_t candidate_index = 0; candidate_index < candidates.size(); ++candidate_index) {
        const ObjectCandidate& candidate = candidates[candidate_index];
        if (!candidate.signature_complete)
            ++result.incomplete_signatures;
        if (query.active_only && !candidate.active)
            continue;
        if (!requested_scene.empty() && normalize(candidate.scene) != requested_scene)
            continue;
        if (!std::all_of(query.required_components.begin(), query.required_components.end(),
                         [&](const std::string& required) {
                             return contains_type(candidate.component_types, required);
                         }))
            continue;
        if (!std::all_of(query.required_dynamic_behaviours.begin(), query.required_dynamic_behaviours.end(),
                         [&](const std::string& required) {
                             return contains_type(candidate.dynamic_behaviour_types, required);
                         }))
            continue;

        RankedObject ranked{};
        ranked.candidate_index = candidate_index;
        const std::string name = normalize(candidate.name);
        const std::string path = normalize(candidate.path);
        const std::string tag = normalize(candidate.tag);

        bool text_matched = text.empty();
        if (!text.empty()) {
            if (name == text) {
                add_score(ranked, 70, "exact_name");
                text_matched = true;
            } else if (contains(name, text)) {
                add_score(ranked, 55, "name");
                text_matched = true;
            }
            if (contains(path, text)) {
                add_score(ranked, 35, "path");
                text_matched = true;
            }
            if (contains(tag, text)) {
                add_score(ranked, 15, "tag");
                text_matched = true;
            }
            if (contains_fragment(candidate.component_types, text)) {
                add_score(ranked, 40, "component_type");
                text_matched = true;
            }
            if (contains_fragment(candidate.dynamic_behaviour_types, text)) {
                add_score(ranked, 45, "dynamic_behaviour_type");
                text_matched = true;
            }
        }
        if (!text_matched)
            continue;

        for (const std::string& required : query.required_components)
            add_score(ranked, 12, "required_component:" + required);
        for (const std::string& required : query.required_dynamic_behaviours)
            add_score(ranked, 15, "required_dynamic_behaviour:" + required);

        if (role == "player") {
            if (contains(name, "player") || contains(path, "player"))
                add_score(ranked, 25, "player_name_or_path");
            if (contains_type(candidate.component_types, "PlayerAnimationController"))
                add_score(ranked, 35, "player_animation_controller");
            if (contains_type(candidate.component_types, "CharacterController"))
                add_score(ranked, 25, "character_controller");
            if (contains_type(candidate.component_types, "Animator"))
                add_score(ranked, 8, "animator");
            if (contains_fragment(candidate.dynamic_behaviour_types, "avatartag"))
                add_score(ranked, 25, "avatar_tag");
            if (ranked.score == 0)
                continue;
        }

        if (candidate.active)
            add_score(ranked, 3, "active");
        ranked.score = std::min(ranked.score, 100);
        ++result.eligible;
        result.matches.push_back(std::move(ranked));
    }

    std::stable_sort(result.matches.begin(), result.matches.end(), [&](const RankedObject& left,
                                                                      const RankedObject& right) {
        if (left.score != right.score)
            return left.score > right.score;
        const ObjectCandidate& left_candidate = candidates[left.candidate_index];
        const ObjectCandidate& right_candidate = candidates[right.candidate_index];
        if (left_candidate.active != right_candidate.active)
            return left_candidate.active;
        return left_candidate.instance_id < right_candidate.instance_id;
    });
    result.ambiguous = result.matches.size() > 1 &&
        result.matches.front().score - result.matches[1].score <= 5;
    if (result.matches.size() > query.limit)
        result.matches.resize(query.limit);
    return result;
}

} // namespace Explorer::Mcp::Discovery

// Copyright (c) 2026 Jadis0x. All rights reserved.
#include "mcp/core/bridge_protocol.h"
#include "mcp/core/object_discovery.h"
#include "mcp/core/tool_catalog.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}
} // namespace

int main() {
    using namespace Explorer::Mcp;
    require(tool_catalog().size() == 24, "twenty-four base tools are exposed");
    require(tool_catalog(true).size() == 27, "three tracing tools are exposed");
    require(tool_catalog(false, true).size() == 25, "invocation can be classified independently");
    require(tool_catalog(true, true).size() == 28, "tracing and invocation can be combined");
    require(tool_catalog(true, true, true).size() == 32, "the complete Explorer-controlled catalog is exposed");
    require(is_read_only_tool("runtime_status"), "runtime_status is allowed");
    require(is_read_only_tool("search_types"), "managed type search is read-only");
    require(is_read_only_tool("get_method_trace"), "trace reads are read-only");
    require(is_base_tool("inspect_managed_object"), "managed object traversal is in the base catalog");
    require(!is_read_only_tool("inspect_managed_object"), "optional property execution is conservatively annotated");
    require(is_read_only_tool("read_array"), "array paging is read-only");
    require(is_read_only_tool("decode_byte_array"), "copied byte decoding is read-only");
    require(is_read_only_tool("start_instance_scan"), "bounded instance scanning is read-only");
    require(is_read_only_tool("cancel_instance_scan"), "scan cancellation is read-only to the game");
    require(is_instrumentation_tool("start_method_trace"), "trace start is instrumentation");
    require(!is_available_tool("start_method_trace", false), "trace start is hidden by default");
    require(is_available_tool("start_method_trace", true), "trace start is available after helper opt-in");
    require(is_invocation_tool("invoke_method"), "method invocation has a separate capability class");
    require(!is_available_tool("invoke_method", false, false), "invocation is hidden by default");
    require(!is_available_tool("invoke_method", true, false), "tracing does not imply invocation");
    require(is_available_tool("invoke_method", false, true), "invocation requires its own helper opt-in");
    require(!is_read_only_tool("write_member"), "write_member is not exposed");
    require(!is_read_only_tool("invoke_method"), "invoke_method is not in the read-only catalog");
    require(is_write_tool("write_member"), "managed writes have a distinct capability class");
    require(is_write_tool("mutate_game_object"), "GameObject mutation has a distinct capability class");
    require(is_destructive_tool("load_scene"), "scene loading is marked destructive");
    require(is_available_tool("write_member", false, false, true), "mutation is available only when requested");
    for (const auto& tool : tool_catalog()) {
        require(tool["inputSchema"].dump().find("pointer") == std::string::npos,
                "schemas do not expose pointer inputs");
        require(tool.contains("outputSchema"), "every tool publishes an output schema");
    }
    for (const auto& tool : tool_catalog(true)) {
        require(tool["inputSchema"].dump().find("pointer") == std::string::npos,
                "no tool schema exposes pointer inputs");
        if (is_instrumentation_tool(tool["name"].get<std::string>()))
            require(!tool["annotations"]["readOnlyHint"].get<bool>(),
                    "instrumentation tools are not mislabeled read-only");
    }
    for (const auto& tool : tool_catalog(true, true)) {
        require(tool["inputSchema"].dump().find("pointer") == std::string::npos,
                "optional capabilities do not expose pointer inputs");
        if (is_invocation_tool(tool["name"].get<std::string>()))
            require(!tool["annotations"]["readOnlyHint"].get<bool>(),
                    "invocation is not mislabeled read-only");
    }

    Request input{"42", "hierarchy_search", {{"query", "camera"}}};
    input.context.capabilities = capability_bit(Capability::Read) | capability_bit(Capability::Write);
    Request parsed;
    std::string error;
    require(parse_request(serialize(input), parsed, error), "bridge request round trip");
    require(parsed.id == input.id && parsed.tool == input.tool, "bridge request identity preserved");
    require(parsed.context.capabilities == 0, "serialized requests cannot inject capability context");

    Response output{"42", true, {{"connected", true}}};
    Response parsed_output;
    require(parse_response(serialize(output), parsed_output, error), "bridge response round trip");
    require(parsed_output.ok && parsed_output.result["connected"].get<bool>(), "bridge result preserved");

    Response sensitive{"7", true, {{"safe", "object at 0x1234ABCDEF and small 0x12"}}};
    const std::string redacted = serialize(sensitive);
    require(redacted.find("0x1234ABCDEF") == std::string::npos, "pointer-like values are redacted");
    require(redacted.find("<redacted-address>") != std::string::npos, "redaction marker is present");
    require(redacted.find("0x12") != std::string::npos, "short ordinary hex values are retained");

    require(!parse_request("{bad", parsed, error), "malformed JSON is rejected");
    require(!parse_request(R"({"id":"1","tool":"x","arguments":[]})", parsed, error),
            "non-object arguments are rejected");

    using namespace Discovery;
    const std::vector<std::string> player_components{
        "UnityEngine.Transform", "UnityEngine.CharacterController",
        "XDT.EngineSystem.AnimationSystem.PlayerAnimationController"};
    const std::vector<std::string> player_behaviours{
        "ScriptsRefactory.LevelAndEntity.ResHandle.LevelResHandleBase"};
    const std::vector<std::string> camera_components{
        "UnityEngine.Transform", "UnityEngine.Camera"};
    const std::vector<ObjectCandidate> candidates{
        {-10, "p_player_skeleton(Clone)", "p_player_skeleton(Clone)", "StarTown", "Untagged", true,
         player_components, player_behaviours},
        {-20, "Main Camera", "GameApp/Main Camera", "DontDestroyOnLoad", "MainCamera", true,
         camera_components, {}},
        {-30, "p_player_skeleton(Clone)", "p_player_skeleton(Clone)", "StarTown", "Untagged", true,
         player_components, player_behaviours}
    };
    Query player_query{};
    player_query.role = "player";
    player_query.active_only = true;
    const SearchResult players = rank(candidates, player_query);
    require(players.matches.size() == 2, "player role finds component-backed player candidates");
    require(players.ambiguous, "equally supported player candidates are reported as ambiguous");
    require(players.incomplete_signatures == 0, "complete fixtures do not report signature failures");
    require(players.matches.front().score >= 75, "strong player component evidence has high confidence");
    auto incomplete_candidates = candidates;
    incomplete_candidates.front().signature_complete = false;
    require(rank(incomplete_candidates, player_query).incomplete_signatures == 1,
            "incomplete component signatures remain observable");
    player_query.limit = 1;
    require(rank(candidates, player_query).ambiguous,
            "ambiguity is preserved when the response limit returns only the first candidate");

    Query component_query{};
    component_query.required_components = {"CharacterController", "PlayerAnimationController"};
    const SearchResult component_matches = rank(candidates, component_query);
    require(component_matches.matches.size() == 2, "required components match full type suffixes");
    component_query.required_components = {"Controller"};
    require(rank(candidates, component_query).matches.empty(),
            "component suffix matching respects type-name boundaries");

    Query text_query{};
    text_query.text = "camera";
    const SearchResult cameras = rank(candidates, text_query);
    require(cameras.matches.size() == 1 &&
            candidates[cameras.matches.front().candidate_index].instance_id == -20,
            "text discovery considers object path and name");
    return 0;
}

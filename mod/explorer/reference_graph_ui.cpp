// Copyright (c) 2026 Jadis0x. All rights reserved.
#include "reference_graph_ui.h"

#include "explorer_commands.h"
#include "explorer_model.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string_view>
#include <utility>
#include <vector>

namespace Explorer::ReferenceGraphUI {
namespace {
using Graph = Snapshot::ReferenceGraph;

struct ViewState {
    float zoom = 1.0f;
    ImVec2 pan{24.0f, 24.0f};
    std::array<char, 128> search{};
    std::uint64_t selected_token = 0;
    std::uint64_t graph_signature = 0;
    bool fit_requested = true;
    bool show_minimap = true;
};

ViewState& state() {
    static ViewState value;
    return value;
}

bool contains_case_insensitive(std::string_view text, std::string_view query) {
    if (query.empty())
        return true;
    return std::search(text.begin(), text.end(), query.begin(), query.end(),
                       [](unsigned char left, unsigned char right) {
                           return static_cast<unsigned char>(std::tolower(left)) ==
                                  static_cast<unsigned char>(std::tolower(right));
                       }) != text.end();
}

void enqueue(CommandKind kind) {
    RuntimeModel::instance().enqueue(Command{.kind = kind});
}

void inspect(std::uint64_t token) {
    Command command{.kind = CommandKind::InspectReference};
    command.reference_token = token;
    RuntimeModel::instance().enqueue(std::move(command));
}

ImU32 node_fill(Graph::Node::Kind kind, bool selected, bool hovered, bool dimmed) {
    ImVec4 color;
    switch (kind) {
    case Graph::Node::Kind::GameObject: color = ImVec4(0.16f, 0.31f, 0.43f, 1.0f); break;
    case Graph::Node::Kind::Component: color = ImVec4(0.17f, 0.29f, 0.31f, 1.0f); break;
    case Graph::Node::Kind::UnityObject: color = ImVec4(0.24f, 0.25f, 0.29f, 1.0f); break;
    case Graph::Node::Kind::Array: color = ImVec4(0.28f, 0.23f, 0.34f, 1.0f); break;
    case Graph::Node::Kind::ManagedObject: color = ImVec4(0.21f, 0.22f, 0.24f, 1.0f); break;
    }
    if (selected)
        color = ImVec4(0.20f, 0.42f, 0.58f, 1.0f);
    else if (hovered) {
        color.x += 0.07f;
        color.y += 0.07f;
        color.z += 0.07f;
    }
    if (dimmed)
        color.w = 0.30f;
    return ImGui::GetColorU32(color);
}

ImU32 edge_color(Graph::Edge::Kind kind, bool highlighted, bool dimmed) {
    ImVec4 color;
    switch (kind) {
    case Graph::Edge::Kind::Component: color = ImVec4(0.28f, 0.60f, 0.76f, 0.78f); break;
    case Graph::Edge::Kind::Owner: color = ImVec4(0.35f, 0.63f, 0.48f, 0.75f); break;
    case Graph::Edge::Kind::ArrayElement: color = ImVec4(0.62f, 0.43f, 0.72f, 0.72f); break;
    case Graph::Edge::Kind::BackReference: color = ImVec4(0.80f, 0.48f, 0.31f, 0.78f); break;
    case Graph::Edge::Kind::Field: color = ImVec4(0.48f, 0.55f, 0.61f, 0.68f); break;
    }
    if (highlighted) {
        color.x = std::min(1.0f, color.x + 0.22f);
        color.y = std::min(1.0f, color.y + 0.22f);
        color.z = std::min(1.0f, color.z + 0.22f);
        color.w = 1.0f;
    } else if (dimmed) {
        color.w = 0.12f;
    }
    return ImGui::GetColorU32(color);
}

void draw_arrow(ImDrawList* draw, ImVec2 from, ImVec2 tip, ImU32 color, float scale) {
    const float dx = tip.x - from.x;
    const float dy = tip.y - from.y;
    const float length = std::sqrt(dx * dx + dy * dy);
    if (length < 0.001f)
        return;
    const float ux = dx / length;
    const float uy = dy / length;
    const float size = 7.0f * scale;
    const ImVec2 base(tip.x - ux * size, tip.y - uy * size);
    const ImVec2 normal(-uy * size * 0.55f, ux * size * 0.55f);
    draw->AddTriangleFilled(tip, ImVec2(base.x + normal.x, base.y + normal.y),
                            ImVec2(base.x - normal.x, base.y - normal.y), color);
}

const char* kind_label(Graph::Node::Kind kind) {
    switch (kind) {
    case Graph::Node::Kind::GameObject: return "GameObject";
    case Graph::Node::Kind::Component: return "Component";
    case Graph::Node::Kind::UnityObject: return "Unity Object";
    case Graph::Node::Kind::Array: return "Managed Array";
    case Graph::Node::Kind::ManagedObject: return "Managed Object";
    }
    return "Object";
}
} // namespace

void render(const Snapshot& snapshot) {
    ViewState& view = state();
    if (ImGui::Button("Build / Refresh")) {
        Command command{.kind = CommandKind::BuildReferenceGraph};
        command.object_inspector_token = snapshot.object_inspector.valid ? snapshot.object_inspector.token : 0;
        RuntimeModel::instance().enqueue(std::move(command));
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear"))
        enqueue(CommandKind::ClearReferenceGraph);
    ImGui::SameLine();
    if (ImGui::Button("Frame All"))
        view.fit_requested = true;
    ImGui::SameLine();
    ImGui::Checkbox("Minimap", &view.show_minimap);
    ImGui::SameLine();
    ImGui::TextDisabled("Safe traversal: fields, arrays, GameObject/component ownership; no property getters.");

    const Graph& graph = snapshot.reference_graph;
    if (graph.nodes.empty()) {
        ImGui::TextDisabled("Select a GameObject or open a managed object, then build its graph.");
        return;
    }
    const std::uint64_t signature = graph.nodes.front().token ^
        (static_cast<std::uint64_t>(graph.nodes.size()) << 32u) ^ graph.edges.size();
    if (signature != view.graph_signature) {
        view.graph_signature = signature;
        view.selected_token = graph.nodes.front().token;
        view.fit_requested = true;
    }

    ImGui::SetNextItemWidth(std::min(300.0f, ImGui::GetContentRegionAvail().x * 0.34f));
    ImGui::InputTextWithHint("##graph-search", "Find object or type...", view.search.data(), view.search.size());
    ImGui::SameLine();
    ImGui::TextDisabled("%s%s", graph.status.c_str(), graph.truncated ? " | traversal budget reached" : "");

    const auto selected_it = std::find_if(graph.nodes.begin(), graph.nodes.end(), [&](const Graph::Node& node) {
        return node.token == view.selected_token;
    });
    const std::size_t selected_index = selected_it == graph.nodes.end()
        ? std::numeric_limits<std::size_t>::max()
        : static_cast<std::size_t>(selected_it - graph.nodes.begin());
    if (selected_it != graph.nodes.end()) {
        ImGui::Text("%s", selected_it->label.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("%s | %s | %s", kind_label(selected_it->kind), selected_it->type_name.c_str(),
                            selected_it->pointer_text.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Open Inspector"))
            inspect(selected_it->token);
        ImGui::SameLine();
        if (ImGui::SmallButton("Copy Ptr"))
            ImGui::SetClipboardText(selected_it->pointer_text.c_str());
    }

    const ImVec2 canvas_origin = ImGui::GetCursorScreenPos();
    const ImVec2 canvas_size(std::max(320.0f, ImGui::GetContentRegionAvail().x),
                             std::max(260.0f, ImGui::GetContentRegionAvail().y));
    ImGui::InvisibleButton("##advanced-reference-graph", canvas_size,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
    const bool canvas_hovered = ImGui::IsItemHovered();
    const ImVec2 canvas_max(canvas_origin.x + canvas_size.x, canvas_origin.y + canvas_size.y);
    constexpr ImVec2 base_node_size{238.0f, 70.0f};

    float graph_min_x = graph.nodes.front().x;
    float graph_min_y = graph.nodes.front().y;
    float graph_max_x = graph_min_x + base_node_size.x;
    float graph_max_y = graph_min_y + base_node_size.y;
    for (const Graph::Node& node : graph.nodes) {
        graph_min_x = std::min(graph_min_x, node.x);
        graph_min_y = std::min(graph_min_y, node.y);
        graph_max_x = std::max(graph_max_x, node.x + base_node_size.x);
        graph_max_y = std::max(graph_max_y, node.y + base_node_size.y);
    }
    if (view.fit_requested) {
        const float width = std::max(1.0f, graph_max_x - graph_min_x);
        const float height = std::max(1.0f, graph_max_y - graph_min_y);
        view.zoom = std::clamp(std::min((canvas_size.x - 56.0f) / width,
                                        (canvas_size.y - 56.0f) / height), 0.30f, 1.55f);
        view.pan.x = (canvas_size.x - width * view.zoom) * 0.5f - graph_min_x * view.zoom;
        view.pan.y = (canvas_size.y - height * view.zoom) * 0.5f - graph_min_y * view.zoom;
        view.fit_requested = false;
    }
    if (canvas_hovered && ImGui::GetIO().MouseWheel != 0.0f) {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const float old_zoom = view.zoom;
        const float new_zoom = std::clamp(old_zoom * std::pow(1.13f, ImGui::GetIO().MouseWheel), 0.25f, 2.2f);
        const float graph_x = (mouse.x - canvas_origin.x - view.pan.x) / old_zoom;
        const float graph_y = (mouse.y - canvas_origin.y - view.pan.y) / old_zoom;
        view.zoom = new_zoom;
        view.pan.x = mouse.x - canvas_origin.x - graph_x * new_zoom;
        view.pan.y = mouse.y - canvas_origin.y - graph_y * new_zoom;
    }
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
        view.pan.x += ImGui::GetIO().MouseDelta.x;
        view.pan.y += ImGui::GetIO().MouseDelta.y;
    }

    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(canvas_origin, canvas_max, IM_COL32(42, 42, 42, 255));
    draw->PushClipRect(canvas_origin, canvas_max, true);
    const float grid = 32.0f * view.zoom;
    if (grid >= 12.0f) {
        float x = std::fmod(view.pan.x, grid);
        if (x < 0.0f) x += grid;
        for (; x < canvas_size.x; x += grid)
            draw->AddLine(ImVec2(canvas_origin.x + x, canvas_origin.y),
                          ImVec2(canvas_origin.x + x, canvas_max.y), IM_COL32(56, 56, 56, 150));
        float y = std::fmod(view.pan.y, grid);
        if (y < 0.0f) y += grid;
        for (; y < canvas_size.y; y += grid)
            draw->AddLine(ImVec2(canvas_origin.x, canvas_origin.y + y),
                          ImVec2(canvas_max.x, canvas_origin.y + y), IM_COL32(56, 56, 56, 150));
    }

    std::vector<bool> neighbourhood(graph.nodes.size(), selected_index == std::numeric_limits<std::size_t>::max());
    if (selected_index < graph.nodes.size()) {
        neighbourhood[selected_index] = true;
        for (const Graph::Edge& edge : graph.edges) {
            if (edge.from == selected_index && edge.to < neighbourhood.size()) neighbourhood[edge.to] = true;
            if (edge.to == selected_index && edge.from < neighbourhood.size()) neighbourhood[edge.from] = true;
        }
    }
    const std::string_view query(view.search.data());
    std::vector<bool> search_match(graph.nodes.size(), query.empty());
    for (std::size_t index = 0; index < graph.nodes.size(); ++index)
        search_match[index] = query.empty() || contains_case_insensitive(graph.nodes[index].label, query) ||
                              contains_case_insensitive(graph.nodes[index].type_name, query);

    const auto node_min = [&](const Graph::Node& node) {
        return ImVec2(canvas_origin.x + view.pan.x + node.x * view.zoom,
                      canvas_origin.y + view.pan.y + node.y * view.zoom);
    };
    std::vector<int> outgoing_count(graph.nodes.size(), 0), incoming_count(graph.nodes.size(), 0);
    for (const Graph::Edge& edge : graph.edges) {
        if (edge.from < outgoing_count.size()) ++outgoing_count[edge.from];
        if (edge.to < incoming_count.size()) ++incoming_count[edge.to];
    }
    std::vector<int> outgoing_slot(graph.nodes.size(), 0), incoming_slot(graph.nodes.size(), 0);
    for (const Graph::Edge& edge : graph.edges) {
        if (edge.from >= graph.nodes.size() || edge.to >= graph.nodes.size())
            continue;
        const Graph::Node& source = graph.nodes[edge.from];
        const Graph::Node& target = graph.nodes[edge.to];
        const ImVec2 source_min = node_min(source);
        const ImVec2 target_min = node_min(target);
        const ImVec2 scaled_size(base_node_size.x * view.zoom, base_node_size.y * view.zoom);
        const int out_slot = outgoing_slot[edge.from]++;
        const int in_slot = incoming_slot[edge.to]++;
        const float out_t = static_cast<float>(out_slot + 1) / static_cast<float>(outgoing_count[edge.from] + 1);
        const float in_t = static_cast<float>(in_slot + 1) / static_cast<float>(incoming_count[edge.to] + 1);
        const bool forward = target_min.x >= source_min.x;
        const ImVec2 start(forward ? source_min.x + scaled_size.x : source_min.x,
                           source_min.y + scaled_size.y * out_t);
        const ImVec2 end(forward ? target_min.x : target_min.x + scaled_size.x,
                         target_min.y + scaled_size.y * in_t);
        const bool highlighted = edge.from == selected_index || edge.to == selected_index;
        const bool dimmed = selected_index < graph.nodes.size() && !highlighted;
        const ImU32 color = edge_color(edge.kind, highlighted, dimmed);
        const float direction = forward ? 1.0f : -1.0f;
        const float bend = std::max(38.0f * view.zoom, std::abs(end.x - start.x) * 0.42f);
        const ImVec2 control_a(start.x + bend * direction, start.y);
        const ImVec2 control_b(end.x - bend * direction, end.y);
        draw->AddBezierCubic(start, control_a, control_b, end, color,
                             highlighted ? 2.5f : 1.25f);
        draw_arrow(draw, control_b, end, color, std::clamp(view.zoom, 0.65f, 1.3f));
        if (!edge.label.empty() && (highlighted || view.zoom >= 1.05f)) {
            const ImVec2 label_pos((start.x + end.x) * 0.5f + 4.0f,
                                   (start.y + end.y) * 0.5f - 15.0f);
            draw->AddText(label_pos, dimmed ? IM_COL32(145, 145, 145, 75) : IM_COL32(190, 194, 198, 230),
                          edge.label.c_str());
        }
    }

    const ImVec2 mouse = ImGui::GetIO().MousePos;
    bool node_hovered = false;
    for (std::size_t index = 0; index < graph.nodes.size(); ++index) {
        const Graph::Node& node = graph.nodes[index];
        const ImVec2 min = node_min(node);
        const ImVec2 max(min.x + base_node_size.x * view.zoom, min.y + base_node_size.y * view.zoom);
        if (max.x < canvas_origin.x || min.x > canvas_max.x || max.y < canvas_origin.y || min.y > canvas_max.y)
            continue;
        const bool hovered = mouse.x >= min.x && mouse.x <= max.x && mouse.y >= min.y && mouse.y <= max.y;
        node_hovered = node_hovered || hovered;
        const bool selected = node.token == view.selected_token;
        const bool dimmed = !neighbourhood[index] || !search_match[index];
        draw->AddRectFilled(min, max, node_fill(node.kind, selected, hovered, dimmed), 3.0f);
        const ImU32 border = selected ? IM_COL32(76, 160, 220, 255)
            : search_match[index] && !query.empty() ? IM_COL32(225, 176, 70, 255)
            : IM_COL32(102, 105, 109, dimmed ? 75 : 230);
        draw->AddRect(min, max, border, 3.0f, 0, selected ? 2.0f : 1.0f);
        const ImVec4 clip(min.x + 8.0f, min.y + 4.0f, max.x - 7.0f, max.y - 4.0f);
        draw->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(min.x + 9.0f, min.y + 8.0f),
                      IM_COL32(235, 236, 238, dimmed ? 90 : 255), node.label.c_str(), nullptr, 0.0f, &clip);
        if (view.zoom >= 0.58f)
            draw->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(min.x + 9.0f, min.y + 31.0f * view.zoom),
                           IM_COL32(168, 171, 176, dimmed ? 65 : 245), node.type_name.c_str(), nullptr, 0.0f, &clip);
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            view.selected_token = node.token;
        if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            inspect(node.token);
        if (hovered)
            ImGui::SetTooltip("%s\n%s\n%s\nDouble-click: open Inspector", node.label.c_str(),
                              node.type_name.c_str(), node.pointer_text.c_str());
    }
    if (canvas_hovered && !node_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        view.selected_token = 0;

    if (view.show_minimap) {
        constexpr ImVec2 minimap_size{184.0f, 112.0f};
        const ImVec2 mini_max(canvas_max.x - 12.0f, canvas_max.y - 12.0f);
        const ImVec2 mini_min(mini_max.x - minimap_size.x, mini_max.y - minimap_size.y);
        draw->AddRectFilled(mini_min, mini_max, IM_COL32(25, 25, 25, 225), 2.0f);
        draw->AddRect(mini_min, mini_max, IM_COL32(102, 105, 109, 230), 2.0f);
        const float graph_width = std::max(1.0f, graph_max_x - graph_min_x);
        const float graph_height = std::max(1.0f, graph_max_y - graph_min_y);
        const float mini_scale = std::min((minimap_size.x - 12.0f) / graph_width,
                                          (minimap_size.y - 12.0f) / graph_height);
        const auto mini_point = [&](float x, float y) {
            return ImVec2(mini_min.x + 6.0f + (x - graph_min_x) * mini_scale,
                          mini_min.y + 6.0f + (y - graph_min_y) * mini_scale);
        };
        for (std::size_t index = 0; index < graph.nodes.size(); ++index) {
            const Graph::Node& node = graph.nodes[index];
            const ImVec2 point = mini_point(node.x + base_node_size.x * 0.5f,
                                            node.y + base_node_size.y * 0.5f);
            draw->AddCircleFilled(point, index == selected_index ? 3.0f : 1.8f,
                                  index == selected_index ? IM_COL32(82, 168, 225, 255) : IM_COL32(155, 158, 162, 220));
        }
        const float visible_min_x = (-view.pan.x) / view.zoom;
        const float visible_min_y = (-view.pan.y) / view.zoom;
        const float visible_max_x = (canvas_size.x - view.pan.x) / view.zoom;
        const float visible_max_y = (canvas_size.y - view.pan.y) / view.zoom;
        draw->AddRect(mini_point(visible_min_x, visible_min_y), mini_point(visible_max_x, visible_max_y),
                      IM_COL32(76, 160, 220, 245), 0.0f, 0, 1.5f);
    }
    draw->PopClipRect();
}
} // namespace Explorer::ReferenceGraphUI

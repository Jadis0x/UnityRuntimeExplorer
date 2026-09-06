// Copyright (c) 2026 Jadis0x. All rights reserved.
// Scene hierarchy tree, its filter and its per-node context menu.
#include "ui_members.h"

#include "config/mod_config.h"
#include "explorer_model.h"
#include "ui_shared.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace Explorer::UI {
namespace {

std::array<char, 128> &search_buffer() {
    static std::array<char, 128> buffer{};
    return buffer;
}

using NodeMatchSet = std::unordered_set<int>;

enum class HierarchyFilterMode {
    Everything,
    Name,
    Tag,
    InstanceId,
};

NodeMatchSet &hierarchy_filter_matches() {
    static NodeMatchSet matches;
    return matches;
}

bool hierarchy_node_matches(const HierarchyNode &node, std::string_view filter, HierarchyFilterMode mode) {
    if (filter.empty())
        return true;
    const std::string instance_id = std::to_string(node.instance_id);
    switch (mode) {
    case HierarchyFilterMode::Name:
        return contains_case_insensitive(node.name, filter);
    case HierarchyFilterMode::Tag:
        return contains_case_insensitive(node.tag, filter);
    case HierarchyFilterMode::InstanceId:
        return contains_case_insensitive(instance_id, filter);
    case HierarchyFilterMode::Everything:
        return contains_case_insensitive(node.name, filter) || contains_case_insensitive(node.tag, filter) ||
               contains_case_insensitive(instance_id, filter);
    }
    return false;
}

bool collect_matching_nodes(const HierarchyNode &node, std::string_view filter, HierarchyFilterMode mode,
                            bool include_inactive,
                            NodeMatchSet &matches) {
    bool matches_filter = include_inactive || node.active;
    matches_filter = matches_filter && hierarchy_node_matches(node, filter, mode);
    for (const HierarchyNode &child : node.children)
        matches_filter = collect_matching_nodes(child, filter, mode, include_inactive, matches) || matches_filter;
    if (matches_filter)
        matches.insert(node.instance_id);
    return matches_filter;
}

} // namespace
void enqueue_simple(CommandKind kind, int instance_id) {
    RuntimeModel::instance().enqueue(Command{.kind = kind, .instance_id = instance_id});
}
namespace {

void enqueue_hierarchy_command(CommandKind kind, const HierarchyNode &node, std::uint64_t revision) {
    Command command{
         .kind = kind,
         .instance_id = node.instance_id
    };

    if (kind == CommandKind::DeleteObject ||
        kind == CommandKind::DuplicateObject) {
        command.hierarchy_revision = revision;
    }

    RuntimeModel::instance().enqueue(std::move(command));

}

void enqueue_hierarchy_transform_paste(const HierarchyNode& node, std::uint64_t revision,
                                       const Snapshot::TransformClipboard& clipboard) {
    Command command{};
    command.kind = CommandKind::PasteLocalTransform;
    command.instance_id = node.instance_id;
    command.hierarchy_revision = revision;
    command.vector_value = clipboard.local_position;
    command.vector_value_secondary = clipboard.local_rotation;
    command.vector_value_tertiary = clipboard.local_scale;
    RuntimeModel::instance().enqueue(std::move(command));
}

void render_context_menu(const HierarchyNode &node, std::uint64_t revision,
                         const Snapshot::TransformClipboard& clipboard) {
    if (!ImGui::BeginPopupContextItem("##game-object-context"))
        return;
    ImGui::TextDisabled("%s", node.name.c_str());
    ImGui::Separator();
    if (ImGui::MenuItem("Copy Ptr"))
        ImGui::SetClipboardText(node.pointer_text.c_str());
    if (ImGui::MenuItem("Copy transform"))
        enqueue_hierarchy_command(CommandKind::CopyLocalTransform, node, revision);
    if (ImGui::MenuItem("Paste transform", nullptr, false, clipboard.valid))
        enqueue_hierarchy_transform_paste(node, revision, clipboard);
    if (clipboard.valid)
        ImGui::TextDisabled("Local transform from %s", clipboard.source_name.c_str());
    if (ImGui::MenuItem("Duplicate"))
        enqueue_hierarchy_command(CommandKind::DuplicateObject, node, revision);
    if (ImGui::MenuItem("Delete"))
        enqueue_hierarchy_command(CommandKind::DeleteObject, node, revision);
    ImGui::EndPopup();
}

void render_node(const HierarchyNode &node, int selected_instance_id, const NodeMatchSet *matches,
                  bool include_inactive, std::uint64_t revision,
                  const Snapshot::TransformClipboard& clipboard) {
    if (!include_inactive && !node.active)
        return;
    if (matches && !matches->contains(node.instance_id))
        return;

    ImGui::PushID(node.instance_id);
    ImGuiTreeNodeFlags flags =
        ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (node.children.empty())
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    if (node.instance_id == selected_instance_id)
        flags |= ImGuiTreeNodeFlags_Selected;
    if (matches)
        flags |= ImGuiTreeNodeFlags_DefaultOpen;

    if (!node.active)
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    const bool open = ImGui::TreeNodeEx("##node", flags, "%s", node.name.c_str());
    if (!node.active)
        ImGui::PopStyleColor();

    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        enqueue_hierarchy_command(CommandKind::FocusSelected, node, revision);
    }
    else if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        enqueue_hierarchy_command(CommandKind::Select, node, revision);
    }
    render_context_menu(node, revision, clipboard);

    if (open && !node.children.empty()) {
        for (const HierarchyNode &child : node.children)
            render_node(child, selected_instance_id, matches, include_inactive, revision, clipboard);
        ImGui::TreePop();
    }
    ImGui::PopID();
}

} // namespace
void render_hierarchy(const HierarchyInfo &hierarchy, int selected_instance_id,
                      const Snapshot::TransformClipboard& clipboard) {
    static int filter_mode_index = 0;
    constexpr const char *filter_modes[] = {"Name, tag or instance ID", "Name", "Tag", "Instance ID"};
    static bool include_inactive = true;
    if (ImGui::SmallButton("+##hierarchy-actions"))
        ImGui::OpenPopup("##hierarchy-actions-popup");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Scene actions and Hierarchy options");
    if (ImGui::BeginPopup("##hierarchy-actions-popup")) {
        ImGui::Checkbox("Show inactive objects", &include_inactive);
        ImGui::SeparatorText("Scenes in Build Settings");
        if (hierarchy.available_scenes.empty()) {
            ImGui::TextDisabled("No build scenes are available.");
        } else {
            for (const SceneLoadInfo &scene : hierarchy.available_scenes) {
                ImGui::PushID(scene.build_index);
                const std::string label = "Load [" + std::to_string(scene.build_index) + "] " + scene.name;
                if (ImGui::MenuItem(label.c_str(), nullptr, false, !scene.active)) {
                    Command command{.kind = CommandKind::LoadScene};
                    command.int_value = scene.build_index;
                    command.text = scene.path;
                    RuntimeModel::instance().enqueue(std::move(command));
                }
                if (ImGui::IsItemHovered() && !scene.path.empty())
                    ImGui::SetTooltip("%s%s", scene.path.c_str(), scene.loaded ? "\nLoaded" : "");
                ImGui::PopID();
            }
        }
        ImGui::SeparatorText("Load by path or name");
        static std::vector<char> manual_scene_key;
        ImGui::SetNextItemWidth(320.0f);
        input_text_dynamic("##manual-scene-key", "Assets/.../Scene.unity or scene name", manual_scene_key);
        ImGui::BeginDisabled(manual_scene_key.empty() || manual_scene_key.front() == '\0');
        if (ImGui::Button("Load Scene", ImVec2(-1.0f, 0.0f))) {
            Command command{.kind = CommandKind::LoadScene};
            command.int_value = -1;
            command.text = std::string(manual_scene_key.data());
            RuntimeModel::instance().enqueue(std::move(command));
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    const float mode_width = std::min(155.0f, ImGui::GetContentRegionAvail().x * 0.38f);
    ImGui::SetNextItemWidth(std::max(100.0f, ImGui::GetContentRegionAvail().x - mode_width -
                                             ImGui::GetStyle().ItemSpacing.x));
    ImGui::InputTextWithHint("##hierarchy-search", "Search GameObjects...", search_buffer().data(),
                             search_buffer().size());
    ImGui::SameLine();
    ImGui::SetNextItemWidth(mode_width);
    ImGui::Combo("##hierarchy-filter-mode", &filter_mode_index, filter_modes, IM_ARRAYSIZE(filter_modes));

    const std::string_view filter(search_buffer().data());
    const auto filter_mode = static_cast<HierarchyFilterMode>(std::clamp(filter_mode_index, 0, 3));
    NodeMatchSet *matches = nullptr;
    if (!filter.empty()) {
        static std::uint64_t cached_hierarchy_revision = 0;
        static std::string cached_filter;
        static bool cached_include_inactive = true;
        static HierarchyFilterMode cached_mode = HierarchyFilterMode::Everything;
        NodeMatchSet &cached_matches = hierarchy_filter_matches();
        if (cached_hierarchy_revision != hierarchy.revision || cached_filter != filter ||
            cached_include_inactive != include_inactive || cached_mode != filter_mode) {
            cached_matches.clear();
            if (cached_matches.bucket_count() < hierarchy.objects)
                cached_matches.reserve(hierarchy.objects);
            for (const SceneNode &scene : hierarchy.scenes)
                for (const HierarchyNode &root : scene.roots)
                    collect_matching_nodes(root, filter, filter_mode, include_inactive, cached_matches);
            cached_hierarchy_revision = hierarchy.revision;
            cached_filter = filter;
            cached_include_inactive = include_inactive;
            cached_mode = filter_mode;
        }
        matches = &cached_matches;
    }
    const float status_height = ImGui::GetTextLineHeightWithSpacing();
    ImGui::BeginChild("##hierarchy-results", ImVec2(0.0f, -status_height), false);
    for (const SceneNode &scene : hierarchy.scenes) {
        const int group_id = scene.dont_destroy_on_load ? -1 : scene.hide_and_dont_save ? -2 : scene.handle;
        ImGui::PushID(group_id);
        ImGuiTreeNodeFlags scene_flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth;
        const char *marker = scene.dont_destroy_on_load ? "DontDestroyOnLoad"
                             : scene.hide_and_dont_save ? "Hidden / Dont Save"
                                                        : scene.name.c_str();
        const bool open = ImGui::TreeNodeEx("##scene", scene_flags, "%s", marker);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s%s", scene.name.c_str(), scene.active ? "\nActive scene" : "");
        if (open) {
            for (const HierarchyNode &root : scene.roots)
                render_node(root, selected_instance_id, matches, include_inactive, hierarchy.revision, clipboard);
            if (scene.roots.empty())
                ImGui::TextDisabled("  No root GameObjects");
            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    if (hierarchy.scenes.empty())
        ImGui::TextDisabled("Waiting for a loaded scene...");

    if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemHovered()) {
        enqueue_simple(CommandKind::ClearSelection, 0);
    }
    ImGui::EndChild();
    ImGui::TextDisabled("%zu GameObjects  |  %zu roots", hierarchy.objects, hierarchy.roots);
}

} // namespace Explorer::UI

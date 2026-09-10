// Copyright (c) 2026 Jadis0x. All rights reserved.
#include "explorer_ui.h"

#include "config/mod_config.h"
#include "config/user_settings.h"
#include "explorer_model.h"
#include "method_trace_format.h"
#include "reference_graph_ui.h"
#include "unity_editor_theme.h"
#include "ui_members.h"
#include "ui_shared.h"
#include "ui_state.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <shellapi.h>

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <cmath>
#include <cfloat>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <list>
#include <string>
#include <utility>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Explorer::UI {
namespace {

void render_toggle_key_setting() {
    static bool waiting_for_key = false;
    const std::string key_name = ModConfig::UserSettings::virtual_key_name(ModConfig::menu_toggle_key);
    ImGui::Text("Explorer toggle: %s", key_name.c_str());
    if (ImGui::SmallButton(waiting_for_key ? "Press a keyboard key..." : "Change key")) {
        waiting_for_key = !waiting_for_key;
        if (waiting_for_key)
            ModConfig::UserSettings::begin_toggle_key_capture();
        else
            ModConfig::UserSettings::end_toggle_key_capture();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Reset F7")) {
        waiting_for_key = false;
        ModConfig::UserSettings::end_toggle_key_capture();
        ModConfig::UserSettings::save_toggle_key(VK_F7);
    }

    if (waiting_for_key) {
        const int key = ModConfig::UserSettings::poll_toggle_key_capture();
        if (key != 0 && ModConfig::UserSettings::save_toggle_key(key)) {
                waiting_for_key = false;
                ModConfig::UserSettings::end_toggle_key_capture();
        }
    }
    if (!ModConfig::UserSettings::last_error().empty())
        ImGui::TextColored(ImVec4(0.78f, 0.42f, 0.38f, 1.0f), "%s",
                           ModConfig::UserSettings::last_error().c_str());
}

void render_diagnostics(const Snapshot &snapshot) {
    if (ImGui::Button("Create diagnostic bundle"))
        RuntimeModel::instance().enqueue(Command{.kind = CommandKind::ExportDiagnosticBundle});
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Export backend, Unity version, capabilities, recent errors and the flight recorder");
    if (!snapshot.diagnostic_bundle_path.empty()) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Copy bundle path"))
            ImGui::SetClipboardText(snapshot.diagnostic_bundle_path.c_str());
        ImGui::TextDisabled("Latest bundle: %s", snapshot.diagnostic_bundle_path.c_str());
    }
    ImGui::TextDisabled("%s | Unity %s | capabilities 0x%llX", snapshot.runtime_backend.c_str(),
                        snapshot.unity_version.empty() ? "unavailable" : snapshot.unity_version.c_str(),
                        static_cast<unsigned long long>(snapshot.runtime_capabilities));
    ImGui::Separator();
    if (snapshot.diagnostics.empty()) {
        ImGui::TextDisabled("No errors or external overwrite events.");
    } else {
        for (const std::string &diagnostic : snapshot.diagnostics) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.78f, 0.42f, 0.38f, 1.0f));
            ImGui::Bullet();
            ImGui::TextWrapped("%s", diagnostic.c_str());
            ImGui::PopStyleColor();
        }
    }
    if (ImGui::Button("Clear diagnostics"))
        RuntimeModel::instance().enqueue(Command{.kind = CommandKind::ClearDiagnostics});
    ImGui::SeparatorText("Flight recorder (last 50 operations)");
    if (snapshot.flight_recorder.empty()) {
        ImGui::TextDisabled("No recorded Explorer operations.");
    } else {
        for (auto it = snapshot.flight_recorder.rbegin(); it != snapshot.flight_recorder.rend(); ++it) {
            const Snapshot::FlightEvent& event = *it;
            const bool fault = event.stage == "FAULT";
            ImGui::PushStyleColor(ImGuiCol_Text, fault ? ImVec4(0.78f, 0.42f, 0.38f, 1.0f)
                                                       : ImVec4(0.62f, 0.72f, 0.82f, 1.0f));
            ImGui::Text("#%llu  +%.2fs  %s  %s%s%s", static_cast<unsigned long long>(event.sequence),
                        event.seconds_since_start, event.stage.c_str(), event.operation.c_str(),
                        event.detail.empty() ? "" : " — ", event.detail.c_str());
            ImGui::PopStyleColor();
        }
    }
    if (ImGui::Button("Clear flight recorder"))
        RuntimeModel::instance().enqueue(Command{.kind = CommandKind::ClearFlightRecorder});
}

// Screen picking. The overlay owns the click, so the whole interaction lives
// here: arm the mode, draw what is under the cursor, and hand the point to the
// model, which is the only place allowed to touch the managed runtime.
struct PickState {
    bool armed = false;
    bool include_inactive = false;
    bool list_open = false;
    // Set while a row of the "Under cursor" list is hovered, so that object is
    // outlined too. Reset every frame.
    int hovered_instance_id = 0;
    std::uint64_t seen_revision = 0;
};

PickState &pick_state() {
    static PickState state;
    return state;
}

void draw_pick_outline(ImDrawList *draw_list, const ScreenPickHit &hit, const ImVec2 &origin, ImU32 color,
                       float thickness) {
    const ImVec2 min(origin.x + hit.min_x, origin.y + hit.min_y);
    const ImVec2 max(origin.x + hit.max_x, origin.y + hit.max_y);
    draw_list->AddRect(min, max, color, 0.0f, 0, thickness);
}

void select_picked(int instance_id) {
    if (instance_id == 0)
        return;
    enqueue_simple(CommandKind::Select, instance_id);
    reveal_in_hierarchy(instance_id);
}

void render_screen_pick(const Snapshot &snapshot, const ImGuiViewport &viewport, bool &show_hierarchy) {
    PickState &state = pick_state();
    ImGuiIO &io = ImGui::GetIO();
    ImDrawList *draw_list = ImGui::GetForegroundDrawList();
    const ImVec2 origin = viewport.Pos;
    const ScreenPickResult &result = snapshot.screen_pick;

    // A fresh result selects its top hit and unfolds the tree to it - the part
    // that makes this feel like the editor rather than a readout.
    if (result.valid && result.revision != state.seen_revision) {
        state.seen_revision = result.revision;
        if (!result.hits.empty()) {
            show_hierarchy = true;
            reveal_in_hierarchy(result.hits.front().instance_id);
            state.list_open = result.hits.size() > 1;
        } else {
            state.list_open = false;
        }
    }

    // One box, only while the mode is live. Outlining every hit left a scatter
    // of rectangles standing on the screen long after the click that made them,
    // which reads as the overlay drawing stray highlights.
    const int hovered = state.hovered_instance_id;
    state.hovered_instance_id = 0;
    if (draw_list && (state.armed || state.list_open) && result.valid && !result.hits.empty()) {
        const ScreenPickHit *shown = &result.hits.front();
        for (const ScreenPickHit &hit : result.hits) {
            if (hit.instance_id == snapshot.selected_instance_id) {
                shown = &hit;
                break;
            }
        }
        // Hovering a row of the list previews that object before committing.
        if (hovered != 0) {
            for (const ScreenPickHit &hit : result.hits) {
                if (hit.instance_id != hovered)
                    continue;
                draw_pick_outline(draw_list, hit, origin, IM_COL32(230, 230, 230, 200), 1.0f);
                break;
            }
        }
        draw_pick_outline(draw_list, *shown, origin, IM_COL32(90, 170, 255, 235), 2.0f);
        const ImVec2 label(origin.x + shown->min_x, origin.y + shown->min_y - ImGui::GetTextLineHeight() - 2.0f);
        draw_list->AddText(label, IM_COL32(255, 255, 255, 235), shown->name.c_str());
    }

    if (state.armed && draw_list) {
        const ImVec2 cursor = io.MousePos;
        const bool over_panels = io.WantCaptureMouse;
        const ImU32 color = over_panels ? IM_COL32(140, 140, 140, 140) : IM_COL32(90, 170, 255, 235);
        constexpr float kArm = 11.0f;
        draw_list->AddLine(ImVec2(cursor.x - kArm, cursor.y), ImVec2(cursor.x - 3.0f, cursor.y), color, 1.0f);
        draw_list->AddLine(ImVec2(cursor.x + 3.0f, cursor.y), ImVec2(cursor.x + kArm, cursor.y), color, 1.0f);
        draw_list->AddLine(ImVec2(cursor.x, cursor.y - kArm), ImVec2(cursor.x, cursor.y - 3.0f), color, 1.0f);
        draw_list->AddLine(ImVec2(cursor.x, cursor.y + 3.0f), ImVec2(cursor.x, cursor.y + kArm), color, 1.0f);
        draw_list->AddText(ImVec2(cursor.x + 14.0f, cursor.y + 6.0f), color,
                           over_panels ? "Select in Game: move the cursor over the game"
                                       : "Select in Game: click an object  |  Esc to cancel");

        // A click on an Explorer panel belongs to that panel, not to the pick.
        if (!over_panels && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            Command command{.kind = CommandKind::PickAtScreenPoint};
            command.vector_value = {cursor.x - origin.x, cursor.y - origin.y, 0.0f};
            // The height the click was measured against, for the flip out of
            // Unity bottom-left screen space.
            command.float_value = viewport.Size.y;
            command.bool_value = state.include_inactive;
            RuntimeModel::instance().enqueue(std::move(command));
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape))
            state.armed = false;
    }

    if (!state.list_open || !result.valid || result.hits.size() < 2)
        return;
    // A crowded click: the same list the editor offers when several objects
    // overlap, in the order they are drawn.
    ImGui::SetNextWindowPos(ImVec2(origin.x + result.point_x + 16.0f, origin.y + result.point_y + 16.0f),
                            ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(ImVec2(360.0f, 0.0f), ImGuiCond_Appearing);
    char title[64];
    std::snprintf(title, sizeof(title), "Under cursor (%zu)###urk-pick-list", result.hits.size());
    if (ImGui::Begin(title, &state.list_open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking)) {
        ImGui::TextDisabled("Topmost first. UI draws over the world.");
        for (std::size_t index = 0; index < result.hits.size(); ++index) {
            const ScreenPickHit &hit = result.hits[index];
            ImGui::PushID(static_cast<int>(index));
            const bool selected = hit.instance_id == snapshot.selected_instance_id;
            if (ImGui::Selectable(hit.name.c_str(), selected))
                select_picked(hit.instance_id);
            if (ImGui::IsItemHovered()) {
                state.hovered_instance_id = hit.instance_id;
                if (!hit.path.empty())
                    ImGui::SetTooltip("%s", hit.path.c_str());
            }
            ImGui::SameLine();
            if (hit.ui)
                ImGui::TextDisabled("%s", hit.source_type.c_str());
            else
                ImGui::TextDisabled("%s  |  %.1f units", hit.source_type.c_str(), hit.distance);
            ImGui::PopID();
        }
    }
    ImGui::End();
}

void render_reference_graph(const Snapshot& snapshot) {
    ReferenceGraphUI::render(snapshot);
}

} // namespace

void render() {
    if (!ModConfig::show_menu)
        return;

    const auto snapshot = RuntimeModel::instance().snapshot();
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    if (!viewport || viewport->WorkSize.x <= 1.0f || viewport->WorkSize.y <= 1.0f)
        return;
    static const auto empty_hierarchy = std::make_shared<const HierarchyInfo>();
    const auto hierarchy = snapshot->hierarchy ? snapshot->hierarchy : empty_hierarchy;
    const ImVec2 work_pos = viewport->WorkPos;
    const ImVec2 work_size = viewport->WorkSize;
    static float opacity = 0.94f;
    static float highlight_max_distance = 0.0f;
    static bool show_hierarchy = true;
    static bool show_inspector = true;
    static bool show_object_inspector = false;
    static bool show_class_browser = false;
    static bool show_method_traces = false;
    static bool show_field_watches = false;
    static bool show_reference_graph = false;
    static bool show_diagnostics = false;
    static ImVec2 inspector_window_size{};
    static int previous_selection_id = 0;
    static std::uint64_t previous_object_token = 0;
    static std::size_t previous_trace_count = 0;
    static std::size_t previous_field_watch_count = 0;
    static bool dock_layout_initialized = false;
    const bool selection_changed = snapshot->selected_instance_id != previous_selection_id;
    if (selection_changed) {
        ui_state().clear_selection_scoped();
        component_buffers() = {};
        previous_selection_id = snapshot->selected_instance_id;
        // Hierarchy selection reopens the Inspector.
        if (snapshot->selected_instance_id != 0)
            show_inspector = true;
    }
    const bool class_browser_owns_object_target =
        snapshot->object_inspector.valid &&
        class_browser_target_token() == snapshot->object_inspector.token;
    if (snapshot->object_inspector.valid && snapshot->object_inspector.token != previous_object_token &&
        !class_browser_owns_object_target)
        show_object_inspector = true;
    if (object_inspector_window_requested()) {
        show_object_inspector = true;
        object_inspector_window_requested() = false;
    }
    previous_object_token = snapshot->object_inspector.valid ? snapshot->object_inspector.token : 0;
    if (snapshot->method_traces.size() > previous_trace_count)
        show_method_traces = true;
    previous_trace_count = snapshot->method_traces.size();
    if (snapshot->field_watches.size() > previous_field_watch_count)
        show_field_watches = true;
    previous_field_watch_count = snapshot->field_watches.size();

    // Unity's editor chrome is a compact 11pt UI; the overlay's shared font is
    // sized for a mod menu, so the Explorer runs at its own size.
    ImGui::PushFont(nullptr, Unity::font_size());
    const int pushed_colors = Unity::push_style(opacity);
    const ImGuiID dockspace_id = ImGui::GetID("URKExplorerDockSpace");
    // Rebuilding the layout would pull detached panels back to the main viewport.
    if (!dock_layout_initialized || !ImGui::DockBuilderGetNode(dockspace_id)) {
        if (ImGui::DockBuilderGetNode(dockspace_id))
            ImGui::DockBuilderRemoveNode(dockspace_id);
        ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockspace_id, work_size);
        ImGuiID workspace_dock = 0;
        ImGuiID content_dock = dockspace_id;
        // Unity's toolbar is a fixed strip, not a resizable pane, so the split
        // is sized from the strip's own height rather than a share of the screen.
        const float toolbar_ratio =
            std::clamp(Unity::toolbar_height() / std::max(1.0f, work_size.y), 0.02f, 0.20f);
        ImGui::DockBuilderSplitNode(content_dock, ImGuiDir_Up, toolbar_ratio, &workspace_dock, &content_dock);
        ImGuiID hierarchy_dock = 0;
        ImGui::DockBuilderSplitNode(content_dock, ImGuiDir_Left, 0.21f, &hierarchy_dock, &content_dock);
        ImGuiID inspector_dock = 0;
        ImGui::DockBuilderSplitNode(content_dock, ImGuiDir_Right, 0.36f, &inspector_dock, &content_dock);
        ImGuiID diagnostics_dock = 0;
        ImGui::DockBuilderSplitNode(content_dock, ImGuiDir_Down, 0.28f, &diagnostics_dock, &content_dock);
        // The toolbar wears no tab and cannot be dragged off, the way the
        // editor's own toolbar behaves.
        if (ImGuiDockNode *toolbar_node = ImGui::DockBuilderGetNode(workspace_dock))
            toolbar_node->LocalFlags |= ImGuiDockNodeFlags_NoTabBar | ImGuiDockNodeFlags_NoDockingOverMe |
                                        ImGuiDockNodeFlags_NoDockingSplit;
        ImGui::DockBuilderDockWindow("URK Explorer Workspace", workspace_dock);
        if (show_hierarchy)
            ImGui::DockBuilderDockWindow("Hierarchy##urk-hierarchy", hierarchy_dock);
        if (show_inspector)
            ImGui::DockBuilderDockWindow("###urk-inspector", inspector_dock);
        if (show_object_inspector)
            ImGui::DockBuilderDockWindow("###urk-object", content_dock);
        if (show_class_browser)
            ImGui::DockBuilderDockWindow("###urk-class-browser", content_dock);
        if (show_method_traces)
            ImGui::DockBuilderDockWindow("###urk-method-traces", content_dock);
        if (show_field_watches)
            ImGui::DockBuilderDockWindow("###urk-field-watches", content_dock);
        if (show_reference_graph)
            ImGui::DockBuilderDockWindow("###urk-reference-graph", content_dock);
        if (show_diagnostics)
            ImGui::DockBuilderDockWindow("###urk-diagnostics", diagnostics_dock);
        ImGui::DockBuilderFinish(dockspace_id);
        dock_layout_initialized = true;
    }
    ImGui::DockSpaceOverViewport(dockspace_id, viewport, ImGuiDockNodeFlags_PassthruCentralNode);

    ImGui::SetNextWindowPos(ImVec2(work_pos.x + 12.0f, work_pos.y + 12.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(std::min(980.0f, work_size.x - 24.0f), 112.0f), ImGuiCond_FirstUseEver);
    // Unity's chrome is a menu strip over a toolbar strip, so the workspace
    // window is exactly that: menus for kinds of action, and a fixed row of
    // square toolbar controls underneath. It never scrolls.
    if (ImGui::Begin("URK Explorer Workspace", nullptr,
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoScrollbar |
                         ImGuiWindowFlags_NoScrollWithMouse)) {
        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("Panels")) {
                ImGui::MenuItem("Hierarchy", nullptr, &show_hierarchy);
                ImGui::MenuItem("Inspector", nullptr, &show_inspector);
                ImGui::MenuItem("Object Inspector", nullptr, &show_object_inspector);
                ImGui::MenuItem("Class Browser", nullptr, &show_class_browser);
                ImGui::Separator();
                ImGui::MenuItem("Method Calls", nullptr, &show_method_traces);
                ImGui::MenuItem("Value Watches", nullptr, &show_field_watches);
                ImGui::MenuItem("Reference Graph", nullptr, &show_reference_graph);
                ImGui::MenuItem("Console", nullptr, &show_diagnostics);
                ImGui::Separator();
                if (ImGui::MenuItem("Runtime Monitor")) {
                    show_method_traces = true;
                    show_field_watches = true;
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Opens Method Calls and Value Watches together");
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Scene")) {
                ImGui::SeparatorText("Scenes in Build Settings");
                if (hierarchy->available_scenes.empty()) {
                    ImGui::TextDisabled("No build scenes are available.");
                } else {
                    for (const SceneLoadInfo &scene : hierarchy->available_scenes) {
                        ImGui::PushID(scene.build_index);
                        const std::string label = "[" + std::to_string(scene.build_index) + "] " + scene.name;
                        if (ImGui::MenuItem(label.c_str(), scene.loaded ? "loaded" : nullptr, false, !scene.active)) {
                            Command command{.kind = CommandKind::LoadScene};
                            command.int_value = scene.build_index;
                            command.text = scene.path;
                            RuntimeModel::instance().enqueue(std::move(command));
                        }
                        if (ImGui::IsItemHovered() && !scene.path.empty())
                            ImGui::SetTooltip("%s%s", scene.path.c_str(), scene.active ? "\nActive scene" : "");
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
                }
                ImGui::EndDisabled();
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Settings")) {
                ImGui::SeparatorText("Overlay");
                ImGui::SetNextItemWidth(180.0f);
                ImGui::SliderFloat("UI scale", &Unity::ui_scale(), 0.75f, 2.0f, "%.2fx");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Text and control size. 1.00x matches the Unity editor at 100%% display scaling.");
                ImGui::SameLine();
                if (ImGui::SmallButton("Reset##ui-scale"))
                    Unity::ui_scale() = 1.0f;
                ImGui::SetNextItemWidth(180.0f);
                ImGui::SliderFloat("Panel background", &opacity, 0.35f, 1.0f, "%.2f");
                ImGui::TextDisabled("Text, controls and borders remain opaque for readability.");
                const bool multi_monitor =
                    (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0;
                ImGui::TextDisabled("Multi-monitor windows: %s",
                                    multi_monitor ? "enabled" : "unavailable on this renderer");
                if (multi_monitor)
                    ImGui::TextDisabled("Drag a panel's tab out of the dock, then onto your other monitor.");
                ImGui::SeparatorText("Controls");
                render_toggle_key_setting();
                ImGui::SeparatorText("Screen Pick");
                ImGui::Checkbox("Include inactive objects", &pick_state().include_inactive);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("A switched-off object is invisible, so it is normally not considered to be "
                                      "under the cursor. Turn this on to find one anyway.");
                ImGui::SeparatorText("Selection Highlight");
                bool highlight_enabled = snapshot->highlight_enabled;
                if (ImGui::Checkbox("Enabled", &highlight_enabled)) {
                    Command command{.kind = CommandKind::SetHighlightEnabled};
                    command.bool_value = highlight_enabled;
                    RuntimeModel::instance().enqueue(std::move(command));
                }
                ImGui::BeginDisabled(!highlight_enabled);
                ImGui::SetNextItemWidth(220.0f);
                if (ImGui::DragFloat("Max distance", &highlight_max_distance, 10.0f, 0.0f, 100000.0f, "%.0f")) {
                    highlight_max_distance = std::clamp(highlight_max_distance, 0.0f, 100000.0f);
                    Command command{.kind = CommandKind::SetHighlightDistance};
                    command.float_value = highlight_max_distance;
                    RuntimeModel::instance().enqueue(std::move(command));
                }
                ImGui::SameLine();
                ImGui::TextDisabled("0 = unlimited");
                ImGui::EndDisabled();
                ImGui::TextDisabled("Measured from the active game camera.");
                ImGui::SeparatorText("Camera Focus");
                bool camera_focus_top_down = snapshot->camera_focus_top_down;
                if (ImGui::Checkbox("Force top-down view", &camera_focus_top_down)) {
                    Command command{.kind = CommandKind::SetCameraFocusTopDown};
                    command.bool_value = camera_focus_top_down;
                    RuntimeModel::instance().enqueue(std::move(command));
                }
                float camera_focus_distance = snapshot->camera_focus_distance;
                ImGui::SetNextItemWidth(220.0f);
                if (ImGui::DragFloat(camera_focus_top_down ? "Top-down height / zoom" : "Focus distance / zoom",
                                     &camera_focus_distance, 0.25f, 1.0f, 100.0f, "%.2f units")) {
                    camera_focus_distance = std::clamp(camera_focus_distance, 1.0f, 100.0f);
                    Command command{.kind = CommandKind::SetCameraFocusDistance};
                    command.float_value = camera_focus_distance;
                    RuntimeModel::instance().enqueue(std::move(command));
                }
                if (camera_focus_top_down) {
                    float camera_focus_tilt = snapshot->camera_focus_tilt;
                    ImGui::SetNextItemWidth(220.0f);
                    if (ImGui::DragFloat("Perspective tilt", &camera_focus_tilt, 0.25f, 0.0f, 100.0f, "%.2f units")) {
                        camera_focus_tilt = std::clamp(camera_focus_tilt, 0.0f, 100.0f);
                        Command command{.kind = CommandKind::SetCameraFocusTilt};
                        command.float_value = camera_focus_tilt;
                        RuntimeModel::instance().enqueue(std::move(command));
                    }
                }
                float camera_offset[3]{
                    snapshot->camera_focus_offset.x,
                    snapshot->camera_focus_offset.y,
                    snapshot->camera_focus_offset.z,
                };
                ImGui::SetNextItemWidth(300.0f);
                if (ImGui::DragFloat3("Target offset X/Y/Z", camera_offset, 0.1f, -10000.0f, 10000.0f, "%.2f")) {
                    Command command{.kind = CommandKind::SetCameraFocusOffset};
                    command.vector_value = {camera_offset[0], camera_offset[1], camera_offset[2]};
                    RuntimeModel::instance().enqueue(std::move(command));
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Reset##camera-offset")) {
                    Command command{.kind = CommandKind::SetCameraFocusOffset};
                    command.vector_value = {};
                    RuntimeModel::instance().enqueue(std::move(command));
                }
                ImGui::TextDisabled(camera_focus_top_down
                    ? "Height and tilt define the base view; X/Y/Z adds a world-space offset."
                    : "Distance preserves the active view axis; X/Y/Z offsets the camera in world space.");
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Help")) {
                if (ImGui::MenuItem("GitHub"))
                    ShellExecuteA(nullptr, "open", ModConfig::url, nullptr, nullptr, SW_SHOWNORMAL);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", ModConfig::url);
                if (ImGui::MenuItem("Support"))
                    ShellExecuteA(nullptr, "open", ModConfig::social, nullptr, nullptr, SW_SHOWNORMAL);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", ModConfig::social);
                ImGui::EndMenu();
            }

            // Kept out of the toolbar row, which grows with the panel count and
            // used to draw straight over this.
            char version[32];
            std::snprintf(version, sizeof(version), "v%s", ModConfig::version);
            const std::string status = snapshot->status.empty() ? "Ready" : snapshot->status;
            const ImGuiStyle &menu_style = ImGui::GetStyle();
            const auto segment_width = [&menu_style](const char *text) {
                return ImGui::CalcTextSize(text).x + menu_style.ItemSpacing.x;
            };
            const float identity_width = segment_width(ModConfig::short_name) +
                                         segment_width(ModConfig::backend_name) + segment_width(version) +
                                         segment_width(ModConfig::author) + segment_width(status.c_str());
            const float identity_x = ImGui::GetContentRegionMax().x - identity_width;
            if (identity_x > ImGui::GetCursorPosX() + 12.0f) {
                ImGui::SetCursorPosX(identity_x);
                ImGui::TextColored(ImVec4(0.92f, 0.92f, 0.92f, 1.0f), "%s", ModConfig::short_name);
                ImGui::TextColored(Unity::Skin::accent, "%s", ModConfig::backend_name);
                ImGui::TextDisabled("%s", version);
                ImGui::TextColored(ImVec4(0.82f, 0.66f, 0.36f, 1.0f), "%s", ModConfig::author);
                // Green when idle, amber when the last command left something to read.
                ImGui::TextColored(status == "Ready" ? ImVec4(0.48f, 0.74f, 0.52f, 1.0f) : Unity::Skin::warning,
                                   "%s", status.c_str());
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", status.c_str());
            }
            ImGui::EndMenuBar();
        }
        // The toolbar row. Refresh sits where Unity puts Play - leftmost and
        // always one click away - then the runtime toggles, then the panel
        // buttons that stand in for Unity's Layers / Layout controls.
        if (Unity::toolbar_button("Refresh", Unity::Skin::action_blue,
                                  "Re-read the scene hierarchy from the running game"))
            enqueue_simple(CommandKind::Refresh, 0);
        ImGui::SameLine(0.0f, 2.0f);
        if (Unity::toolbar_toggle("Live", snapshot->live_data, Unity::Skin::action_green,
                                  "Re-read inspected values from the running game every tick")) {
            Command command{.kind = CommandKind::SetLiveData};
            command.bool_value = !snapshot->live_data;
            RuntimeModel::instance().enqueue(std::move(command));
        }
        ImGui::SameLine(0.0f, 2.0f);
        ImGui::BeginDisabled(!snapshot->camera_focus_active);
        if (Unity::toolbar_button("Return Camera", Unity::Skin::action_amber,
                                  "Restore the camera pose saved before focusing"))
            enqueue_simple(CommandKind::RestoreCamera, 0);
        ImGui::EndDisabled();
        ImGui::SameLine(0.0f, 2.0f);
        if (Unity::toolbar_toggle("Select in Game", pick_state().armed, Unity::Skin::action_violet,
                                  "Click anything in the game to select it in the Hierarchy, the way the scene view "
                                  "does in the editor. Esc cancels."))
            pick_state().armed = !pick_state().armed;
        Unity::toolbar_separator();

        struct PanelToggle {
            const char *label;
            bool *visible;
        };
        // Abbreviations here told nobody anything: each button now carries the
        // panel's own title, which is what the tab and the Panels menu say too.
        const PanelToggle toggles[] = {
            {"Hierarchy", &show_hierarchy},
            {"Inspector", &show_inspector},
            {"Object Inspector", &show_object_inspector},
            {"Class Browser", &show_class_browser},
            {"Method Calls", &show_method_traces},
            {"Value Watches", &show_field_watches},
            {"Reference Graph", &show_reference_graph},
            {"Console", &show_diagnostics},
        };
        // The strip does not scroll or wrap, so a row too wide for it runs off the
        // edge rather than clipping. Measure first, collapse to a dropdown if needed.
        constexpr float kToggleSpacing = 2.0f;
        float toggles_width = 0.0f;
        for (const PanelToggle &toggle : toggles)
            toggles_width += Unity::toolbar_control_width(toggle.label) + kToggleSpacing;
        const float toggles_space = ImGui::GetWindowContentRegionMax().x - ImGui::GetCursorPosX();
        if (toggles_width <= toggles_space) {
            for (std::size_t index = 0; index < static_cast<std::size_t>(IM_ARRAYSIZE(toggles)); ++index) {
                if (index > 0)
                    ImGui::SameLine(0.0f, kToggleSpacing);
                if (Unity::toolbar_toggle(toggles[index].label, *toggles[index].visible))
                    *toggles[index].visible = !*toggles[index].visible;
            }
        } else {
            std::size_t open_panels = 0;
            for (const PanelToggle &toggle : toggles)
                open_panels += *toggle.visible ? 1u : 0u;
            char label[48];
            std::snprintf(label, sizeof(label), "Panels (%zu/%d)", open_panels, IM_ARRAYSIZE(toggles));
            if (Unity::toolbar_toggle(label, open_panels != 0, "Show or hide Explorer panels"))
                ImGui::OpenPopup("##urk-panel-toggles");
            if (ImGui::BeginPopup("##urk-panel-toggles")) {
                for (const PanelToggle &toggle : toggles)
                    ImGui::MenuItem(toggle.label, nullptr, toggle.visible);
                ImGui::EndPopup();
            }
        }
    }
    ImGui::End();

    if (show_hierarchy) {
        ImGui::SetNextWindowPos(ImVec2(work_pos.x + 12.0f, work_pos.y + 120.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(std::max(300.0f, work_size.x * 0.26f), std::max(420.0f, work_size.y * 0.72f)),
                                 ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Hierarchy##urk-hierarchy", &show_hierarchy, ImGuiWindowFlags_NoCollapse)) {
            render_hierarchy(*hierarchy, snapshot->selected_instance_id, snapshot->transform_clipboard);
        }
        ImGui::End();
    }

    if (show_inspector) {
        const float inspector_width = std::max(340.0f, work_size.x * 0.30f);
        ImGui::SetNextWindowPos(ImVec2(work_pos.x + work_size.x - inspector_width - 12.0f, work_pos.y + 120.0f),
                                ImGuiCond_FirstUseEver);
        if (inspector_window_size.x > 0.0f && inspector_window_size.y > 0.0f)
            ImGui::SetNextWindowSize(inspector_window_size, ImGuiCond_FirstUseEver);
        else
            ImGui::SetNextWindowSize(ImVec2(inspector_width, std::max(480.0f, work_size.y * 0.80f)),
                                     ImGuiCond_FirstUseEver);
        const std::string title = "Inspector###urk-inspector";
        if (ImGui::Begin(title.c_str(), &show_inspector, ImGuiWindowFlags_NoCollapse)) {
            render_inspector(*snapshot);
            inspector_window_size = ImGui::GetWindowSize();
        }
        ImGui::End();
    }

    if (show_object_inspector) {
        ImGui::SetNextWindowPos(ImVec2(work_pos.x + work_size.x * 0.28f, work_pos.y + 130.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(std::max(500.0f, work_size.x * 0.48f), std::max(420.0f, work_size.y * 0.68f)),
                                 ImGuiCond_FirstUseEver);
        const std::string title = snapshot->object_inspector.valid
                                      ? "Object Inspector - " + snapshot->object_inspector.type_name + "###urk-object"
                                      : "Object Inspector###urk-object";
        if (ImGui::Begin(title.c_str(), &show_object_inspector, ImGuiWindowFlags_NoCollapse)) {
            render_object_inspector(*snapshot);
        }
        ImGui::End();
    }

    if (show_class_browser) {
        ImGui::SetNextWindowPos(ImVec2(work_pos.x + work_size.x * 0.20f, work_pos.y + 150.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(std::max(520.0f, work_size.x * 0.42f), std::max(520.0f, work_size.y * 0.74f)),
                                 ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Class Browser###urk-class-browser", &show_class_browser, ImGuiWindowFlags_NoCollapse)) {
            render_class_browser(*snapshot);
        }
        ImGui::End();
    }

    if (show_method_traces) {
        ImGui::SetNextWindowPos(ImVec2(work_pos.x + work_size.x * 0.18f, work_pos.y + 145.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(std::max(680.0f, work_size.x * 0.58f), std::max(420.0f, work_size.y * 0.62f)),
                                 ImGuiCond_FirstUseEver);
        const std::string title = "Runtime Monitor - Calls (" + std::to_string(snapshot->method_traces.size()) + ")###urk-method-traces";
        if (ImGui::Begin(title.c_str(), &show_method_traces, ImGuiWindowFlags_NoCollapse)) {
            render_method_traces(*snapshot);
        }
        ImGui::End();
    }

    if (show_field_watches) {
        ImGui::SetNextWindowPos(ImVec2(work_pos.x + work_size.x * 0.20f, work_pos.y + 170.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(std::max(650.0f, work_size.x * 0.54f), std::max(360.0f, work_size.y * 0.54f)),
                                 ImGuiCond_FirstUseEver);
        const std::string title =
            "Runtime Monitor - Values (" + std::to_string(snapshot->field_watches.size()) + ")###urk-field-watches";
        if (ImGui::Begin(title.c_str(), &show_field_watches, ImGuiWindowFlags_NoCollapse)) {
            render_field_watches(*snapshot);
        }
        ImGui::End();
    }

    if (show_reference_graph) {
        ImGui::SetNextWindowPos(ImVec2(work_pos.x + work_size.x * 0.18f, work_pos.y + 145.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(std::max(760.0f, work_size.x * 0.62f),
                                       std::max(480.0f, work_size.y * 0.68f)), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Reference Graph###urk-reference-graph", &show_reference_graph, ImGuiWindowFlags_NoCollapse)) {
            render_reference_graph(*snapshot);
        }
        ImGui::End();
    }

    if (show_diagnostics) {
        ImGui::SetNextWindowPos(ImVec2(work_pos.x + 30.0f, work_pos.y + work_size.y - 260.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(std::min(760.0f, work_size.x - 60.0f), 230.0f), ImGuiCond_FirstUseEver);
        const std::string title =
            "Console (" + std::to_string(snapshot->diagnostics.size()) + ")###urk-diagnostics";
        if (ImGui::Begin(title.c_str(), &show_diagnostics)) {
            ImGui::TextDisabled("Latest activity");
            ImGui::TextWrapped("%s", snapshot->status.empty() ? "Ready" : snapshot->status.c_str());
            ImGui::Separator();
            ImGui::Text("GC: %.1f MiB used / %.1f MiB heap | handles: %zu strong, %zu weak | quarantined: %llu",
                        static_cast<double>(snapshot->managed_used_bytes) / (1024.0 * 1024.0),
                        static_cast<double>(snapshot->managed_heap_bytes) / (1024.0 * 1024.0),
                        snapshot->strong_handle_count, snapshot->weak_handle_count,
                        static_cast<unsigned long long>(snapshot->quarantined_handle_count));
            ImGui::Separator();
            render_diagnostics(*snapshot);
        }
        ImGui::End();
    }

    render_screen_pick(*snapshot, *viewport, show_hierarchy);

    Unity::pop_style(pushed_colors);
    ImGui::PopFont();
}

} // namespace Explorer::UI

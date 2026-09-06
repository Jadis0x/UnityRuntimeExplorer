// Copyright (c) 2026 Jadis0x. All rights reserved.
#include "explorer_ui.h"

#include "config/mod_config.h"
#include "config/user_settings.h"
#include "explorer_model.h"
#include "method_trace_format.h"
#include "reference_graph_ui.h"
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

// One place to mix a colour towards a panel's accent. t = 0 keeps the neutral
// surface, t = 1 is the accent itself.
ImVec4 tint(const ImVec4& base, const ImVec4& accent, float t, float alpha) {
    return ImVec4(base.x + (accent.x - base.x) * t, base.y + (accent.y - base.y) * t,
                  base.z + (accent.z - base.z) * t, alpha);
}

ImVec4 with_alpha(const ImVec4& color, float alpha) {
    return ImVec4(color.x, color.y, color.z, alpha);
}

// The Explorer's own palette. Surfaces are cool near-blacks at three clearly
// separated levels - window, child, input well - so a panel, a list inside it
// and an editable field never read as the same slab of grey.
namespace Palette {
constexpr ImVec4 window{0.098f, 0.106f, 0.122f, 1.0f};
constexpr ImVec4 child{0.071f, 0.078f, 0.090f, 1.0f};
constexpr ImVec4 popup{0.129f, 0.139f, 0.157f, 1.0f};
constexpr ImVec4 well{0.047f, 0.052f, 0.063f, 1.0f};
constexpr ImVec4 raised{0.180f, 0.196f, 0.224f, 1.0f};
constexpr ImVec4 border{0.031f, 0.035f, 0.043f, 1.0f};
constexpr ImVec4 line{0.204f, 0.220f, 0.251f, 1.0f};
constexpr ImVec4 text{0.878f, 0.894f, 0.918f, 1.0f};
constexpr ImVec4 text_dim{0.478f, 0.510f, 0.561f, 1.0f};
constexpr ImVec4 accent{0.290f, 0.596f, 0.980f, 1.0f};
} // namespace Palette

int push_explorer_theme(float opacity) {
    const float surface_opacity = std::clamp(opacity, 0.35f, 1.0f);
    int pushed = 0;
    const auto push = [&pushed](ImGuiCol index, const ImVec4& value) {
        ImGui::PushStyleColor(index, value);
        ++pushed;
    };
    // Only the large surfaces follow the opacity slider; text, controls and
    // borders stay opaque or the overlay becomes unreadable over bright scenes.
    const auto surface = [surface_opacity](const ImVec4& color, float alpha) {
        return ImVec4(color.x, color.y, color.z, std::clamp(alpha * surface_opacity, 0.30f, 1.0f));
    };
    const ImVec4& accent = Palette::accent;

    push(ImGuiCol_Text, Palette::text);
    push(ImGuiCol_TextDisabled, Palette::text_dim);
    push(ImGuiCol_WindowBg, surface(Palette::window, 0.99f));
    push(ImGuiCol_ChildBg, surface(Palette::child, 0.97f));
    push(ImGuiCol_PopupBg, surface(Palette::popup, 0.99f));
    push(ImGuiCol_MenuBarBg, surface(Palette::child, 0.99f));
    push(ImGuiCol_Border, Palette::border);
    push(ImGuiCol_BorderShadow, ImVec4(0.0f, 0.0f, 0.0f, 0.40f));
    push(ImGuiCol_TitleBg, surface(Palette::child, 0.99f));
    push(ImGuiCol_TitleBgActive, surface(Palette::window, 1.0f));
    push(ImGuiCol_TitleBgCollapsed, surface(Palette::child, 0.85f));
    // Inputs sit below the window surface, not above it: a well reads as
    // editable, a raised slab reads as a button.
    push(ImGuiCol_FrameBg, Palette::well);
    push(ImGuiCol_FrameBgHovered, ImVec4(0.125f, 0.137f, 0.161f, 1.0f));
    push(ImGuiCol_FrameBgActive, tint(Palette::well, accent, 0.22f, 1.0f));
    push(ImGuiCol_Header, with_alpha(accent, 0.32f));
    push(ImGuiCol_HeaderHovered, with_alpha(accent, 0.52f));
    push(ImGuiCol_HeaderActive, with_alpha(accent, 0.78f));
    push(ImGuiCol_Button, Palette::raised);
    push(ImGuiCol_ButtonHovered, tint(Palette::raised, accent, 0.34f, 1.0f));
    push(ImGuiCol_ButtonActive, tint(Palette::raised, accent, 0.62f, 1.0f));
    push(ImGuiCol_Separator, Palette::line);
    push(ImGuiCol_SeparatorHovered, with_alpha(accent, 0.72f));
    push(ImGuiCol_SeparatorActive, accent);
    push(ImGuiCol_ResizeGrip, with_alpha(accent, 0.24f));
    push(ImGuiCol_ResizeGripHovered, with_alpha(accent, 0.60f));
    push(ImGuiCol_ResizeGripActive, accent);
    push(ImGuiCol_TableRowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    push(ImGuiCol_TableRowBgAlt, ImVec4(1.0f, 1.0f, 1.0f, 0.028f));
    push(ImGuiCol_TableHeaderBg, ImVec4(0.145f, 0.157f, 0.180f, 1.0f));
    push(ImGuiCol_TableBorderStrong, Palette::line);
    push(ImGuiCol_TableBorderLight, ImVec4(0.145f, 0.157f, 0.180f, 1.0f));
    push(ImGuiCol_CheckMark, ImVec4(0.42f, 0.74f, 1.0f, 1.0f));
    push(ImGuiCol_SliderGrab, with_alpha(accent, 0.85f));
    push(ImGuiCol_SliderGrabActive, accent);
    push(ImGuiCol_TextSelectedBg, with_alpha(accent, 0.42f));
    push(ImGuiCol_DragDropTarget, ImVec4(1.0f, 0.74f, 0.30f, 0.95f));
    push(ImGuiCol_NavCursor, with_alpha(accent, 0.85f));
    push(ImGuiCol_ScrollbarBg, ImVec4(0.043f, 0.047f, 0.055f, 0.90f));
    push(ImGuiCol_ScrollbarGrab, ImVec4(0.239f, 0.259f, 0.294f, 1.0f));
    push(ImGuiCol_ScrollbarGrabHovered, ImVec4(0.318f, 0.345f, 0.392f, 1.0f));
    push(ImGuiCol_ScrollbarGrabActive, accent);
    push(ImGuiCol_Tab, surface(Palette::child, 0.99f));
    push(ImGuiCol_TabHovered, tint(Palette::window, accent, 0.34f, 1.0f));
    push(ImGuiCol_TabSelected, tint(Palette::window, accent, 0.22f, 1.0f));
    push(ImGuiCol_TabSelectedOverline, accent);
    push(ImGuiCol_TabDimmed, surface(Palette::child, 0.97f));
    push(ImGuiCol_TabDimmedSelected, tint(Palette::window, accent, 0.12f, 1.0f));
    push(ImGuiCol_TabDimmedSelectedOverline, with_alpha(accent, 0.45f));
    push(ImGuiCol_DockingPreview, with_alpha(accent, 0.55f));
    push(ImGuiCol_DockingEmptyBg, surface(Palette::child, 0.60f));
    push(ImGuiCol_PlotLines, with_alpha(accent, 0.90f));
    push(ImGuiCol_PlotLinesHovered, ImVec4(1.0f, 0.74f, 0.30f, 1.0f));
    push(ImGuiCol_PlotHistogram, with_alpha(accent, 0.85f));
    push(ImGuiCol_PlotHistogramHovered, ImVec4(1.0f, 0.74f, 0.30f, 1.0f));

    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 3.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(7.0f, 4.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(7.0f, 5.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(6.0f, 4.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, 16.0f);
    return pushed;
}

// Each panel repaints its title bar, tabs, borders and highlight controls in its
// own accent. Previously the accent was mixed in at 15-24%, which on a near
// black surface is indistinguishable from no accent at all.
int push_panel_accent(const ImVec4& accent, float opacity) {
    const float surface_opacity = std::clamp(opacity, 0.35f, 1.0f);
    int pushed = 0;
    const auto push = [&pushed](ImGuiCol index, const ImVec4& value) {
        ImGui::PushStyleColor(index, value);
        ++pushed;
    };
    const auto surface = [surface_opacity](const ImVec4& color, float alpha) {
        return ImVec4(color.x, color.y, color.z, std::clamp(alpha * surface_opacity, 0.30f, 1.0f));
    };
    push(ImGuiCol_WindowBg, surface(tint(Palette::window, accent, 0.05f, 1.0f), 0.99f));
    push(ImGuiCol_ChildBg, surface(tint(Palette::child, accent, 0.04f, 1.0f), 0.97f));
    push(ImGuiCol_MenuBarBg, tint(Palette::child, accent, 0.20f, 1.0f));
    push(ImGuiCol_Border, tint(Palette::border, accent, 0.30f, 1.0f));
    push(ImGuiCol_Separator, tint(Palette::line, accent, 0.30f, 1.0f));
    push(ImGuiCol_SeparatorHovered, with_alpha(accent, 0.75f));
    push(ImGuiCol_SeparatorActive, accent);
    push(ImGuiCol_TitleBg, tint(Palette::child, accent, 0.24f, 0.99f));
    push(ImGuiCol_TitleBgActive, tint(Palette::child, accent, 0.58f, 1.0f));
    push(ImGuiCol_TitleBgCollapsed, tint(Palette::child, accent, 0.18f, 0.90f));
    push(ImGuiCol_Tab, tint(Palette::child, accent, 0.10f, 0.99f));
    push(ImGuiCol_TabHovered, tint(Palette::window, accent, 0.50f, 1.0f));
    push(ImGuiCol_TabSelected, tint(Palette::window, accent, 0.36f, 1.0f));
    push(ImGuiCol_TabSelectedOverline, accent);
    push(ImGuiCol_TabDimmed, tint(Palette::child, accent, 0.07f, 0.97f));
    push(ImGuiCol_TabDimmedSelected, tint(Palette::window, accent, 0.20f, 1.0f));
    push(ImGuiCol_TabDimmedSelectedOverline, with_alpha(accent, 0.45f));
    push(ImGuiCol_Header, with_alpha(accent, 0.28f));
    push(ImGuiCol_HeaderHovered, with_alpha(accent, 0.48f));
    push(ImGuiCol_HeaderActive, with_alpha(accent, 0.72f));
    push(ImGuiCol_CheckMark, tint(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), accent, 0.70f, 1.0f));
    push(ImGuiCol_SliderGrab, with_alpha(accent, 0.85f));
    push(ImGuiCol_SliderGrabActive, accent);
    push(ImGuiCol_ButtonHovered, tint(Palette::raised, accent, 0.40f, 1.0f));
    push(ImGuiCol_ButtonActive, tint(Palette::raised, accent, 0.70f, 1.0f));
    push(ImGuiCol_ResizeGrip, with_alpha(accent, 0.26f));
    push(ImGuiCol_ResizeGripHovered, with_alpha(accent, 0.62f));
    push(ImGuiCol_ResizeGripActive, accent);
    push(ImGuiCol_TextSelectedBg, with_alpha(accent, 0.40f));
    push(ImGuiCol_ScrollbarGrabActive, accent);
    push(ImGuiCol_NavCursor, with_alpha(accent, 0.85f));
    return pushed;
}

// Drawn across the top of the content area rather than the window frame: a
// docked panel has no title bar, and that is exactly when telling one panel
// from another is hardest.
void draw_panel_accent_bar(const ImVec4& accent) {
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    if (!draw_list)
        return;
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float width = std::max(1.0f, ImGui::GetContentRegionAvail().x);
    draw_list->AddRectFilled(start, ImVec2(start.x + width, start.y + 3.0f),
                             ImGui::GetColorU32(with_alpha(accent, 1.0f)), 1.5f);
    ImGui::Dummy(ImVec2(width, 5.0f));
}

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

    const int pushed_colors = push_explorer_theme(opacity);
    const ImGuiID dockspace_id = ImGui::GetID("URKExplorerDockSpace");
    // Rebuilding the layout would pull detached panels back to the main viewport.
    if (!dock_layout_initialized || !ImGui::DockBuilderGetNode(dockspace_id)) {
        if (ImGui::DockBuilderGetNode(dockspace_id))
            ImGui::DockBuilderRemoveNode(dockspace_id);
        ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockspace_id, work_size);
        ImGuiID workspace_dock = 0;
        ImGuiID content_dock = dockspace_id;
        ImGui::DockBuilderSplitNode(content_dock, ImGuiDir_Up, 0.075f, &workspace_dock, &content_dock);
        ImGuiID hierarchy_dock = 0;
        ImGui::DockBuilderSplitNode(content_dock, ImGuiDir_Left, 0.27f, &hierarchy_dock, &content_dock);
        ImGuiID inspector_dock = 0;
        ImGui::DockBuilderSplitNode(content_dock, ImGuiDir_Right, 0.43f, &inspector_dock, &content_dock);
        ImGuiID diagnostics_dock = 0;
        ImGui::DockBuilderSplitNode(content_dock, ImGuiDir_Down, 0.30f, &diagnostics_dock, &content_dock);
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
    // Every entry used to be an identically shaped coloured button in one row,
    // so "open a panel", "run an action" and "change a setting" were
    // indistinguishable. Menus separate them by kind.
    const ImVec4 workspace_accent(0.98f, 0.78f, 0.28f, 1.0f);
    const int workspace_colors = push_panel_accent(workspace_accent, opacity);
    if (ImGui::Begin("URK Explorer Workspace", nullptr,
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_MenuBar)) {
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
                ImGui::MenuItem("Activity Log", nullptr, &show_diagnostics);
                ImGui::Separator();
                if (ImGui::MenuItem("Runtime Monitor")) {
                    show_method_traces = true;
                    show_field_watches = true;
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Opens Method Calls and Value Watches together");
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Runtime")) {
                bool live_data = snapshot->live_data;
                if (ImGui::MenuItem("Live data", nullptr, &live_data)) {
                    Command command{.kind = CommandKind::SetLiveData};
                    command.bool_value = live_data;
                    RuntimeModel::instance().enqueue(std::move(command));
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Re-read inspected values from the running game every tick");
                ImGui::BeginDisabled(!snapshot->camera_focus_active);
                if (ImGui::MenuItem("Return camera"))
                    enqueue_simple(CommandKind::RestoreCamera, 0);
                ImGui::EndDisabled();
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Settings")) {
                ImGui::SeparatorText("Overlay");
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
                if (ImGui::MenuItem("Diagnostics"))
                    show_diagnostics = true;
                ImGui::Separator();
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
            // Refresh is the one action that gets used constantly, so it stays a
            // one-click button on the bar instead of living inside a menu.
            ImGui::Separator();
            if (workspace_button("Refresh", ImVec4(0.22f, 0.39f, 0.54f, 1.0f)))
                enqueue_simple(CommandKind::Refresh, 0);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Re-read the scene hierarchy from the running game");
            ImGui::EndMenuBar();
        }
        ImGui::TextColored(ImVec4(0.91f, 0.92f, 0.94f, 1.0f), "%s", ModConfig::display_name);
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.42f, 0.69f, 0.91f, 1.0f), "%s", ModConfig::backend_name);
        ImGui::SameLine();
        ImGui::TextDisabled("Runtime Explorer  |  %s  |  v%s", ModConfig::author, ModConfig::version);
        if (!snapshot->live_data) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.80f, 0.71f, 0.45f, 1.0f), "|  live data off");
        }
        if (snapshot->camera_focus_active) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Return camera"))
                enqueue_simple(CommandKind::RestoreCamera, 0);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Restore the camera pose saved before focusing");
        }
        // The result of the last command used to be visible only in the Activity
        // Log, so a rejected trace or write looked like the button did nothing.
        ImGui::TextDisabled("%s", snapshot->status.empty() ? "Ready" : snapshot->status.c_str());
        if (ImGui::IsItemHovered() && !snapshot->status.empty())
            ImGui::SetTooltip("%s", snapshot->status.c_str());
    }
    ImGui::End();
    ImGui::PopStyleColor(workspace_colors);

    if (show_hierarchy) {
        ImGui::SetNextWindowPos(ImVec2(work_pos.x + 12.0f, work_pos.y + 120.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(std::max(300.0f, work_size.x * 0.26f), std::max(420.0f, work_size.y * 0.72f)),
                                 ImGuiCond_FirstUseEver);
        const ImVec4 accent(0.16f, 0.76f, 0.62f, 1.0f);
        const int panel_colors = push_panel_accent(accent, opacity);
        if (ImGui::Begin("Hierarchy##urk-hierarchy", &show_hierarchy, ImGuiWindowFlags_NoCollapse)) {
            draw_panel_accent_bar(accent);
            render_hierarchy(*hierarchy, snapshot->selected_instance_id, snapshot->transform_clipboard);
        }
        ImGui::End();
        ImGui::PopStyleColor(panel_colors);
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
        const ImVec4 accent(0.29f, 0.60f, 0.98f, 1.0f);
        const int panel_colors = push_panel_accent(accent, opacity);
        if (ImGui::Begin(title.c_str(), &show_inspector, ImGuiWindowFlags_NoCollapse)) {
            draw_panel_accent_bar(accent);
            render_inspector(*snapshot);
            inspector_window_size = ImGui::GetWindowSize();
        }
        ImGui::End();
        ImGui::PopStyleColor(panel_colors);
    }

    if (show_object_inspector) {
        ImGui::SetNextWindowPos(ImVec2(work_pos.x + work_size.x * 0.28f, work_pos.y + 130.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(std::max(500.0f, work_size.x * 0.48f), std::max(420.0f, work_size.y * 0.68f)),
                                 ImGuiCond_FirstUseEver);
        const std::string title = snapshot->object_inspector.valid
                                      ? "Object Inspector - " + snapshot->object_inspector.type_name + "###urk-object"
                                      : "Object Inspector###urk-object";
        const ImVec4 accent(0.55f, 0.80f, 0.30f, 1.0f);
        const int panel_colors = push_panel_accent(accent, opacity);
        if (ImGui::Begin(title.c_str(), &show_object_inspector, ImGuiWindowFlags_NoCollapse)) {
            draw_panel_accent_bar(accent);
            render_object_inspector(*snapshot);
        }
        ImGui::End();
        ImGui::PopStyleColor(panel_colors);
    }

    if (show_class_browser) {
        ImGui::SetNextWindowPos(ImVec2(work_pos.x + work_size.x * 0.20f, work_pos.y + 150.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(std::max(520.0f, work_size.x * 0.42f), std::max(520.0f, work_size.y * 0.74f)),
                                 ImGuiCond_FirstUseEver);
        const ImVec4 accent(0.68f, 0.47f, 0.98f, 1.0f);
        const int panel_colors = push_panel_accent(accent, opacity);
        if (ImGui::Begin("Class Browser###urk-class-browser", &show_class_browser, ImGuiWindowFlags_NoCollapse)) {
            draw_panel_accent_bar(accent);
            render_class_browser(*snapshot);
        }
        ImGui::End();
        ImGui::PopStyleColor(panel_colors);
    }

    if (show_method_traces) {
        ImGui::SetNextWindowPos(ImVec2(work_pos.x + work_size.x * 0.18f, work_pos.y + 145.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(std::max(680.0f, work_size.x * 0.58f), std::max(420.0f, work_size.y * 0.62f)),
                                 ImGuiCond_FirstUseEver);
        const std::string title = "Runtime Monitor - Calls (" + std::to_string(snapshot->method_traces.size()) + ")###urk-method-traces";
        const ImVec4 accent(0.98f, 0.62f, 0.20f, 1.0f);
        const int panel_colors = push_panel_accent(accent, opacity);
        if (ImGui::Begin(title.c_str(), &show_method_traces, ImGuiWindowFlags_NoCollapse)) {
            draw_panel_accent_bar(accent);
            render_method_traces(*snapshot);
        }
        ImGui::End();
        ImGui::PopStyleColor(panel_colors);
    }

    if (show_field_watches) {
        ImGui::SetNextWindowPos(ImVec2(work_pos.x + work_size.x * 0.20f, work_pos.y + 170.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(std::max(650.0f, work_size.x * 0.54f), std::max(360.0f, work_size.y * 0.54f)),
                                 ImGuiCond_FirstUseEver);
        const std::string title =
            "Runtime Monitor - Values (" + std::to_string(snapshot->field_watches.size()) + ")###urk-field-watches";
        const ImVec4 accent(0.96f, 0.40f, 0.64f, 1.0f);
        const int panel_colors = push_panel_accent(accent, opacity);
        if (ImGui::Begin(title.c_str(), &show_field_watches, ImGuiWindowFlags_NoCollapse)) {
            draw_panel_accent_bar(accent);
            render_field_watches(*snapshot);
        }
        ImGui::End();
        ImGui::PopStyleColor(panel_colors);
    }

    if (show_reference_graph) {
        ImGui::SetNextWindowPos(ImVec2(work_pos.x + work_size.x * 0.18f, work_pos.y + 145.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(std::max(760.0f, work_size.x * 0.62f),
                                       std::max(480.0f, work_size.y * 0.68f)), ImGuiCond_FirstUseEver);
        const int panel_colors = push_panel_accent(ImVec4(0.24f, 0.74f, 0.94f, 1.0f), opacity);
        if (ImGui::Begin("Reference Graph###urk-reference-graph", &show_reference_graph, ImGuiWindowFlags_NoCollapse)) {
            draw_panel_accent_bar(ImVec4(0.24f, 0.74f, 0.94f, 1.0f));
            render_reference_graph(*snapshot);
        }
        ImGui::End();
        ImGui::PopStyleColor(panel_colors);
    }

    if (show_diagnostics) {
        ImGui::SetNextWindowPos(ImVec2(work_pos.x + 30.0f, work_pos.y + work_size.y - 260.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(std::min(760.0f, work_size.x - 60.0f), 230.0f), ImGuiCond_FirstUseEver);
        const std::string title =
            "Activity Log (" + std::to_string(snapshot->diagnostics.size()) + ")###urk-diagnostics";
        const ImVec4 accent(0.96f, 0.40f, 0.34f, 1.0f);
        const int panel_colors = push_panel_accent(accent, opacity);
        if (ImGui::Begin(title.c_str(), &show_diagnostics)) {
            draw_panel_accent_bar(accent);
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
        ImGui::PopStyleColor(panel_colors);
    }

    ImGui::PopStyleVar(11);
    ImGui::PopStyleColor(pushed_colors);
}

} // namespace Explorer::UI

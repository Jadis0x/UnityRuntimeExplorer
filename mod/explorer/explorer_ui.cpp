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

int push_explorer_theme(float opacity) {
    const float surface_opacity = std::clamp(opacity, 0.35f, 1.0f);
    const auto color = [](float r, float g, float b, float alpha) { return ImVec4(r, g, b, alpha); };
    const auto surface = [surface_opacity](float r, float g, float b, float alpha) {
        return ImVec4(r, g, b, std::clamp(alpha * surface_opacity, 0.28f, 1.0f));
    };

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.82f, 0.82f, 0.82f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, surface(0.176f, 0.176f, 0.176f, 0.99f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, surface(0.157f, 0.157f, 0.157f, 0.97f));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, surface(0.205f, 0.205f, 0.205f, 0.99f));
    ImGui::PushStyleColor(ImGuiCol_Border, color(0.105f, 0.105f, 0.105f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_BorderShadow, color(0.0f, 0.0f, 0.0f, 0.42f));
    ImGui::PushStyleColor(ImGuiCol_TitleBg, surface(0.145f, 0.145f, 0.145f, 0.99f));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, surface(0.205f, 0.205f, 0.205f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, color(0.125f, 0.125f, 0.125f, 0.96f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, color(0.235f, 0.235f, 0.235f, 0.99f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, color(0.270f, 0.270f, 0.270f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Header, color(0.235f, 0.235f, 0.235f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, color(0.255f, 0.350f, 0.445f, 0.98f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, color(0.225f, 0.445f, 0.690f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Button, color(0.235f, 0.235f, 0.235f, 0.98f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, color(0.315f, 0.315f, 0.315f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, color(0.225f, 0.445f, 0.690f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Separator, color(0.105f, 0.105f, 0.105f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_SeparatorHovered, color(0.32f, 0.49f, 0.62f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_SeparatorActive, color(0.34f, 0.57f, 0.74f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_TableRowBg, color(0.170f, 0.170f, 0.170f, 0.88f));
    ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, color(0.195f, 0.195f, 0.195f, 0.90f));
    ImGui::PushStyleColor(ImGuiCol_TableHeaderBg, color(0.235f, 0.235f, 0.235f, 0.98f));
    ImGui::PushStyleColor(ImGuiCol_TextDisabled, ImVec4(0.57f, 0.59f, 0.62f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(0.35f, 0.67f, 0.90f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_TextSelectedBg, color(0.225f, 0.445f, 0.690f, 0.92f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, color(0.09f, 0.095f, 0.10f, 0.90f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, color(0.28f, 0.29f, 0.31f, 0.98f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, color(0.36f, 0.38f, 0.40f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive, color(0.29f, 0.49f, 0.66f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Tab, color(0.145f, 0.145f, 0.145f, 0.99f));
    ImGui::PushStyleColor(ImGuiCol_TabHovered, color(0.260f, 0.310f, 0.360f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_TabSelected, color(0.235f, 0.235f, 0.235f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_TabDimmed, color(0.125f, 0.125f, 0.125f, 0.97f));
    ImGui::PushStyleColor(ImGuiCol_TabDimmedSelected, color(0.190f, 0.190f, 0.190f, 0.99f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(9.0f, 7.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 3.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 4.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(5.0f, 3.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, 16.0f);
    return 35;
}

int push_panel_accent(const ImVec4& accent, float opacity) {
    const float surface_opacity = std::clamp(opacity, 0.35f, 1.0f);
    const auto neutral = [surface_opacity](float base, float alpha) {
        return ImVec4(base, base + 0.004f, base + 0.009f,
                      std::clamp(alpha * surface_opacity, 0.25f, 1.0f));
    };
    ImGui::PushStyleColor(ImGuiCol_WindowBg, neutral(0.176f, 0.99f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, neutral(0.157f, 0.97f));
    ImGui::PushStyleColor(ImGuiCol_Border, neutral(0.105f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_Separator, neutral(0.105f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_TitleBg,
                          ImVec4(0.105f + accent.x * 0.15f, 0.105f + accent.y * 0.15f,
                                 0.105f + accent.z * 0.15f, 0.99f));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive,
                          ImVec4(0.125f + accent.x * 0.24f, 0.125f + accent.y * 0.24f,
                                 0.125f + accent.z * 0.24f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_TabSelected,
                          ImVec4(0.145f + accent.x * 0.20f, 0.145f + accent.y * 0.20f,
                                 0.145f + accent.z * 0.20f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_TabHovered,
                          ImVec4(0.155f + accent.x * 0.17f, 0.155f + accent.y * 0.17f,
                                 0.155f + accent.z * 0.17f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Text,
                          ImVec4(0.74f + accent.x * 0.14f, 0.74f + accent.y * 0.14f,
                                 0.74f + accent.z * 0.14f, 1.0f));
    return 9;
}

void draw_panel_accent_bar(const ImVec4& accent) {
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    if (!draw_list)
        return;
    const ImVec2 position = ImGui::GetWindowPos();
    const ImVec2 size = ImGui::GetWindowSize();
    draw_list->AddLine(position, ImVec2(position.x + size.x, position.y),
                       ImGui::GetColorU32(ImVec4(accent.x, accent.y, accent.z, 0.95f)), 2.0f);
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

const ImGuiPlatformMonitor* find_secondary_monitor(const ImGuiViewport* main_viewport) {
    if (!main_viewport)
        return nullptr;

    const ImVec2 main_center(
        main_viewport->WorkPos.x + main_viewport->WorkSize.x * 0.5f,
        main_viewport->WorkPos.y + main_viewport->WorkSize.y * 0.5f);
    const ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
    for (const ImGuiPlatformMonitor& monitor : platform_io.Monitors) {
        const ImRect work_area(
            monitor.WorkPos,
            ImVec2(
                monitor.WorkPos.x + monitor.WorkSize.x,
                monitor.WorkPos.y + monitor.WorkSize.y));
        if (!work_area.Contains(main_center))
            return &monitor;
    }
    return nullptr;
}

void render_secondary_workspace(const ImGuiViewport* main_viewport) {
    const ImGuiIO& io = ImGui::GetIO();
    if ((io.ConfigFlags & (ImGuiConfigFlags_DockingEnable |
                           ImGuiConfigFlags_ViewportsEnable)) !=
        (ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_ViewportsEnable))
        return;

    const ImGuiPlatformMonitor* monitor = find_secondary_monitor(main_viewport);
    if (!monitor || monitor->WorkSize.x <= 1.0f || monitor->WorkSize.y <= 1.0f)
        return;

    // Use a stationary host as the secondary monitor's docking target.
    ImGuiWindowClass workspace_class{};
    workspace_class.ViewportFlagsOverrideSet =
        ImGuiViewportFlags_NoFocusOnAppearing | ImGuiViewportFlags_NoTaskBarIcon;
    ImGui::SetNextWindowClass(&workspace_class);
    ImGui::SetNextWindowPos(monitor->WorkPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(monitor->WorkSize, ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBackground |
        ImGuiWindowFlags_NoSavedSettings;
    ImGui::Begin("URK Explorer Secondary Workspace##urk-secondary-workspace", nullptr, flags);
    ImGui::PopStyleVar(3);
    ImGui::DockSpace(
        ImGui::GetID("URKExplorerSecondaryDockSpace"),
        ImVec2(0.0f, 0.0f),
        ImGuiDockNodeFlags_PassthruCentralNode);
    ImGui::End();
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
    render_secondary_workspace(viewport);

    ImGui::SetNextWindowPos(ImVec2(work_pos.x + 12.0f, work_pos.y + 12.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(std::min(980.0f, work_size.x - 24.0f), 74.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("URK Explorer Workspace", nullptr, ImGuiWindowFlags_NoCollapse)) {
        ImGui::TextColored(ImVec4(0.91f, 0.92f, 0.94f, 1.0f), "%s", ModConfig::display_name);
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.42f, 0.69f, 0.91f, 1.0f), "%s", ModConfig::backend_name);
        ImGui::SameLine();
        ImGui::TextDisabled("Runtime Explorer  |  %s  |  v%s", ModConfig::author, ModConfig::version);
        ImGui::Separator();
        if (workspace_button("Panels", ImVec4(0.27f, 0.29f, 0.32f, 1.0f)))
            ImGui::OpenPopup("##workspace-panels");
        if (ImGui::BeginPopup("##workspace-panels")) {
            ImGui::TextDisabled("Visible panels");
            ImGui::Checkbox("Hierarchy", &show_hierarchy);
            ImGui::Checkbox("Inspector", &show_inspector);
            ImGui::Checkbox("Object Inspector", &show_object_inspector);
            ImGui::Checkbox("Class Browser", &show_class_browser);
            ImGui::Checkbox("Method Calls", &show_method_traces);
            ImGui::Checkbox("Value Watches", &show_field_watches);
            ImGui::Checkbox("Reference Graph", &show_reference_graph);
            ImGui::Checkbox("Activity log", &show_diagnostics);
            ImGui::EndPopup();
        }
        ImGui::SameLine();
        if (workspace_button("Refresh", ImVec4(0.22f, 0.39f, 0.54f, 1.0f)))
            enqueue_simple(CommandKind::Refresh, 0);
        ImGui::SameLine();
        if (workspace_button("Runtime Monitor", ImVec4(0.47f, 0.34f, 0.20f, 1.0f))) {
            show_method_traces = true;
            show_field_watches = true;
        }
        ImGui::SameLine();
        if (workspace_button("Reference Graph", ImVec4(0.20f, 0.40f, 0.48f, 1.0f)))
            show_reference_graph = true;
        ImGui::SameLine();
        if (workspace_button("Diagnostics", ImVec4(0.47f, 0.27f, 0.24f, 1.0f)))
            show_diagnostics = true;
        ImGui::SameLine();
        if (workspace_button("Options", ImVec4(0.29f, 0.29f, 0.31f, 1.0f)))
            ImGui::OpenPopup("##workspace-options");
        ImGui::SameLine();
        workspace_link_button("GitHub", ModConfig::url, ImVec4(0.24f, 0.34f, 0.44f, 1.0f));
        ImGui::SameLine();
        workspace_link_button("Support", ModConfig::social, ImVec4(0.88f, 0.66f, 0.13f, 1.0f),
                              ImVec4(0.10f, 0.085f, 0.045f, 1.0f));
        if (ImGui::BeginPopup("##workspace-options")) {
            ImGui::SeparatorText("Overlay");
            bool live_data = snapshot->live_data;
            if (ImGui::Checkbox("Live Data", &live_data)) {
                Command command{.kind = CommandKind::SetLiveData};
                command.bool_value = live_data;
                RuntimeModel::instance().enqueue(std::move(command));
            }
            ImGui::SetNextItemWidth(180.0f);
            ImGui::SliderFloat("Panel background", &opacity, 0.35f, 1.0f, "%.2f");
            ImGui::TextDisabled("Text, controls and borders remain opaque for readability.");
            const bool multi_monitor =
                (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0;
            ImGui::TextDisabled("Multi-monitor windows: %s", multi_monitor ? "enabled" : "unavailable on this renderer");
            if (multi_monitor)
                ImGui::TextWrapped("Drag an undocked panel or tab outside the game window to place it on your other monitor.");
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
            ImGui::EndPopup();
        }
        if (snapshot->camera_focus_active) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Return camera"))
                enqueue_simple(CommandKind::RestoreCamera, 0);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Restore the camera pose saved before focusing");
        }
    }
    ImGui::End();

    if (show_hierarchy) {
        ImGui::SetNextWindowPos(ImVec2(work_pos.x + 12.0f, work_pos.y + 120.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(std::max(300.0f, work_size.x * 0.26f), std::max(420.0f, work_size.y * 0.72f)),
                                 ImGuiCond_FirstUseEver);
        const ImVec4 accent(0.24f, 0.56f, 0.52f, 1.0f);
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
        const ImVec4 accent(0.32f, 0.50f, 0.74f, 1.0f);
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
        const ImVec4 accent(0.48f, 0.62f, 0.42f, 1.0f);
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
        const ImVec4 accent(0.56f, 0.45f, 0.73f, 1.0f);
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
        const ImVec4 accent(0.72f, 0.51f, 0.30f, 1.0f);
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
        const ImVec4 accent(0.63f, 0.42f, 0.54f, 1.0f);
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
        const int panel_colors = push_panel_accent(ImVec4(0.31f, 0.55f, 0.68f, 1.0f), opacity);
        if (ImGui::Begin("Reference Graph###urk-reference-graph", &show_reference_graph, ImGuiWindowFlags_NoCollapse)) {
            draw_panel_accent_bar(ImVec4(0.31f, 0.55f, 0.68f, 1.0f));
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
        const ImVec4 accent(0.68f, 0.40f, 0.34f, 1.0f);
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

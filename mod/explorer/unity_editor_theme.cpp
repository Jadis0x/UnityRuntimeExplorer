// Copyright (c) 2026 Jadis0x. All rights reserved.
// The Unity 6 dark editor skin and the chrome widgets built on it.
#include "unity_editor_theme.h"

#include "ui/theme.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>

namespace Explorer::UI::Unity {
namespace {

ImVec4 surface(const ImVec4 &color, float opacity) {
    return ImVec4(color.x, color.y, color.z, std::clamp(color.w * opacity, 0.30f, 1.0f));
}

ImVec4 with_alpha(const ImVec4 &color, float alpha) {
    return ImVec4(color.x, color.y, color.z, alpha);
}

ImVec4 mix(const ImVec4 &color, const ImVec4 &towards, float amount) {
    return ImVec4(color.x + (towards.x - color.x) * amount, color.y + (towards.y - color.y) * amount,
                  color.z + (towards.z - color.z) * amount, color.w);
}

} // namespace

float &ui_scale() {
    static float value = 1.0f;
    return value;
}

float font_size() {
    // 15px is Unity's 11pt editor chrome at 100% display scaling. It tracks the
    // display's DPI the way the rest of the mod does, and the user's scale on
    // top of that, because one fixed size cannot serve a 1080p and a 4K panel.
    const float scaled =
        std::floor((15.0f * ModUI::Theme::dpi_scale() * std::clamp(ui_scale(), 0.75f, 2.0f)) + 0.5f);
    return std::clamp(scaled, 11.0f, 40.0f);
}

float row_height() {
    return font_size() + 6.0f;
}

float toolbar_height() {
    // Unity stacks a menu strip over a toolbar strip; both are one row tall.
    return row_height() * 2.0f + 14.0f;
}

int push_style(float opacity) {
    const float panel_opacity = std::clamp(opacity, 0.35f, 1.0f);
    int pushed = 0;
    const auto push = [&pushed](ImGuiCol index, const ImVec4 &value) {
        ImGui::PushStyleColor(index, value);
        ++pushed;
    };

    push(ImGuiCol_Text, Skin::text);
    push(ImGuiCol_TextDisabled, Skin::text_dim);
    push(ImGuiCol_WindowBg, surface(Skin::window, panel_opacity));
    push(ImGuiCol_ChildBg, surface(Skin::window, panel_opacity));
    push(ImGuiCol_PopupBg, surface(Skin::popup, std::max(panel_opacity, 0.96f)));
    push(ImGuiCol_MenuBarBg, surface(Skin::toolbar, panel_opacity));
    push(ImGuiCol_Border, Skin::rule);
    push(ImGuiCol_BorderShadow, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));

    push(ImGuiCol_TitleBg, surface(Skin::tab_bar, panel_opacity));
    push(ImGuiCol_TitleBgActive, surface(Skin::toolbar, panel_opacity));
    push(ImGuiCol_TitleBgCollapsed, surface(Skin::tab_bar, panel_opacity));

    // Editor fields are wells sunk below the panel with a darker rule around
    // them - never raised slabs, which in Unity read as buttons.
    push(ImGuiCol_FrameBg, Skin::field);
    push(ImGuiCol_FrameBgHovered, Skin::field_hover);
    push(ImGuiCol_FrameBgActive, Skin::field_hover);

    push(ImGuiCol_Button, Skin::button);
    push(ImGuiCol_ButtonHovered, Skin::button_hover);
    push(ImGuiCol_ButtonActive, Skin::button_active);

    // Rows, foldouts and list selection all share the editor's #2C5D87.
    push(ImGuiCol_Header, Skin::selection);
    push(ImGuiCol_HeaderHovered, Skin::selection_hover);
    push(ImGuiCol_HeaderActive, Skin::selection);

    push(ImGuiCol_Separator, Skin::rule);
    push(ImGuiCol_SeparatorHovered, Skin::selection_hover);
    push(ImGuiCol_SeparatorActive, Skin::selection);

    push(ImGuiCol_ResizeGrip, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    push(ImGuiCol_ResizeGripHovered, with_alpha(Skin::selection, 0.55f));
    push(ImGuiCol_ResizeGripActive, Skin::selection);

    push(ImGuiCol_ScrollbarBg, surface(Skin::window, panel_opacity));
    push(ImGuiCol_ScrollbarGrab, Skin::scroll_grab);
    push(ImGuiCol_ScrollbarGrabHovered, Skin::scroll_grab_hover);
    push(ImGuiCol_ScrollbarGrabActive, Skin::scroll_grab_hover);

    push(ImGuiCol_CheckMark, Skin::text);
    push(ImGuiCol_SliderGrab, Skin::button_hover);
    push(ImGuiCol_SliderGrabActive, Skin::selection);

    // Docked panel tabs: the active one is the panel body continued upward, the
    // rest sit on the darker strip behind them.
    push(ImGuiCol_Tab, surface(Skin::tab_bar, panel_opacity));
    push(ImGuiCol_TabHovered, surface(Skin::header_hover, panel_opacity));
    push(ImGuiCol_TabSelected, surface(Skin::window, panel_opacity));
    push(ImGuiCol_TabSelectedOverline, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    push(ImGuiCol_TabDimmed, surface(Skin::tab_bar, panel_opacity));
    push(ImGuiCol_TabDimmedSelected, surface(Skin::window, panel_opacity));
    push(ImGuiCol_TabDimmedSelectedOverline, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));

    push(ImGuiCol_TableHeaderBg, surface(Skin::toolbar, panel_opacity));
    push(ImGuiCol_TableBorderStrong, Skin::rule);
    push(ImGuiCol_TableBorderLight, Skin::rule);
    push(ImGuiCol_TableRowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    push(ImGuiCol_TableRowBgAlt, ImVec4(1.0f, 1.0f, 1.0f, 0.022f));

    push(ImGuiCol_TextSelectedBg, with_alpha(Skin::selection, 0.75f));
    push(ImGuiCol_DragDropTarget, Skin::accent);
    push(ImGuiCol_NavCursor, with_alpha(Skin::selection, 0.85f));
    push(ImGuiCol_DockingPreview, with_alpha(Skin::selection, 0.65f));
    push(ImGuiCol_DockingEmptyBg, surface(Skin::tab_bar, panel_opacity));
    push(ImGuiCol_PlotLines, Skin::accent);
    push(ImGuiCol_PlotLinesHovered, Skin::warning);
    push(ImGuiCol_PlotHistogram, Skin::accent);
    push(ImGuiCol_PlotHistogramHovered, Skin::warning);
    push(ImGuiCol_ModalWindowDimBg, ImVec4(0.0f, 0.0f, 0.0f, 0.45f));

    // Unity's editor has no rounded corners anywhere, and its rows are tight:
    // an inspector line is one text height plus three pixels of padding.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_TabRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_TabBarBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.0f, 4.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 3.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 3.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(4.0f, 3.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(4.0f, 2.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, 14.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 13.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, 10.0f);
    return pushed;
}

void pop_style(int pushed_colors) {
    ImGui::PopStyleVar(kStyleVarCount);
    ImGui::PopStyleColor(pushed_colors);
}

void rule_line(float top_padding, float bottom_padding) {
    ImDrawList *draw_list = ImGui::GetWindowDrawList();
    if (!draw_list)
        return;
    if (top_padding > 0.0f)
        ImGui::Dummy(ImVec2(1.0f, top_padding));
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float width = std::max(1.0f, ImGui::GetContentRegionAvail().x);
    draw_list->AddLine(start, ImVec2(start.x + width, start.y), ImGui::GetColorU32(Skin::rule), 1.0f);
    ImGui::Dummy(ImVec2(width, 1.0f + bottom_padding));
}

namespace {

// Hover lifts the control towards white and pressing it sinks towards black, so
// one accent colour is enough to describe all three states.
bool toolbar_control(const char *label, const ImVec4 &background, const char *tooltip) {
    static constexpr ImVec4 kWhite{1.0f, 1.0f, 1.0f, 1.0f};
    static constexpr ImVec4 kBlack{0.0f, 0.0f, 0.0f, 1.0f};
    const ImGuiStyle &style = ImGui::GetStyle();
    const ImVec2 size(ImGui::CalcTextSize(label).x + style.FramePadding.x * 4.0f, row_height());
    ImGui::PushStyleColor(ImGuiCol_Button, background);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, mix(background, kWhite, 0.16f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, mix(background, kBlack, 0.22f));
    const bool pressed = ImGui::Button(label, size);
    ImGui::PopStyleColor(3);
    if (tooltip && ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", tooltip);
    return pressed;
}

} // namespace

bool toolbar_button(const char *label, const char *tooltip) {
    return toolbar_control(label, Skin::button, tooltip);
}

bool toolbar_button(const char *label, const ImVec4 &accent, const char *tooltip) {
    return toolbar_control(label, accent, tooltip);
}

bool toolbar_toggle(const char *label, bool active, const char *tooltip) {
    return toolbar_control(label, active ? Skin::selection : Skin::button, tooltip);
}

bool toolbar_toggle(const char *label, bool active, const ImVec4 &accent, const char *tooltip) {
    return toolbar_control(label, active ? accent : Skin::button, tooltip);
}

void toolbar_separator() {
    const float height = row_height();
    ImGui::SameLine(0.0f, 6.0f);
    ImDrawList *draw_list = ImGui::GetWindowDrawList();
    const ImVec2 start = ImGui::GetCursorScreenPos();
    if (draw_list)
        draw_list->AddLine(ImVec2(start.x, start.y + 2.0f), ImVec2(start.x, start.y + height - 2.0f),
                           ImGui::GetColorU32(Skin::rule), 1.0f);
    ImGui::Dummy(ImVec2(1.0f, height));
    ImGui::SameLine(0.0f, 6.0f);
}

bool component_header(const char *str_id, const char *label, bool *enabled, bool *enabled_changed,
                      const char *right_text) {
    ImGuiContext &context = *ImGui::GetCurrentContext();
    const ImGuiStyle &style = ImGui::GetStyle();
    ImDrawList *draw_list = ImGui::GetWindowDrawList();
    const float height = ImGui::GetFrameHeight();
    const ImVec2 band_pos = ImGui::GetCursorScreenPos();
    const float band_width = std::max(1.0f, ImGui::GetContentRegionAvail().x);
    const ImVec2 band_max(band_pos.x + band_width, band_pos.y + height);
    const bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) &&
                         ImGui::IsMouseHoveringRect(band_pos, band_max);
    const ImVec4 band = hovered ? Skin::header_hover : Skin::header;

    // The strip is painted first so it also covers the checkbox Unity places
    // inside the header, which a framed tree node would leave outside of it.
    if (draw_list) {
        draw_list->AddRectFilled(band_pos, band_max, ImGui::GetColorU32(band));
        draw_list->AddLine(ImVec2(band_pos.x, band_max.y - 1.0f), ImVec2(band_max.x, band_max.y - 1.0f),
                           ImGui::GetColorU32(Skin::rule), 1.0f);
    }

    ImGui::PushStyleColor(ImGuiCol_Header, band);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, Skin::header_hover);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, Skin::header_hover);
    const bool open = ImGui::TreeNodeEx(str_id,
                                        ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_FramePadding |
                                            ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowOverlap,
                                        "%s", "");
    ImGui::PopStyleColor(3);
    // Nothing after this point may become the "last item", or the caller's
    // context menu and tooltip would attach to the checkbox instead of the band.
    const ImGuiLastItemData header_item = context.LastItemData;

    float text_x = band_pos.x + ImGui::GetFontSize() + style.FramePadding.x * 3.0f;
    if (enabled) {
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::SetCursorScreenPos(ImVec2(text_x, band_pos.y));
        if (ImGui::Checkbox("##urk-component-enabled", enabled) && enabled_changed)
            *enabled_changed = true;
        text_x = ImGui::GetItemRectMax().x + style.ItemInnerSpacing.x;
        context.LastItemData = header_item;
    }

    if (draw_list) {
        const float baseline = band_pos.y + (height - ImGui::GetTextLineHeight()) * 0.5f;
        draw_list->AddText(ImVec2(text_x, baseline), ImGui::GetColorU32(Skin::text), label);
        if (right_text && *right_text) {
            const float suffix_width = ImGui::CalcTextSize(right_text).x;
            const float right_x = band_max.x - suffix_width - style.FramePadding.x * 2.0f;
            if (right_x > text_x + ImGui::CalcTextSize(label).x + 12.0f)
                draw_list->AddText(ImVec2(right_x, baseline), ImGui::GetColorU32(Skin::text_dim), right_text);
        }
    }

    ImGui::SetCursorScreenPos(ImVec2(band_pos.x, band_max.y + style.ItemSpacing.y));
    context.LastItemData = header_item;
    return open;
}

bool begin_member_tab(const char *label, const ImVec4 &accent, ImGuiTabItemFlags flags) {
    static constexpr ImVec4 kWhite{1.0f, 1.0f, 1.0f, 1.0f};
    // The unselected tab only hints at its colour; the selected one wears it.
    ImGui::PushStyleColor(ImGuiCol_Tab, mix(Skin::tab_bar, accent, 0.30f));
    ImGui::PushStyleColor(ImGuiCol_TabHovered, mix(accent, kWhite, 0.16f));
    ImGui::PushStyleColor(ImGuiCol_TabSelected, accent);
    ImGui::PushStyleColor(ImGuiCol_TabDimmed, mix(Skin::tab_bar, accent, 0.18f));
    ImGui::PushStyleColor(ImGuiCol_TabDimmedSelected, mix(accent, Skin::window, 0.35f));
    const bool open = ImGui::BeginTabItem(label, nullptr, flags);
    ImGui::PopStyleColor(5);
    return open;
}

bool begin_property_rows(const char *str_id) {
    // Unity keeps roughly a 38% label gutter and gives the rest to the field.
    const float label_width = std::clamp(ImGui::GetContentRegionAvail().x * 0.38f, 64.0f, 220.0f);
    if (!ImGui::BeginTable(str_id, 2, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoSavedSettings))
        return false;
    ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, label_width);
    ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
    return true;
}

void end_property_rows() {
    ImGui::EndTable();
}

void property_row(const char *label, const char *tooltip) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    if (tooltip && ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", tooltip);
    ImGui::TableSetColumnIndex(1);
    ImGui::SetNextItemWidth(-1.0f);
}

bool vector3_row(const char *str_id, float value[3], float speed, const char *format) {
    static const char *const axis[3] = {"X", "Y", "Z"};
    const ImGuiStyle &style = ImGui::GetStyle();
    const float axis_width = ImGui::CalcTextSize("X").x;
    const float available = ImGui::GetContentRegionAvail().x;
    const float field_width =
        std::max(34.0f, (available - (axis_width + style.ItemInnerSpacing.x * 2.0f) * 3.0f) / 3.0f);

    bool changed = false;
    ImGui::PushID(str_id);
    for (int index = 0; index < 3; ++index) {
        if (index > 0)
            ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(axis[index]);
        ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
        ImGui::PushID(index);
        ImGui::SetNextItemWidth(field_width);
        changed = ImGui::DragFloat("##axis", &value[index], speed, 0.0f, 0.0f, format) || changed;
        ImGui::PopID();
    }
    ImGui::PopID();
    return changed;
}

} // namespace Explorer::UI::Unity

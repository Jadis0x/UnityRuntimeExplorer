// Copyright (c) 2026 Jadis0x. All rights reserved.
#pragma once
#include <cmath>
#include <cstdio>
#include <unordered_map>
#include <imgui.h>

#include "localization.h"
#include "theme.h"

namespace ModUI::Widgets {
    struct AnimState {
        bool initialized = false;
        float hover = 0.0f;
        float active = 0.0f;
        float checked = 0.0f;
        float value = 0.0f;
        float x = 0.0f;
        float y = 0.0f;
        float w = 0.0f;
        float h = 0.0f;
    };

    inline float clamp01(float value) {
        if (value < 0.0f) return 0.0f;
        if (value > 1.0f) return 1.0f;
        return value;
    }

    inline float ease_out_cubic(float value) {
        const float t = clamp01(value);
        const float inv = 1.0f - t;
        return 1.0f - (inv * inv * inv);
    }

    inline float animation_step(float speed) {
        float dt = ImGui::GetIO().DeltaTime;
        if (dt <= 0.0f) dt = 1.0f / 60.0f;
        if (dt > 1.0f / 15.0f) dt = 1.0f / 15.0f;
        return 1.0f - std::exp(-speed * dt);
    }

    inline float animate_to(float current, float target, float speed = 14.0f) {
        return current + (target - current) * animation_step(speed);
    }

    inline AnimState& anim_state(ImGuiID id) {
        static std::unordered_map<ImGuiID, AnimState> states;
        return states[id];
    }

    inline ImVec4 mix3(const ImVec4& base, const ImVec4& hover, const ImVec4& active,
        float hover_t, float active_t) {
        return Theme::mix(Theme::mix(base, hover, clamp01(hover_t)), active, clamp01(active_t));
    }

    inline float clamp_label_column_width(float available_width) {
        const float desired = Theme::spacing().label_column_width;
        const float max_width = available_width * 0.38f;
        if (max_width < 72.0f) return 72.0f;
        return desired < max_width ? desired : max_width;
    }

    inline bool& labeled_field_table_open() {
        static bool open = false;
        return open;
    }

    inline void begin_labeled_field(const char* label, const char* id = nullptr) {
        const Theme::Palette& p = Theme::palette();
        const float available_width = ImGui::GetContentRegionAvail().x;
        const float label_width = clamp_label_column_width(available_width);

        ImGui::PushID(id && id[0] ? id : (label ? label : "field"));
        bool& table_open = labeled_field_table_open();
        IM_ASSERT(!table_open && "begin_labeled_field() calls cannot be nested");
        table_open = ImGui::BeginTable("##field", 2,
            ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoPadOuterX);
        if (table_open) {
            ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, label_width);
            ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch, available_width - label_width);
            ImGui::TableNextColumn();
            ImGui::PushStyleColor(ImGuiCol_Text, p.text_secondary);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(label && label[0] ? label : "");
            ImGui::PopStyleColor();
            ImGui::TableNextColumn();
        }
    }

    inline void end_labeled_field() {
        bool& table_open = labeled_field_table_open();
        if (table_open) ImGui::EndTable();
        table_open = false;
        ImGui::PopID();
    }

    inline bool checkbox(const char* label, bool* value) {
        if (!value) return false;
        const Theme::Palette& p = Theme::palette();
        const char* text = label && label[0] ? label : "Option";
        bool changed = false;
        begin_labeled_field(text);
        ImGui::PushID(text);
        const ImGuiID id = ImGui::GetID("##checkbox");
        AnimState& anim = anim_state(id);
        if (!anim.initialized) {
            anim.checked = *value ? 1.0f : 0.0f;
            anim.initialized = true;
        }

        const float square = ImGui::GetFontSize() + 6.0f;
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##checkbox", ImVec2(square, square));
        if (ImGui::IsItemClicked()) {
            *value = !*value;
            changed = true;
        }
        const bool hovered = ImGui::IsItemHovered();
        const bool held = ImGui::IsItemActive();
        if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

        anim.hover = animate_to(anim.hover, hovered ? 1.0f : 0.0f, 16.0f);
        anim.active = animate_to(anim.active, held ? 1.0f : 0.0f, 22.0f);
        anim.checked = animate_to(anim.checked, *value ? 1.0f : 0.0f, 18.0f);

        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 end(pos.x + square, pos.y + square);
        const ImVec4 frame_col = mix3(p.surface, p.surface_hover, p.surface_active, anim.hover, anim.active);
        dl->AddRectFilled(pos, end, ImGui::GetColorU32(frame_col), Theme::radius().sm);

        const float checked_t = ease_out_cubic(anim.checked);
        if (checked_t > 0.001f) {
            const float inset = 3.0f + ((1.0f - checked_t) * 5.0f);
            dl->AddRectFilled(ImVec2(pos.x + inset, pos.y + inset), ImVec2(end.x - inset, end.y - inset),
                ImGui::GetColorU32(Theme::with_alpha(p.accent_a, 0.95f * checked_t)), Theme::radius().sm);

            const float mark_alpha = clamp01((checked_t - 0.28f) / 0.72f);
            const ImU32 mark_col = ImGui::GetColorU32(Theme::with_alpha(p.text_primary, mark_alpha));
            const float thickness = 2.0f;
            const ImVec2 a(pos.x + square * 0.28f, pos.y + square * 0.53f);
            const ImVec2 b(pos.x + square * 0.43f, pos.y + square * 0.68f);
            const ImVec2 c(pos.x + square * 0.73f, pos.y + square * 0.34f);
            dl->AddLine(a, b, mark_col, thickness);
            dl->AddLine(b, c, mark_col, thickness);
        }
        ImGui::PopID();
        ImGui::SameLine(0.0f, 10.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, p.text_muted);
        ImGui::TextUnformatted(ModUI::Localization::translate(
            *value ? "widget.enabled" : "widget.disabled"));
        ImGui::PopStyleColor();
        end_labeled_field();
        return changed;
    }

    inline bool button(const char* label, const ImVec2& size = ImVec2(0.0f, 0.0f), bool primary = false) {
        const char* text = label && label[0] ? label : "Button";
        const Theme::Palette& p = Theme::palette();

        ImGui::PushID(text);
        const ImVec2 label_size = ImGui::CalcTextSize(text);
        const ImVec2 padding(14.0f, 9.0f);
        ImVec2 resolved_size(
            size.x > 0.0f ? size.x : label_size.x + padding.x * 2.0f,
            size.y > 0.0f ? size.y : label_size.y + padding.y * 2.0f);
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const ImGuiID id = ImGui::GetID("##button");
        AnimState& anim = anim_state(id);
        if (!anim.initialized) anim.initialized = true;

        const bool pressed = ImGui::InvisibleButton("##button", resolved_size);
        const bool hovered = ImGui::IsItemHovered();
        const bool held = ImGui::IsItemActive();
        if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

        anim.hover = animate_to(anim.hover, hovered ? 1.0f : 0.0f, 16.0f);
        anim.active = animate_to(anim.active, held ? 1.0f : 0.0f, 24.0f);

        const ImVec4 base_col = primary ? p.accent_a : p.surface_raised;
        const ImVec4 hover_col = primary ? p.accent_b : p.surface_hover;
        const ImVec4 active_col = primary ? Theme::with_alpha(p.accent_b, 0.85f) : p.surface_active;
        const ImVec4 fill_col = mix3(base_col, hover_col, active_col, anim.hover, anim.active);
        const float press_offset = ease_out_cubic(anim.active) * 1.0f;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(ImVec2(pos.x, pos.y + press_offset),
            ImVec2(pos.x + resolved_size.x, pos.y + resolved_size.y + press_offset),
            ImGui::GetColorU32(fill_col), Theme::radius().md);
        if (!primary) {
            dl->AddRect(ImVec2(pos.x, pos.y + press_offset),
                ImVec2(pos.x + resolved_size.x, pos.y + resolved_size.y + press_offset),
                ImGui::GetColorU32(p.border_subtle), Theme::radius().md);
        }

        const ImVec4 text_col = primary ? p.bg_base : p.text_primary;
        dl->AddText(ImVec2(pos.x + (resolved_size.x - label_size.x) * 0.5f,
                pos.y + (resolved_size.y - label_size.y) * 0.5f + press_offset),
            ImGui::GetColorU32(text_col), text);
        ImGui::PopID();
        return pressed;
    }

    inline bool slider_float(const char* label, float* value, float min_value, float max_value,
        const char* format = "%.2f") {
        if (!value) return false;
        const char* text = label && label[0] ? label : "Value";
        const Theme::Palette& p = Theme::palette();
        begin_labeled_field(text);
        ImGui::PushID(text);
        const ImGuiID id = ImGui::GetID("##slider");
        AnimState& anim = anim_state(id);
        const float width = ImGui::GetContentRegionAvail().x;
        const float height = ImGui::GetFrameHeight();
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        bool changed = false;

        float normalized = 0.0f;
        if (max_value > min_value) normalized = clamp01((*value - min_value) / (max_value - min_value));
        if (!anim.initialized) {
            anim.value = normalized;
            anim.initialized = true;
        }

        ImGui::InvisibleButton("##slider", ImVec2(width, height));
        const bool hovered = ImGui::IsItemHovered();
        const bool held = ImGui::IsItemActive();
        if (hovered || held) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        if (held && width > 0.0f && max_value > min_value) {
            const float next_normalized = clamp01((ImGui::GetIO().MousePos.x - pos.x) / width);
            const float next_value = min_value + (max_value - min_value) * next_normalized;
            if (next_value != *value) {
                *value = next_value;
                normalized = next_normalized;
                changed = true;
            }
        }

        anim.hover = animate_to(anim.hover, hovered ? 1.0f : 0.0f, 16.0f);
        anim.active = animate_to(anim.active, held ? 1.0f : 0.0f, 22.0f);
        anim.value = animate_to(anim.value, normalized, held ? 28.0f : 14.0f);

        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec4 frame_col = mix3(p.surface_raised, p.surface_hover, p.surface_active, anim.hover, anim.active);
        const ImVec2 end(pos.x + width, pos.y + height);
        const float track_h = 6.0f;
        const ImVec2 track_min(pos.x, pos.y + (height - track_h) * 0.5f);
        const ImVec2 track_max(end.x, track_min.y + track_h);
        dl->AddRectFilled(track_min, track_max, ImGui::GetColorU32(frame_col), Theme::radius().pill);

        const float fill_w = width * clamp01(anim.value);
        if (fill_w > 0.5f) {
            dl->AddRectFilled(track_min, ImVec2(pos.x + fill_w, track_max.y),
                ImGui::GetColorU32(Theme::mix(p.accent_a, p.accent_b, anim.active)),
                Theme::radius().pill);
        }

        const float grab_radius = 6.0f + ease_out_cubic(anim.hover + anim.active) * 1.5f;
        const ImVec2 grab(pos.x + fill_w, pos.y + height * 0.5f);
        dl->AddCircleFilled(grab, grab_radius + 2.0f, ImGui::GetColorU32(p.bg_elevated));
        dl->AddCircleFilled(grab, grab_radius,
            ImGui::GetColorU32(Theme::mix(p.accent_a, p.accent_b, anim.active)));

        char value_text[64]{};
        std::snprintf(value_text, sizeof(value_text), format && format[0] ? format : "%.2f", *value);
        const ImVec2 value_size = ImGui::CalcTextSize(value_text);
        dl->AddText(ImVec2(end.x - value_size.x - 10.0f, pos.y + (height - value_size.y) * 0.5f),
            ImGui::GetColorU32(p.text_primary), value_text);
        ImGui::PopID();
        end_labeled_field();
        return changed;
    }

    inline bool combo(const char* label, int* current, const char* const items[], int item_count,
        const char* id = nullptr) {
        if (!current || !items || item_count <= 0) return false;
        const char* text = label && label[0] ? label : "Mode";
        const Theme::Palette& p = Theme::palette();
        if (*current < 0) *current = 0;
        if (*current >= item_count) *current = item_count - 1;
        const char* preview = items[*current] && items[*current][0] ? items[*current] : " ";
        begin_labeled_field(text, id);
        ImGui::PushID("##combo");
        const ImGuiID field_id = ImGui::GetID("##combo_field");
        AnimState& anim = anim_state(field_id);
        if (!anim.initialized) anim.initialized = true;

        const float width = ImGui::GetContentRegionAvail().x;
        const float height = ImGui::GetFrameHeight();
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const ImVec2 end(pos.x + width, pos.y + height);
        const bool popup_open = ImGui::IsPopupOpen("##combo_popup");

        ImGui::InvisibleButton("##combo_field", ImVec2(width, height));
        bool changed = false;
        if (ImGui::IsItemClicked()) {
            ImGui::OpenPopup("##combo_popup");
        }
        const bool hovered = ImGui::IsItemHovered();
        const bool held = ImGui::IsItemActive();
        if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

        anim.hover = animate_to(anim.hover, hovered ? 1.0f : 0.0f, 16.0f);
        anim.active = animate_to(anim.active, (held || popup_open) ? 1.0f : 0.0f, 22.0f);

        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(pos, end,
            ImGui::GetColorU32(mix3(p.surface, p.surface_hover, p.surface_hover, anim.hover, anim.active)),
            Theme::radius().md);
        dl->AddRect(pos, end,
            ImGui::GetColorU32(Theme::mix(p.border_subtle, p.border_focus, anim.active)),
            Theme::radius().md);
        dl->AddText(ImVec2(pos.x + 12.0f, pos.y + (height - ImGui::GetTextLineHeight()) * 0.5f),
            ImGui::GetColorU32(p.text_primary), preview);

        const float arrow_w = 10.0f;
        const float arrow_h = 6.0f;
        const ImVec2 arrow_center(end.x - 16.0f, pos.y + height * 0.5f + 1.0f);
        const ImU32 arrow_col = ImGui::GetColorU32(p.text_primary);
        dl->AddTriangleFilled(
            ImVec2(arrow_center.x - arrow_w * 0.5f, arrow_center.y - arrow_h * 0.5f),
            ImVec2(arrow_center.x + arrow_w * 0.5f, arrow_center.y - arrow_h * 0.5f),
            ImVec2(arrow_center.x, arrow_center.y + arrow_h * 0.5f),
            arrow_col);

        ImGui::SetNextWindowPos(ImVec2(pos.x, end.y + 4.0f));
        ImGui::SetNextWindowSize(ImVec2(width, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_PopupBg, p.bg_elevated);
        ImGui::PushStyleColor(ImGuiCol_Header, Theme::with_alpha(p.accent_a, 0.20f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, Theme::with_alpha(p.accent_a, 0.30f));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, Theme::with_alpha(p.accent_a, 0.38f));
        ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, Theme::radius().md);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 6.0f));
        if (ImGui::BeginPopup("##combo_popup")) {
            for (int index = 0; index < item_count; ++index) {
                const bool selected = index == *current;
                const char* item = items[index] && items[index][0] ? items[index] : " ";
                if (ImGui::Selectable(item, selected)) {
                    *current = index;
                    changed = true;
                    ImGui::CloseCurrentPopup();
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndPopup();
        }
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(4);
        ImGui::PopID();
        end_labeled_field();
        return changed;
    }

    inline bool toggle(const char* label, bool* value) {
        if (!value) return false;
        const char* text = label && label[0] ? label : "toggle";
        ImGui::PushID(text);
        const Theme::Palette& p = Theme::palette();
        const float row_height = 30.0f;
        const float track_w = 42.0f;
        const float track_h = 22.0f;
        const float row_width = ImGui::GetContentRegionAvail().x;
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const ImGuiID id = ImGui::GetID("##toggle_row");
        AnimState& anim = anim_state(id);
        if (!anim.initialized) {
            anim.checked = *value ? 1.0f : 0.0f;
            anim.initialized = true;
        }

        ImGui::InvisibleButton("##toggle_row", ImVec2(row_width, row_height));
        bool changed = false;
        if (ImGui::IsItemClicked()) {
            *value = !*value;
            changed = true;
        }
        const bool hovered = ImGui::IsItemHovered();
        const bool held = ImGui::IsItemActive();
        if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        anim.hover = animate_to(anim.hover, hovered ? 1.0f : 0.0f, 16.0f);
        anim.active = animate_to(anim.active, held ? 1.0f : 0.0f, 22.0f);
        anim.checked = animate_to(anim.checked, *value ? 1.0f : 0.0f, 18.0f);
        ImDrawList* dl = ImGui::GetWindowDrawList();

        const ImVec2 track_pos(pos.x, pos.y + (row_height - track_h) * 0.5f);
        const ImVec2 track_end(track_pos.x + track_w, track_pos.y + track_h);
        const float rh = track_h * 0.5f;
        const ImVec4 off_col = Theme::mix(p.surface_raised, p.surface_hover, anim.hover);
        const ImVec4 track_col = Theme::mix(off_col, p.toggle_on, ease_out_cubic(anim.checked));
        dl->AddRectFilled(track_pos, track_end,
            ImGui::GetColorU32(track_col), rh);
        const float knob_r = rh - 3.0f;
        const float knob_x = track_pos.x + rh + ((track_w - track_h) * ease_out_cubic(anim.checked));
        const ImVec2 knob_c(knob_x, track_pos.y + rh);
        const float knob_scale = 1.0f - (ease_out_cubic(anim.active) * 0.06f);
        dl->AddCircleFilled(ImVec2(knob_c.x, knob_c.y + 1.0f), knob_r, IM_COL32(0, 0, 0, 55));
        dl->AddCircleFilled(knob_c, knob_r * knob_scale, ImGui::GetColorU32(p.text_primary));

        const float text_y = pos.y + (row_height - ImGui::GetTextLineHeight()) * 0.5f;
        dl->AddText(ImVec2(track_end.x + 12.0f, text_y), ImGui::GetColorU32(p.text_primary), text);
        ImGui::PopID();
        return changed;
    }

    inline bool tab_button(const char* label, bool active, const char* hint = nullptr, float width = 0.0f) {
        const char* text = label && label[0] ? label : "tab";
        const char* sub = hint && hint[0] ? hint : "";
        const Theme::Palette& p = Theme::palette();
        ImGui::PushID(text);
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const ImVec2 label_size = ImGui::CalcTextSize(text);
        const float height = sub[0] ? 44.0f : 34.0f;
        const float resolved_width = width > 0.0f ? width : label_size.x + 24.0f;
        const ImVec2 size(resolved_width, height);
        const ImGuiID id = ImGui::GetID("##tab");
        AnimState& anim = anim_state(id);
        if (!anim.initialized) {
            anim.checked = active ? 1.0f : 0.0f;
            anim.initialized = true;
        }
        ImGui::InvisibleButton("##tab", size);
        const bool hovered = ImGui::IsItemHovered();
        const bool pressed = ImGui::IsItemClicked();
        if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        anim.hover = animate_to(anim.hover, hovered ? 1.0f : 0.0f, 16.0f);
        anim.active = animate_to(anim.active, ImGui::IsItemActive() ? 1.0f : 0.0f, 22.0f);
        anim.checked = animate_to(anim.checked, active ? 1.0f : 0.0f, 16.0f);

        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 end(pos.x + size.x, pos.y + size.y);
        const float active_t = ease_out_cubic(anim.checked);
        const ImVec4 hover_fill = Theme::with_alpha(p.surface_hover, anim.hover * 0.58f);
        dl->AddRectFilled(pos, end, ImGui::GetColorU32(hover_fill), Theme::radius().sm);
        if (active_t > 0.001f) {
            dl->AddRectFilled(ImVec2(pos.x + 12.0f, end.y - 2.0f), ImVec2(end.x - 12.0f, end.y),
                ImGui::GetColorU32(Theme::with_alpha(p.accent_a, active_t)), Theme::radius().pill);
        }

        const float text_alpha = 0.58f + anim.hover * 0.22f + active_t * 0.20f;
        const float label_y = sub[0] ? pos.y + 5.0f : pos.y + (size.y - label_size.y) * 0.5f;
        dl->AddText(ImVec2(pos.x + 12.0f, label_y),
            ImGui::GetColorU32(Theme::with_alpha(p.text_primary, clamp01(text_alpha))), text);
        if (sub[0]) {
            dl->AddText(ImVec2(pos.x + 12.0f, pos.y + 26.0f),
                ImGui::GetColorU32(p.text_muted), sub);
        }
        ImGui::PopID();
        return pressed;
    }

    inline void tab_indicator(const char* id_text, const ImVec2& target_min, const ImVec2& target_max) {
        const Theme::Palette& p = Theme::palette();
        ImGui::PushID(id_text && id_text[0] ? id_text : "tabs");
        const ImGuiID id = ImGui::GetID("##indicator");
        AnimState& anim = anim_state(id);

        const float target_x = target_min.x + 14.0f;
        const float target_y = target_max.y - 4.0f;
        const float target_w = (target_max.x - target_min.x) - 28.0f;
        const float target_h = 2.0f;
        if (!anim.initialized) {
            anim.x = target_x;
            anim.y = target_y;
            anim.w = target_w;
            anim.h = target_h;
            anim.initialized = true;
        }

        anim.x = animate_to(anim.x, target_x, 18.0f);
        anim.y = animate_to(anim.y, target_y, 18.0f);
        anim.w = animate_to(anim.w, target_w, 18.0f);
        anim.h = animate_to(anim.h, target_h, 18.0f);
        ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(anim.x, anim.y),
            ImVec2(anim.x + anim.w, anim.y + anim.h), ImGui::GetColorU32(p.accent_a), Theme::radius().pill);
        ImGui::PopID();
    }

    inline void key_value(const char* key, const char* value) {
        const Theme::Palette& p = Theme::palette();
        const char* safe_key = key && key[0] ? key : "";
        const char* safe_value = value && value[0] ? value : "<unset>";
        begin_labeled_field(safe_key);
        ImGui::PushStyleColor(ImGuiCol_Text, p.text_primary);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextWrapped("%s", safe_value);
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
        end_labeled_field();
    }

    inline bool begin_card(const char* title, const char* subtitle = nullptr) {
        const Theme::Palette& p = Theme::palette();
        const Theme::Spacing& sp = Theme::spacing();
        const bool card_style = title && title[0];

        ImGui::PushStyleColor(ImGuiCol_ChildBg, card_style ? p.bg_elevated : ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, Theme::radius().lg);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(sp.card.x + 2.0f, sp.card.y + 2.0f));
        const bool open = ImGui::BeginChild(title && title[0] ? title : "##card", ImVec2(0.0f, 0.0f), false,
            ImGuiWindowFlags_AlwaysUseWindowPadding);
        if (open) {
            const ImVec2 c_pos = ImGui::GetWindowPos();
            const ImVec2 c_size = ImGui::GetWindowSize();
            ImDrawList* cdl = ImGui::GetWindowDrawList();
            if (card_style) {
                cdl->AddRect(c_pos, ImVec2(c_pos.x + c_size.x, c_pos.y + c_size.y),
                    ImGui::GetColorU32(p.border_subtle), Theme::radius().lg);
                ImGui::PushStyleColor(ImGuiCol_Text, p.text_primary);
                ImGui::PushFont(Theme::heading_font());
                ImGui::TextUnformatted(title);
                ImGui::PopFont();
                ImGui::PopStyleColor();
            }
            if (subtitle && subtitle[0]) {
                ImGui::PushStyleColor(ImGuiCol_Text, p.text_muted);
                ImGui::TextUnformatted(subtitle);
                ImGui::PopStyleColor();
            }
            ImGui::Dummy(ImVec2(0.0f, subtitle && subtitle[0] ? 12.0f : 10.0f));
        }
        return open;
    }

    inline void end_card() {
        ImGui::EndChild();
        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor(1);
    }
} // namespace ModUI::Widgets

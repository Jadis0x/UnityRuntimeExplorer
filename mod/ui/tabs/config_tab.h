// Copyright (c) 2026 Jadis0x. All rights reserved.
#pragma once
#include "config/mod_config.h"
#include <cstdio>
#include <imgui.h>
#include <string>
#include <vector>

#include "ui/localization.h"
#include "ui/widgets.h"
#include "ui/theme.h"

namespace ModUI::Tabs::Config {
    inline void section_label(const char* text) {
        const ModUI::Theme::Palette& p = ModUI::Theme::palette();
        ImGui::PushStyleColor(ImGuiCol_Text, ModUI::Theme::with_alpha(p.text_primary, 0.62f));
        ImGui::TextUnformatted(text && text[0] ? text : "Section");
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0.0f, 5.0f));
    }

    inline void render_controls(const char* hotkey) {
        section_label(ModUI::Localization::translate("config.controls"));
        ModUI::Widgets::toggle(ModUI::Localization::translate("config.show_menu"), &ModConfig::show_menu);
        ImGui::Dummy(ImVec2(0.0f, 10.0f));
        ModUI::Widgets::key_value(ModUI::Localization::translate("config.toggle_key"), hotkey);
    }

    inline void render_language_selector() {
        const std::vector<std::string>& languages = ModUI::Localization::available_languages();
        std::vector<const char*> labels;
        labels.reserve(languages.size());
        int current = 0;
        for (size_t i = 0; i < languages.size(); ++i) {
            labels.push_back(languages[i].c_str());
            if (languages[i] == ModUI::Localization::active_language()) current = static_cast<int>(i);
        }
        if (ModUI::Widgets::combo(ModUI::Localization::translate("config.language"), &current,
            labels.data(), static_cast<int>(labels.size()), "config.language")) {
            ModUI::Localization::set_language(languages[static_cast<size_t>(current)].c_str());
        }
        const char* error = ModUI::Localization::last_error_message();
        if (error && error[0]) {
            const ModUI::Theme::Palette& p = ModUI::Theme::palette();
            ImGui::Dummy(ImVec2(0.0f, 6.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, p.danger);
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextWrapped("%s", error);
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
        }
    }

    inline void render() {
        char hotkey[32]{};
        if (ModConfig::menu_toggle_key == VK_F7) {
            std::snprintf(hotkey, sizeof(hotkey), "F7 (0x%02X)", ModConfig::menu_toggle_key);
        } else {
            std::snprintf(hotkey, sizeof(hotkey), "0x%02X", ModConfig::menu_toggle_key);
        }
        render_controls(hotkey);
        if (ModConfig::enable_localization) {
            ImGui::Dummy(ImVec2(0.0f, 12.0f));
            section_label(ModUI::Localization::translate("config.localization"));
            ImGui::Dummy(ImVec2(0.0f, 10.0f));
            render_language_selector();
        }
    }
} // namespace ModUI::Tabs::Config

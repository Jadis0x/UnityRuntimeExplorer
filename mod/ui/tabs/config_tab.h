// Copyright (c) 2026 Jadis0x. All rights reserved.
#pragma once
#include "config/mod_config.h"
#include "config/user_settings.h"
#include <Windows.h>
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
        static bool waiting_for_key = false;
        section_label(ModUI::Localization::translate("config.controls"));
        ModUI::Widgets::toggle(ModUI::Localization::translate("config.show_menu"), &ModConfig::show_menu);
        ImGui::Dummy(ImVec2(0.0f, 10.0f));
        ModUI::Widgets::key_value(ModUI::Localization::translate("config.toggle_key"), hotkey);
        if (ImGui::Button(waiting_for_key ? "Press a keyboard key..." : "Change toggle key")) {
            waiting_for_key = !waiting_for_key;
            if (waiting_for_key)
                ModConfig::UserSettings::begin_toggle_key_capture();
            else
                ModConfig::UserSettings::end_toggle_key_capture();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset to F7")) {
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
            ImGui::TextColored(ImVec4(1.0f, 0.48f, 0.34f, 1.0f), "%s",
                               ModConfig::UserSettings::last_error().c_str());
    }

    inline void render_mcp_security() {
        section_label("MCP access and automation");
        bool enabled = ModConfig::enable_mcp.load();
        if (ImGui::Checkbox("Enable MCP bridge", &enabled))
            ModConfig::UserSettings::save_mcp_enabled(enabled);
        ImGui::SameLine();
        if (ImGui::SmallButton("Enable full access"))
            ModConfig::UserSettings::save_mcp_full_access(true);
        ImGui::SameLine();
        if (ImGui::SmallButton("Read-only preset")) {
            ModConfig::UserSettings::save_mcp_enabled(true);
            ModConfig::UserSettings::save_mcp_auto_discovery(true);
            ModConfig::UserSettings::save_mcp_property_access(false);
            ModConfig::UserSettings::save_mcp_writes(false);
            ModConfig::UserSettings::save_mcp_tracing(false);
            ModConfig::UserSettings::save_mcp_invocation(false);
            ModConfig::UserSettings::save_mcp_destructive_operations(false);
        }
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("Explorer settings are the authoritative permission boundary. MCP clients cannot grant themselves access through request arguments.");
        ImGui::PopTextWrapPos();
        ImGui::BeginDisabled(!enabled);
        ImGui::Dummy(ImVec2(0.0f, 8.0f));
        bool auto_discovery = ModConfig::enable_mcp_auto_discovery.load();
        if (ImGui::Checkbox("Allow automatic runtime discovery", &auto_discovery))
            ModConfig::UserSettings::save_mcp_auto_discovery(auto_discovery);
        bool allow_properties = ModConfig::enable_mcp_property_access.load();
        if (ImGui::Checkbox("Allow managed property getters", &allow_properties))
            ModConfig::UserSettings::save_mcp_property_access(allow_properties);
        bool allow_writes = ModConfig::enable_mcp_writes.load();
        if (ImGui::Checkbox("Allow managed member and object writes", &allow_writes))
            ModConfig::UserSettings::save_mcp_writes(allow_writes);
        bool allow_tracing = ModConfig::enable_mcp_tracing.load();
        if (ImGui::Checkbox("Allow MCP method tracing", &allow_tracing))
            ModConfig::UserSettings::save_mcp_tracing(allow_tracing);
        bool allow_invocation = ModConfig::enable_mcp_invocation.load();
        if (ImGui::Checkbox("Allow MCP method invocation", &allow_invocation))
            ModConfig::UserSettings::save_mcp_invocation(allow_invocation);
        bool allow_destructive = ModConfig::enable_mcp_destructive_operations.load();
        if (ImGui::Checkbox("Allow destructive Unity operations", &allow_destructive))
            ModConfig::UserSettings::save_mcp_destructive_operations(allow_destructive);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("Full access permits field/property writes, method execution, tracing, scene changes, component changes, duplication and destruction. Every request remains bounded and audited.");
        ImGui::PopTextWrapPos();
        ImGui::EndDisabled();
        if (!ModConfig::UserSettings::last_error().empty())
            ImGui::TextColored(ImVec4(1.0f, 0.48f, 0.34f, 1.0f), "%s",
                               ModConfig::UserSettings::last_error().c_str());
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
        const std::string hotkey = ModConfig::UserSettings::virtual_key_name(ModConfig::menu_toggle_key);
        render_controls(hotkey.c_str());
        ImGui::Dummy(ImVec2(0.0f, 12.0f));
        render_mcp_security();
        if (ModConfig::enable_localization) {
            ImGui::Dummy(ImVec2(0.0f, 12.0f));
            section_label(ModUI::Localization::translate("config.localization"));
            ImGui::Dummy(ImVec2(0.0f, 10.0f));
            render_language_selector();
        }
    }
} // namespace ModUI::Tabs::Config

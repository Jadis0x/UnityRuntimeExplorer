// Copyright (c) 2026 Jadis0x. All rights reserved.
#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include "config/mod_config.h"
#include <Windows.h>
#include <shellapi.h>
#include <imgui.h>
#include <string>

#include "ui/theme.h"
#include "ui/widgets.h"
#include "ui/localization.h"

namespace ModUI::Tabs::About {
inline std::string normalized_link(const char *raw) {
    if (!raw || !raw[0])
        return {};
    std::string link(raw);
    if (link.find("://") == std::string::npos) {
        link.insert(0, "https://");
    }
    return link;
}

inline void open_external_link(const char *raw) {
    const std::string link = normalized_link(raw);
    if (link.empty())
        return;
    ShellExecuteA(nullptr, "open", link.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

inline bool link_text(const char *label, const char *raw_url) {
    const ModUI::Theme::Palette &p = ModUI::Theme::palette();
    const std::string link = normalized_link(raw_url);
    const char *visible = link.empty() ? "<unset>" : link.c_str();
    ImGui::PushID(label && label[0] ? label : "link");

    ImGui::PushStyleColor(ImGuiCol_Text, link.empty() ? p.text_muted : p.info);
    ImGui::TextUnformatted(visible);
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    if (!link.empty()) {
        ImDrawList *dl = ImGui::GetWindowDrawList();
        dl->AddLine(ImVec2(min.x, max.y), ImVec2(max.x, max.y), ImGui::GetColorU32(p.info), 1.0f);
    }
    ImGui::PopStyleColor();

    bool activated = false;
    if (!link.empty() && ImGui::IsItemHovered()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if (ImGui::IsItemClicked()) {
            open_external_link(link.c_str());
            activated = true;
        }
    }
    ImGui::PopID();
    return activated;
}

inline bool social_badge(const char *label, const char *raw_url) {
    const ModUI::Theme::Palette &p = ModUI::Theme::palette();
    const std::string link = normalized_link(raw_url);
    if (link.empty())
        return false;

    const char *text = label && label[0] ? label : "Social";
    const ImVec2 text_size = ImGui::CalcTextSize(text);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImVec2 size(text_size.x + 22.0f, 26.0f);
    const ImVec2 end(pos.x + size.x, pos.y + size.y);

    ImGui::InvisibleButton(text, size);
    const bool hovered = ImGui::IsItemHovered();
    const bool pressed = ImGui::IsItemClicked();

    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(pos, end,
                      ImGui::GetColorU32(hovered ? Theme::with_alpha(p.surface_hover, 0.90f)
                                                 : Theme::with_alpha(p.surface_active, 0.76f)),
                      Theme::radius().pill);
    dl->AddText(ImVec2(pos.x + 11.0f, pos.y + 5.0f), ImGui::GetColorU32(p.text_primary), text);

    if (hovered)
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    if (pressed) {
        open_external_link(link.c_str());
        return true;
    }
    return false;
}

inline void render() {
    const ModUI::Theme::Palette &p = ModUI::Theme::palette();
    const char *description = ModConfig::description[0] ? ModConfig::description
                                                        : ModUI::Localization::translate("about.default_description");

    ModUI::Widgets::key_value(ModUI::Localization::translate("about.author"), ModConfig::author);
    ModUI::Widgets::key_value(ModUI::Localization::translate("about.version"), ModConfig::version);

    ModUI::Widgets::begin_labeled_field(ModUI::Localization::translate("about.url"));
    link_text(ModUI::Localization::translate("about.project_url"), ModConfig::url);
    ModUI::Widgets::end_labeled_field();

    if (ModConfig::social[0]) {
        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        ModUI::Widgets::begin_labeled_field(ModUI::Localization::translate("about.social"));
        social_badge(ModUI::Localization::translate("about.support"), ModConfig::social);
        ModUI::Widgets::end_labeled_field();
    }

    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, p.text_secondary);
    ImGui::TextWrapped("%s", description);
    ImGui::PopStyleColor();
}
} // namespace ModUI::Tabs::About

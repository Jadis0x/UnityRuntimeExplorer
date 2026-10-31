// Copyright (c) 2026 Jadis0x. All rights reserved.
#include "member_labels.h"
#include "ui_members.h"
#include "ui_shared.h"
#include "unity_editor_theme.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

namespace Explorer::UI {
namespace {

// A label reads as a name someone chose, so it gets the editor's link blue; an
// alias is only a stand-in and stays a muted teal so the two never look alike.
constexpr ImVec4 kLabelColor{0.55f, 0.76f, 0.98f, 1.0f};
constexpr ImVec4 kAliasColor{0.52f, 0.70f, 0.66f, 1.0f};

std::string original_text(Labels::Kind kind, std::string_view name) {
    return Labels::unreadable(name) ? Labels::alias(kind, name) : std::string(name);
}

std::string label_details(Labels::Kind kind, std::string_view name, const std::optional<Labels::Label> &label) {
    std::string text;
    if (label) {
        text += "Label: " + label->name + (label->source == "mcp" ? "  (MCP)" : "");
        if (label->stale)
            text += "\nWritten for another game build - check it still fits.";
        if (!label->note.empty())
            text += "\n" + label->note;
        text += '\n';
    }
    if (Labels::unreadable(name))
        text += "Original name is unreadable; shown as " + Labels::alias(kind, name) + ".\n";
    return text;
}

struct EditorState {
    std::string key;
    std::array<char, Labels::kMaxNameBytes + 1> name{};
    std::array<char, Labels::kMaxNoteBytes + 1> note{};
    bool all_overloads = false;
    std::string status;
    bool status_error = false;
};

EditorState &editor_state() {
    static EditorState state;
    return state;
}

} // namespace

void render_member_name(Labels::Kind kind, std::string_view type_name, std::string_view member,
                        std::string_view signature, std::string_view tooltip, bool dimmed) {
    const std::optional<Labels::Label> label = Labels::find(kind, type_name, member, signature);
    const bool unreadable = Labels::unreadable(member);
    ImGui::BeginGroup();
    if (label) {
        ImGui::TextColored(label->stale ? Unity::Skin::warning : kLabelColor, "%s", label->name.c_str());
        ImGui::SameLine(0.0f, 6.0f);
        ImGui::TextDisabled("%s", original_text(kind, member).c_str());
    } else if (unreadable) {
        ImGui::TextColored(kAliasColor, "%s", Labels::alias(kind, member).c_str());
    } else if (dimmed) {
        ImGui::TextDisabled("%.*s", static_cast<int>(member.size()), member.data());
    } else {
        ImGui::TextUnformatted(member.data(), member.data() + member.size());
    }
    ImGui::EndGroup();
    if (ImGui::IsItemHovered()) {
        std::string text = label_details(kind, member, label);
        if (!tooltip.empty())
            text += std::string(tooltip) + '\n';
        text += "Right-click to label or copy code";
        ImGui::SetTooltip("%s", text.c_str());
    }
}

std::string member_name_text(Labels::Kind kind, std::string_view type_name, std::string_view member,
                             std::string_view signature) {
    if (const std::optional<Labels::Label> label = Labels::find(kind, type_name, member, signature))
        return label->name + "  (" + original_text(kind, member) + ")";
    return original_text(kind, member);
}

std::string type_name_text(std::string_view type_name) {
    if (const std::optional<Labels::Label> label = Labels::find(Labels::Kind::Type, type_name))
        return label->name + "  (" + Labels::humanize(type_name) + ")";
    return Labels::humanize(type_name);
}

void render_label_editor(Labels::Kind kind, std::string_view type_name, std::string_view member,
                         std::string_view signature) {
    EditorState &state = editor_state();
    const std::string_view name = kind == Labels::Kind::Type ? type_name : member;
    std::string key = std::string(Labels::kind_name(kind)) + '\n' + std::string(type_name) + '\n' +
                      std::string(member) + '\n' + std::string(signature);
    const std::optional<Labels::Label> existing = Labels::find(kind, type_name, member, signature);
    if (state.key != key || ImGui::IsWindowAppearing()) {
        state.key = std::move(key);
        copy_text(state.name, existing ? std::string_view(existing->name) : std::string_view{});
        copy_text(state.note, existing ? std::string_view(existing->note) : std::string_view{});
        // An overloaded obfuscated method usually wants one label per overload,
        // but a label written without a signature still covers the rest.
        state.all_overloads = kind == Labels::Kind::Method && existing &&
                              !Labels::find_exact(kind, type_name, member, signature).has_value();
        state.status.clear();
    }

    ImGui::SeparatorText("Label");
    if (Labels::unreadable(name)) {
        ImGui::TextDisabled("Original shown as %s", Labels::alias(kind, name).c_str());
        if (ImGui::SmallButton("Copy original name"))
            ImGui::SetClipboardText(std::string(name).c_str());
    }
    if (existing && existing->stale)
        ImGui::TextColored(Unity::Skin::warning, "Written for another game build - check it still fits.");
    ImGui::SetNextItemWidth(280.0f);
    const bool submitted = ImGui::InputTextWithHint("##label-name", "Readable name, e.g. get_inventory",
                                                    state.name.data(), state.name.size(),
                                                    ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::InputTextMultiline("##label-note", state.note.data(), state.note.size(),
                              ImVec2(280.0f, ImGui::GetTextLineHeight() * 3.5f));
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Optional note: what it does, where it is called from, how you found out.");
    if (kind == Labels::Kind::Method) {
        ImGui::Checkbox("Every overload of this name", &state.all_overloads);
    }
    const auto save = [&] {
        Labels::Entry entry;
        entry.kind = kind;
        entry.type_name = std::string(type_name);
        entry.member = std::string(member);
        entry.signature = state.all_overloads ? std::string{} : std::string(signature);
        entry.label.name = state.name.data();
        entry.label.note = state.note.data();
        entry.label.source = "user";
        std::string error;
        state.status_error = !Labels::set(std::move(entry), &error);
        state.status = state.status_error ? error : "Saved";
    };
    if (submitted || ImGui::Button("Save label"))
        save();
    if (existing) {
        ImGui::SameLine();
        if (ImGui::Button("Remove label")) {
            std::string error;
            // Remove whichever label is actually in effect: this overload's own, or the shared one.
            const std::string_view removed_signature =
                Labels::find_exact(kind, type_name, member, signature) ? signature : std::string_view{};
            state.status_error = !Labels::remove(kind, type_name, member, removed_signature, &error);
            state.status = state.status_error ? error : "Removed";
            state.name.fill('\0');
            state.note.fill('\0');
        }
    }
    if (!state.status.empty())
        ImGui::TextColored(state.status_error ? Unity::Skin::error : Unity::Skin::text_dim, "%s",
                           state.status.c_str());
}

void render_labels_panel() {
    struct PanelState {
        std::array<char, 128> filter{};
        std::uint64_t revision = 0;
        std::vector<Labels::Entry> entries;
    };
    static PanelState state;
    if (state.revision != Labels::revision()) {
        state.revision = Labels::revision();
        state.entries = Labels::list();
        std::sort(state.entries.begin(), state.entries.end(), [](const Labels::Entry &left, const Labels::Entry &right) {
            return std::tie(left.type_name, left.member, left.signature) <
                   std::tie(right.type_name, right.member, right.signature);
        });
    }

    const std::string path = Labels::storage_path();
    ImGui::TextDisabled("%zu label(s) for this game", state.entries.size());
    ImGui::SameLine();
    if (ImGui::SmallButton("Copy file path"))
        ImGui::SetClipboardText(path.c_str());
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", path.empty() ? "Unavailable" : path.c_str());
    if (const std::string error = Labels::last_error(); !error.empty())
        ImGui::TextColored(Unity::Skin::warning, "%s", error.c_str());
    ImGui::TextDisabled("Right-click any type, field, property or method to label it. MCP clients can label too.");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##labels-filter", "Filter label, note, type or member...", state.filter.data(),
                             state.filter.size());
    const std::string_view filter = state.filter.data();

    if (!ImGui::BeginTable("##labels", 5,
                           ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable |
                               ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp))
        return;
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthStretch, 1.2f);
    ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthFixed, 64.0f);
    ImGui::TableSetupColumn("Member", ImGuiTableColumnFlags_WidthStretch, 1.6f);
    ImGui::TableSetupColumn("Note", ImGuiTableColumnFlags_WidthStretch, 1.6f);
    ImGui::TableSetupColumn("##actions", ImGuiTableColumnFlags_WidthFixed, 64.0f);
    ImGui::TableHeadersRow();
    for (std::size_t index = 0; index < state.entries.size(); ++index) {
        const Labels::Entry &entry = state.entries[index];
        const std::string owner = Labels::humanize(entry.type_name);
        const std::string member =
            entry.kind == Labels::Kind::Type ? std::string{} : original_text(entry.kind, entry.member);
        if (!filter.empty() && !contains_case_insensitive(entry.label.name, filter) &&
            !contains_case_insensitive(entry.label.note, filter) && !contains_case_insensitive(owner, filter) &&
            !contains_case_insensitive(member, filter))
            continue;
        ImGui::PushID(static_cast<int>(index));
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextColored(entry.label.stale ? Unity::Skin::warning : kLabelColor, "%s", entry.label.name.c_str());
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s%s", entry.label.source == "mcp" ? "Written by an MCP client" : "Written in the overlay",
                              entry.label.stale ? "\nWritten for another game build - check it still fits." : "");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextDisabled("%s", Labels::kind_name(entry.kind));
        ImGui::TableSetColumnIndex(2);
        const std::string where = member.empty() ? owner : owner + "." + member;
        ImGui::TextUnformatted(where.c_str());
        if (!entry.signature.empty() && ImGui::IsItemHovered())
            ImGui::SetTooltip("(%s)", Labels::humanize(entry.signature).c_str());
        ImGui::TableSetColumnIndex(3);
        ImGui::TextWrapped("%s", entry.label.note.c_str());
        ImGui::TableSetColumnIndex(4);
        if (ImGui::SmallButton("Remove")) {
            std::string error;
            Labels::remove(entry.kind, entry.type_name, entry.member, entry.signature, &error);
        }
        ImGui::PopID();
    }
    ImGui::EndTable();
}

} // namespace Explorer::UI

// Copyright (c) 2026 Jadis0x. All rights reserved.
// Object Inspector: the reference tab strip and the member view behind it.
#include "ui_members.h"

#include "byte_data_decoder.h"
#include "config/mod_config.h"
#include "explorer_model.h"
#include "ui_shared.h"
#include "unity_editor_theme.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace Explorer::UI {
namespace {

struct ObjectReferenceTabState {
    struct Tab {
        std::uint64_t token = 0;
        std::string label;
    };
    std::vector<Tab> tabs;
    std::unordered_set<std::uint64_t> closed_tokens;
    std::unordered_set<std::uint64_t> requested_tokens;
    std::uint64_t last_seen_token = 0;
    std::uint64_t pending_activation_token = 0;
    std::uint64_t selected_token = 0;
};

ObjectReferenceTabState &object_reference_tabs() {
    static ObjectReferenceTabState state;
    return state;
}

} // namespace
void request_object_reference_tab(std::uint64_t token) {
    if (token == 0)
        return;
    ObjectReferenceTabState &state = object_reference_tabs();
    // A new Inspect request reopens a previously closed token.
    state.closed_tokens.erase(token);
    state.requested_tokens.erase(token);
    state.pending_activation_token = token;
    const auto found = std::find_if(state.tabs.begin(), state.tabs.end(),
                                    [token](const ObjectReferenceTabState::Tab &tab) { return tab.token == token; });
    if (found != state.tabs.end())
        state.selected_token = token;
}
namespace {

void remember_object_reference_tab(const ObjectInspectorInfo &info) {
    if (!info.valid || info.token == 0)
        return;
    ObjectReferenceTabState &state = object_reference_tabs();
    const bool changed_object = state.last_seen_token != info.token;
    if (changed_object)
        state.closed_tokens.erase(info.token);
    state.last_seen_token = info.token;
    state.requested_tokens.erase(info.token);
    if (state.closed_tokens.contains(info.token))
        return;
    const auto found =
        std::find_if(state.tabs.begin(), state.tabs.end(),
                     [&info](const ObjectReferenceTabState::Tab &tab) { return tab.token == info.token; });
    const bool added = found == state.tabs.end();
    if (added)
        state.tabs.push_back({info.token, info.type_name});
    else
        found->label = info.type_name;
    // Select existing tabs only on explicit Inspect requests.
    if (state.pending_activation_token == info.token || (added && state.tabs.size() == 1)) {
        state.selected_token = info.token;
        state.pending_activation_token = 0;
    }
}

} // namespace
void close_object_reference_tab(std::uint64_t token) {
    if (token == 0)
        return;
    Command command{};
    command.kind = CommandKind::CloseObjectInspectorTab;
    command.object_inspector_token = token;
    RuntimeModel::instance().enqueue(std::move(command));
}
void render_current_object_inspector(const Snapshot &snapshot) {
    ImGui::BeginChild("##object-inspector", ImVec2(0.0f, 0.0f), true);
    const ObjectInspectorInfo &info = snapshot.object_inspector;
    if (!info.valid || (!info.is_array && !info.component.metadata)) {
        ImGui::TextDisabled("Select Inspect on an object reference to inspect it here.");
        ImGui::EndChild();
        return;
    }
    ImGui::TextUnformatted(info.type_name.c_str());
    if (info.instance_id != 0) {
        ImGui::SameLine();
        ImGui::TextDisabled("Instance ID: %d", info.instance_id);
    }
    if (!info.pointer_text.empty()) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Copy Ptr"))
            ImGui::SetClipboardText(info.pointer_text.c_str());
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Save reference")) {
        Command command{};
        command.kind = CommandKind::PinManagedReference;
        command.object_inspector_target = true;
        RuntimeModel::instance().enqueue(std::move(command));
    }
    ImGui::Separator();
    if (info.is_array) {
        const std::string heading =
            "Array<" + (info.array_element_type.empty() ? std::string("unknown") : info.array_element_type) +
            ">  Length: " + std::to_string(info.array_length);
        ImGui::TextUnformatted(heading.c_str());
        const ComponentInfo::LiveValues *values = info.array_values.get();
        const std::size_t page_end = std::min(info.array_length, info.array_offset + 128);
        ImGui::TextDisabled("Elements %zu - %zu", info.array_length == 0 ? 0 : info.array_offset,
                            page_end == 0 ? 0 : page_end - 1);
        ImGui::SameLine();
        ImGui::BeginDisabled(info.array_offset == 0);
        if (ImGui::SmallButton("Previous 128")) {
            Command command{.kind = CommandKind::SetArrayPage};
            command.object_inspector_token = info.token;
            command.int_value = static_cast<int>(info.array_offset > 128 ? info.array_offset - 128 : 0);
            RuntimeModel::instance().enqueue(std::move(command));
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(page_end >= info.array_length);
        if (ImGui::SmallButton("Next 128")) {
            Command command{.kind = CommandKind::SetArrayPage};
            command.object_inspector_token = info.token;
            command.int_value = static_cast<int>(page_end);
            RuntimeModel::instance().enqueue(std::move(command));
        }
        ImGui::EndDisabled();
        if (info.byte_array) {
            const ObjectInspectorInfo::ByteArrayInspection &byte_array = *info.byte_array;
            ImGui::SeparatorText("Byte Data Decoder");
            ImGui::TextDisabled("Captured %zu of %zu byte(s)%s", byte_array.bytes.size(), info.array_length,
                                byte_array.truncated ? " (capture limit reached)" : "");
            ImGui::SameLine();
            if (ImGui::SmallButton("Refresh decoded bytes")) {
                Command command{.kind = CommandKind::RefreshByteArrayInspection};
                command.object_inspector_token = info.token;
                RuntimeModel::instance().enqueue(std::move(command));
            }
            if (!byte_array.read_error.empty()) {
                ImGui::TextColored(ImVec4(0.78f, 0.42f, 0.38f, 1.0f), "Byte capture failed: %s",
                                   byte_array.read_error.c_str());
            } else {
                const ByteData::DecodeResult &decoded = byte_array.decoded;
                const std::string decoded_label = std::string("Auto-detected: ") +
                    std::string(ByteData::format_name(decoded.format)) +
                    (decoded.summary.empty() ? std::string{} : " — " + decoded.summary);
                ImGui::TextColored(decoded.complete ? ImVec4(0.60f, 0.68f, 0.60f, 1.0f)
                                                   : ImVec4(0.72f, 0.60f, 0.42f, 1.0f),
                                   "%s", decoded_label.c_str());
                if (!decoded.diagnostic.empty())
                    ImGui::TextDisabled("%s", decoded.diagnostic.c_str());
                if (!decoded.document.empty()) {
                    if (ImGui::SmallButton("Copy decoded"))
                        ImGui::SetClipboardText(decoded.document.c_str());
                    ImGui::BeginChild("##byte-decoded-document", ImVec2(0.0f, 180.0f), true,
                                      ImGuiWindowFlags_HorizontalScrollbar);
                    ImGui::TextUnformatted(decoded.document.c_str());
                    ImGui::EndChild();
                }
            }
            if (ImGui::CollapsingHeader("Raw hex preview", ImGuiTreeNodeFlags_DefaultOpen)) {
                const std::string hex = ByteData::hex_dump(byte_array.bytes);
                if (ImGui::SmallButton("Copy captured hex")) {
                    const std::string complete_hex = ByteData::hex_dump(byte_array.bytes, byte_array.bytes.size());
                    ImGui::SetClipboardText(complete_hex.c_str());
                }
                ImGui::BeginChild("##byte-hex-preview", ImVec2(0.0f, 175.0f), true,
                                  ImGuiWindowFlags_HorizontalScrollbar);
                ImGui::TextUnformatted(hex.c_str());
                ImGui::EndChild();
            }
        }
        ImGui::Separator();
        if (ImGui::BeginTable("##array-elements", 2,
                              ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Index", ImGuiTableColumnFlags_WidthFixed, 72.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            const std::size_t count = values ? values->fields.size() : 0;
            for (std::size_t row = 0; row < count; ++row) {
                const std::size_t index = info.array_offset + row;
                ImGui::PushID(static_cast<int>(index));
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("[%zu]", index);
                ImGui::TableSetColumnIndex(1);
                const auto *reference =
                    row < values->field_references.size() ? &values->field_references[row] : nullptr;
                const auto &element = values->fields[row];
                const bool writable = editable_value(element) ||
                                      element.kind == URK::Unity::Inspect::ValueKind::ObjectReference ||
                                      element.kind == URK::Unity::Inspect::ValueKind::ArrayReference ||
                                      element.kind == URK::Unity::Inspect::ValueKind::Null;
                render_live_value(CommandKind::SetFieldValue, 0, static_cast<int>(index), &element, writable,
                                  scoped_ui_key(info.token, 0x2000000000000000ull, index), reference, true,
                                  snapshot.live_data, false, false, info.token, true, {}, &snapshot.managed_references,
                                  &snapshot.audio_preview, &snapshot.texture_preview);
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        ImGui::EndChild();
        return;
    }
    const ComponentInfo::Metadata &metadata = *info.component.metadata;
    const ComponentInfo::LiveValues *live = info.component.live_values.get();
    const CodeContext object_code = code_context(info.assembly_name, info.namespace_name, info.class_name, info.type_name);
    std::array<char, 128>& filter_buffer = object_member_filter(info.token);
    const float clear_width = ImGui::CalcTextSize("Clear").x + ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::SetNextItemWidth(std::max(100.0f, ImGui::GetContentRegionAvail().x - clear_width -
                                             ImGui::GetStyle().ItemSpacing.x));
    ImGui::InputTextWithHint("##object-member-filter", "Search name, type, owner or parameter...",
                             filter_buffer.data(), filter_buffer.size());
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear"))
        filter_buffer.fill('\0');
    const std::string_view member_filter = filter_buffer.data();
    ImGui::BeginChild("##object-inspector-members", ImVec2(0.0f, 0.0f), false,
                      ImGuiWindowFlags_HorizontalScrollbar);
    auto render_members = [&](const char *id, const auto &members, const auto *values, const auto *references,
                              bool property_members) {
        if (!ImGui::BeginTable(
                id, 3,
                ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoPadOuterX))
            return;
        ImGui::TableSetupColumn("Member", ImGuiTableColumnFlags_WidthFixed, 190.0f);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Watch", ImGuiTableColumnFlags_WidthFixed, 82.0f);
        std::vector<std::size_t> visible_members;
        visible_members.reserve(members.size());
        for (std::size_t index = 0; index < members.size(); ++index)
            if (member_matches_filter(members[index].name, members[index].type_name,
                                      members[index].declaring_type, member_filter))
                visible_members.push_back(index);
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(visible_members.size()), ImGui::GetFrameHeightWithSpacing());
        while (clipper.Step())
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                const std::size_t index = visible_members[static_cast<std::size_t>(row)];
                const auto &member = members[index];
                const auto *value = values && index < values->size() ? &(*values)[index] : nullptr;
                const auto *reference = references && index < references->size() ? &(*references)[index] : nullptr;
                ImGui::PushID(static_cast<int>(index));
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(member.name.c_str());
                if constexpr (requires { member.can_write; })
                    render_property_context_menu(member, object_code, {});
                else
                    render_field_context_menu(member, object_code, {});
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s\nRight-click for copy/code options", member.type_name.c_str());
                ImGui::TableSetColumnIndex(1);
                bool writable = !info.is_value_type || info.value_origin_component_id != 0;
                if constexpr (requires { member.can_write; })
                    writable = writable && member.can_write;
				else if constexpr (requires { member.is_read_only; })
					writable = writable && !member.is_read_only;
                const bool properties = requires { member.can_write; };
                const std::uint64_t key =
                    scoped_ui_key(info.token, properties ? 0x6100000000000000ull : 0x6000000000000000ull, index);
                render_live_value(properties ? CommandKind::SetPropertyValue : CommandKind::SetFieldValue, 0,
                                  static_cast<int>(index), value, writable, key, reference, true, snapshot.live_data,
                                  snapshot.locked_member_keys.contains(key), true, info.token, member.runtime_safe,
                                  member.capability_reason, &snapshot.managed_references, &snapshot.audio_preview,
                                  &snapshot.texture_preview);
				render_member_write_result(snapshot, 0, index, properties, info.token);
				{
					const Snapshot::FieldWatch* watch = field_watch_for(snapshot, 0, index, info.token, property_members);
                    ImGui::TableSetColumnIndex(2);
                    if (ImGui::SmallButton(watch && watch->active ? "Stop watch" : "Watch"))
                        enqueue_field_watch(0, static_cast<int>(index), !(watch && watch->active), info.token,
                                            property_members);
                }
                ImGui::PopID();
            }
        clipper.End();
        ImGui::EndTable();
    };
    const auto visible_member_count = [&](const auto& members) {
        return static_cast<std::size_t>(std::count_if(members.begin(), members.end(), [&](const auto& member) {
            return member_matches_filter(member.name, member.type_name, member.declaring_type, member_filter);
        }));
    };
    const std::size_t visible_fields = visible_member_count(metadata.fields);
    const std::size_t visible_properties = visible_member_count(metadata.properties);
    const std::size_t visible_methods = static_cast<std::size_t>(
        std::count_if(metadata.methods.begin(), metadata.methods.end(),
                      [&](const ComponentInfo::Method& method) {
                          return method_matches_filter(method, member_filter);
                      }));
    // Same one-kind-at-a-time layout the component inspector uses.
    const std::size_t object_counts[3] = {visible_fields, visible_properties, visible_methods};
    int &object_member_tab = object_member_tab_for(info.token);
    int forced_object_tab = -1;
    if (!member_filter.empty() && object_counts[object_member_tab] == 0)
        for (int candidate = 0; candidate < 3; ++candidate)
            if (object_counts[candidate] > 0) {
                forced_object_tab = candidate;
                break;
            }
    const auto object_tab_flags = [&](int slot) {
        return forced_object_tab == slot ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
    };
    char object_tab_label[96];
    if (ImGui::BeginTabBar("##object-member-kinds", ImGuiTabBarFlags_FittingPolicyScroll)) {
    std::snprintf(object_tab_label, sizeof(object_tab_label), "Fields (%zu / %zu)###ofields",
                  visible_fields, metadata.fields.size());
    if (Unity::begin_member_tab(object_tab_label, Unity::Skin::action_blue, object_tab_flags(0))) {
        object_member_tab = 0;
        render_members("##object-field-table", metadata.fields, live ? &live->fields : nullptr,
                       live ? &live->field_references : nullptr, false);
        ImGui::EndTabItem();
    }
    std::snprintf(object_tab_label, sizeof(object_tab_label), "Properties (%zu / %zu)###oproperties",
                  visible_properties, metadata.properties.size());
    if (Unity::begin_member_tab(object_tab_label, Unity::Skin::action_green, object_tab_flags(1))) {
        object_member_tab = 1;
        render_members("##object-property-table", metadata.properties, live ? &live->properties : nullptr,
                       live ? &live->property_references : nullptr, true);
        ImGui::EndTabItem();
    }
    std::snprintf(object_tab_label, sizeof(object_tab_label), "Methods (%zu / %zu)###omethods",
                  visible_methods, metadata.methods.size());
    if (Unity::begin_member_tab(object_tab_label, Unity::Skin::action_amber, object_tab_flags(2))) {
        object_member_tab = 2;
        for (std::size_t index = 0; index < metadata.methods.size(); ++index) {
            const ComponentInfo::Method &method = metadata.methods[index];
            if (!method_matches_filter(method, member_filter))
                continue;
            ImGui::PushID(static_cast<int>(index));
            ImGui::TextDisabled("%s", method.return_type.c_str());
            ImGui::SameLine();
            ImGui::TextUnformatted(method.name.c_str());
            render_method_context_menu(method, object_code);
            ImGui::SameLine();
            std::string parameters = "(";
            for (std::size_t parameter = 0; parameter < method.parameter_types.size(); ++parameter) {
                if (parameter)
                    parameters += ", ";
                parameters += method.parameter_types[parameter];
            }
            parameters += ")";
            ImGui::TextDisabled("%s", parameters.c_str());
            if (method.uses_generic_parameter) {
                MemberBuffer& generic_type = member_buffer(generic_type_key(0, index, info.token));
                ImGui::SetNextItemWidth(-1.0f);
                render_generic_type_input(snapshot, "##generic-type", generic_type.text);
                ImGui::TextDisabled("Example: bolt.user.dll:Photon.Bolt.IPlayerState");
            }
            if (!method.parameter_types.empty()) {
                ImGui::Indent();
                for (std::size_t parameter = 0; parameter < method.parameter_types.size(); ++parameter) {
                    const std::string &type = method.parameter_types[parameter];
                    const std::string name =
                        parameter < method.parameter_names.size() && !method.parameter_names[parameter].empty()
                            ? method.parameter_names[parameter]
                            : "arg" + std::to_string(parameter + 1);
                    ImGui::PushID(static_cast<int>(parameter));
                    render_method_argument(0, index, parameter, type, name, info.token, &snapshot.managed_references);
                    ImGui::PopID();
                }
                ImGui::Unindent();
            }
            const MethodTracer::Snapshot *trace = trace_for_method(snapshot.method_traces, method);
            if (trace && trace->active) {
                if (ImGui::SmallButton("Stop tracing"))
                    enqueue_method_trace(0, static_cast<int>(index), false, true, info.token);
            } else {
                render_trace_button([&](bool capture_return) {
                    enqueue_method_trace(0, static_cast<int>(index), true, true, info.token, capture_return);
                });
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(!invokable_method(method));
            if (ImGui::SmallButton("Execute"))
                enqueue_method_invoke(0, static_cast<int>(index), method, true, info.token);
            ImGui::EndDisabled();
            render_method_result(snapshot, 0, index, info.token);
            ImGui::PopID();
        }
        ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
    }
    ImGui::EndChild();
    ImGui::EndChild();
}
namespace {
} // namespace
void render_object_inspector(const Snapshot &snapshot) {
    ObjectReferenceTabState &tabs = object_reference_tabs();
    if (snapshot.object_inspector.valid)
        remember_object_reference_tab(snapshot.object_inspector);
    else
        tabs.last_seen_token = 0;

    if (tabs.tabs.empty()) {
        render_current_object_inspector(snapshot);
        return;
    }
    // Preserve explicit tab selection during asynchronous updates.
    if (tabs.selected_token == 0 ||
        std::none_of(tabs.tabs.begin(), tabs.tabs.end(),
                     [&tabs](const ObjectReferenceTabState::Tab &tab) { return tab.token == tabs.selected_token; }))
        tabs.selected_token = tabs.tabs.front().token;

    std::uint64_t close_token = 0;
	bool close_all = false;
    ImGui::BeginChild("##object-reference-tab-strip", ImVec2(0.0f, 31.0f), false,
                      ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
	if (tabs.tabs.size() > 1) {
		if (ImGui::SmallButton("Close all##object-reference-tabs"))
			close_all = true;
		ImGui::SameLine(0.0f, 6.0f);
	}
    for (const ObjectReferenceTabState::Tab &tab : tabs.tabs) {
        ImGui::PushID(
            static_cast<int>(
                static_cast<std::uint32_t>(tab.token >> 32)));

        ImGui::PushID(
            static_cast<int>(
                static_cast<std::uint32_t>(tab.token)));
        const bool selected = tabs.selected_token == tab.token;
        ImGui::PushStyleColor(ImGuiCol_Button,
                              selected ? ImVec4(0.30f, 0.43f, 0.56f, 1.0f) : ImVec4(0.16f, 0.16f, 0.16f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.36f, 0.49f, 0.62f, 1.0f));
        if (ImGui::SmallButton(tab.label.c_str())) {
            tabs.selected_token = tab.token;
            if ((!snapshot.object_inspector.valid || snapshot.object_inspector.token != tab.token) &&
                tabs.requested_tokens.insert(tab.token).second)
                enqueue_reference_inspection(tab.token);
        }
        ImGui::PopStyleColor(2);
        ImGui::SameLine(0.0f, 2.0f);
        if (ImGui::SmallButton("x##close-object-tab"))
            close_token = tab.token;
        ImGui::SameLine(0.0f, 5.0f);
        ImGui::PopID();
        ImGui::PopID();
    }
    ImGui::EndChild();
	if (close_all) {
		std::vector<std::uint64_t> tokens;
		tokens.reserve(tabs.tabs.size());
		for (const ObjectReferenceTabState::Tab& tab : tabs.tabs) {
			tabs.closed_tokens.insert(tab.token);
			tabs.requested_tokens.erase(tab.token);
			tokens.push_back(tab.token);
		}
		tabs.tabs.clear();
		tabs.pending_activation_token = 0;
		tabs.selected_token = 0;
		for (const std::uint64_t token : tokens)
			close_object_reference_tab(token);
		ImGui::TextDisabled("All object inspector tabs closed.");
		return;
	}

    if (close_token != 0) {
        tabs.closed_tokens.insert(close_token);
        tabs.requested_tokens.erase(close_token);
        if (tabs.pending_activation_token == close_token)
            tabs.pending_activation_token = 0;
        close_object_reference_tab(close_token);
        tabs.tabs.erase(std::remove_if(tabs.tabs.begin(), tabs.tabs.end(),
                                       [close_token](const auto &tab) { return tab.token == close_token; }),
                        tabs.tabs.end());
        if (tabs.selected_token == close_token)
            tabs.selected_token = tabs.tabs.empty() ? 0 : tabs.tabs.back().token;
        if (tabs.selected_token != 0 &&
            (!snapshot.object_inspector.valid || snapshot.object_inspector.token != tabs.selected_token) &&
            tabs.requested_tokens.insert(tabs.selected_token).second)
            enqueue_reference_inspection(tabs.selected_token);
    }

    const auto selected = std::find_if(tabs.tabs.begin(), tabs.tabs.end(),
                                       [&tabs](const auto &tab) { return tab.token == tabs.selected_token; });
    if (selected == tabs.tabs.end()) {
        ImGui::TextDisabled("Select Inspect on an object reference to inspect it here.");
    } else if (!snapshot.object_inspector.valid || snapshot.object_inspector.token != selected->token) {
        ImGui::TextDisabled("Loading %s...", selected->label.c_str());
    } else {
        render_current_object_inspector(snapshot);
    }
}
namespace {
} // namespace
} // namespace Explorer::UI

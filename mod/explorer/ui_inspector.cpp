// Copyright (c) 2026 Jadis0x. All rights reserved.
// Component inspector: identity, transform, the component list and Add Component.
#include "ui_members.h"

#include "config/mod_config.h"
#include "explorer_model.h"
#include "ui_shared.h"

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

// Trailing segment of a namespace-qualified type, for compact labels.
std::string short_field_component_name(std::string_view type_name) {
    const std::size_t separator = type_name.rfind('.');
    return separator == std::string_view::npos ? std::string(type_name) : std::string(type_name.substr(separator + 1));
}


// -1 collapses every component on the next frame, +1 expands, 0 leaves the
// per-component state alone. Consumed once per inspector frame.
int &component_bulk_toggle() {
    static int state = 0;
    return state;
}

void render_components(const InspectorInfo &info, const Snapshot &snapshot, int only_component_id = 0,
                       const char *fixed_filter = nullptr, bool show_inherited = false) {
    const bool live_data = snapshot.live_data;
    const std::string_view inspector_filter(fixed_filter ? fixed_filter : "");
    const bool searching = !inspector_filter.empty();
    const int bulk_toggle = component_bulk_toggle();
    component_bulk_toggle() = 0;
    for (const ComponentInfo &component : info.components) {
        if (only_component_id != 0 && component.instance_id != only_component_id)
            continue;
        ImGui::PushID(component.instance_id);
        const std::string display_type = short_field_component_name(component.type_name);
        // While a search is running the list shows only components that can
        // answer it. A component whose metadata has not arrived yet cannot be
        // judged, so it stays visible until its members are known.
        if (searching && component.metadata && !component.metadata_unavailable &&
            !contains_case_insensitive(component.type_name, inspector_filter)) {
            const ComponentInfo::Metadata &probe = *component.metadata;
            const auto in_scope = [&](std::string_view declaring) {
                return show_inherited || declaring.empty() || declaring == component.type_name;
            };
            bool any_match = false;
            for (const auto &field : probe.fields)
                any_match = any_match || (in_scope(field.declaring_type) &&
                                          member_matches_filter(field.name, field.type_name, field.declaring_type,
                                                                inspector_filter));
            for (const auto &property : probe.properties)
                any_match = any_match || (in_scope(property.declaring_type) &&
                                          member_matches_filter(property.name, property.type_name,
                                                                property.declaring_type, inspector_filter));
            for (const auto &method : probe.methods)
                any_match = any_match ||
                            (in_scope(method.declaring_type) && method_matches_filter(method, inspector_filter));
            if (!any_match) {
                ImGui::PopID();
                continue;
            }
        }
        if (component.enabled_supported) {
            bool enabled = component.enabled;
            if (ImGui::Checkbox("##enabled", &enabled)) {
                Command command{};
                command.kind = CommandKind::SetComponentEnabled;
                command.instance_id = component.instance_id;
                command.bool_value = enabled;
                RuntimeModel::instance().enqueue(std::move(command));
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Enable / disable component");
        } else
            ImGui::Dummy(ImVec2(ImGui::GetFrameHeight(), ImGui::GetFrameHeight()));
        ImGui::SameLine();
        // The expanded component wears a lit header; the closed ones stay a flat
        // slab. That is the whole "which component am I in" cue, and it beats
        // drawing a marker line next to the body.
        const bool was_open = ImGui::TreeNodeGetOpen(ImGui::GetID("##component"));
        ImGui::PushStyleColor(ImGuiCol_Header, was_open ? ImVec4(0.157f, 0.294f, 0.451f, 1.0f)
                                                        : ImVec4(0.145f, 0.157f, 0.180f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, was_open ? ImVec4(0.196f, 0.361f, 0.545f, 1.0f)
                                                               : ImVec4(0.196f, 0.212f, 0.243f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.220f, 0.412f, 0.616f, 1.0f));
        // Components start closed: a GameObject with a dozen of them used to
        // open as one undifferentiated wall of members. A search still needs to
        // reach inside, so it forces its matches open.
        if (searching)
            ImGui::SetNextItemOpen(true, ImGuiCond_Always);
        else if (bulk_toggle != 0)
            ImGui::SetNextItemOpen(bulk_toggle > 0, ImGuiCond_Always);
        else if (only_component_id != 0)
            ImGui::SetNextItemOpen(true, ImGuiCond_FirstUseEver);
        const bool open = ImGui::TreeNodeEx("##component",
                                            ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick |
                                                ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_Framed |
                                                ImGuiTreeNodeFlags_FramePadding,
                                            "%s", display_type.c_str());
        ImGui::PopStyleColor(3);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s\nRight-click for component actions", component.type_name.c_str());
        // The namespace rides on the right of the header so two components whose
        // short names collide stay distinguishable while collapsed.
        if (!component.namespace_name.empty()) {
            const float label_width = ImGui::CalcTextSize(component.namespace_name.c_str()).x;
            const float right_edge = ImGui::GetContentRegionMax().x - label_width - ImGui::GetStyle().FramePadding.x;
            if (right_edge > ImGui::GetCursorPosX() + 40.0f) {
                ImGui::SameLine(right_edge);
                ImGui::TextDisabled("%s", component.namespace_name.c_str());
            }
        }
        if (ImGui::BeginPopupContextItem("##component-context")) {
            if (ImGui::MenuItem("Copy Component Pointer"))
                ImGui::SetClipboardText(component.pointer_text.c_str());
            if (ImGui::MenuItem("Copy Runtime Type")) {
                const std::string details = type_details_text(component.assembly_name, component.namespace_name,
                                                              component.class_name, component.type_name);
                ImGui::SetClipboardText(details.c_str());
            }
            ImGui::Separator();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.55f, 0.50f, 1.0f));
            if (ImGui::MenuItem("Remove Component"))
                enqueue_simple(CommandKind::DeleteComponent, component.instance_id);
            ImGui::PopStyleColor();
            ImGui::EndPopup();
        }
        if (open) {
            ImGui::Indent(12.0f);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.63f, 0.74f, 0.85f, 1.0f));
            ImGui::TextWrapped("%s", component.type_name.c_str());
            ImGui::PopStyleColor();
            render_type_details("Runtime Type", component.assembly_name, component.namespace_name,
                                component.class_name, component.type_name, false);
            if (!component.metadata) {
                enqueue_simple(CommandKind::LoadComponentMetadata, component.instance_id);
                ImGui::TextDisabled("Loading member metadata...");
            } else {
                const ComponentInfo::Metadata &metadata = *component.metadata;
                const ComponentInfo::LiveValues *live = component.live_values.get();
                CodeContext component_code = code_context(component.assembly_name, component.namespace_name,
                                                           component.class_name, component.type_name);
                component_code.game_object_name = info.name;
                if (component.metadata_unavailable) {
                    ImGui::TextColored(ImVec4(0.78f, 0.42f, 0.38f, 1.0f), "%s",
                                       component.metadata_error.empty()
                                           ? "Reflection failed for this component."
                                           : component.metadata_error.c_str());
                    if (ImGui::SmallButton("Retry metadata load"))
                        enqueue_simple(CommandKind::LoadComponentMetadata, component.instance_id);
                } else if (!component.metadata_error.empty()) {
                    ImGui::TextColored(ImVec4(0.72f, 0.60f, 0.42f, 1.0f), "Metadata diagnostics: %s",
                                       component.metadata_error.c_str());
                }
                if (component.dynamic_bridge.detected) {
                    ImGui::SeparatorText("Dynamic Script Bridge");
                    ImGui::TextDisabled("Bridge component: %s", component.type_name.c_str());
					if (!component.dynamic_bridge.behaviour_type.empty())
						ImGui::TextColored(ImVec4(0.60f, 0.68f, 0.60f, 1.0f), "Behaviour: %s",
														   component.dynamic_bridge.behaviour_type.c_str());
					else if (!component.dynamic_bridge.type_getter.empty())
						ImGui::TextDisabled("Behaviour type has not been read yet.");
					if (component.dynamic_bridge.type_getter_method_index >= 0 &&
						static_cast<std::size_t>(component.dynamic_bridge.type_getter_method_index) < metadata.methods.size()) {
						const ComponentInfo::Method& getter = metadata.methods[static_cast<std::size_t>(component.dynamic_bridge.type_getter_method_index)];
						ImGui::BeginDisabled(!getter.runtime_callable);
						if (ImGui::SmallButton("Read behaviour type"))
							enqueue_method_invoke(component.instance_id, component.dynamic_bridge.type_getter_method_index, getter);
						ImGui::EndDisabled();
						if (!getter.runtime_callable && ImGui::IsItemHovered())
							ImGui::SetTooltip("%s", getter.capability_reason.c_str());
					}
					auto render_bridge_method = [&](int method_index, const char* group) {
						if (method_index < 0 || static_cast<std::size_t>(method_index) >= metadata.methods.size())
							return;
						const ComponentInfo::Method& method = metadata.methods[static_cast<std::size_t>(method_index)];
						ImGui::PushID(method_index);
						std::string signature = method.name + " (";
						for (std::size_t parameter = 0; parameter < method.parameter_types.size(); ++parameter) {
							if (parameter)
								signature += ", ";
							signature += method.parameter_types[parameter];
						}
						signature += ")";
						const bool open = ImGui::TreeNode("##bridge-method", "%s: %s", group, signature.c_str());
						ImGui::SameLine();
						ImGui::TextDisabled("%s", method.return_type.c_str());
						if (!method.runtime_callable) {
							ImGui::SameLine();
							ImGui::TextDisabled("metadata only");
						}
						if (open) {
							if (method.uses_generic_parameter) {
								MemberBuffer& generic_type = member_buffer(generic_type_key(component.instance_id,
									static_cast<std::size_t>(method_index)));
								ImGui::SetNextItemWidth(-1.0f);
								render_generic_type_input(snapshot, "##bridge-generic-type", generic_type.text);
							}
							for (std::size_t parameter = 0; parameter < method.parameter_types.size(); ++parameter) {
								const std::string& type = method.parameter_types[parameter];
								const std::string name = parameter < method.parameter_names.size() &&
									!method.parameter_names[parameter].empty() ? method.parameter_names[parameter]
									: "arg" + std::to_string(parameter + 1);
								ImGui::PushID(static_cast<int>(parameter));
								render_method_argument(component.instance_id, static_cast<std::size_t>(method_index),
									parameter, type, name, 0, &snapshot.managed_references);
								ImGui::PopID();
							}
							const MethodTracer::Snapshot* trace = trace_for_method(snapshot.method_traces, method);
							if (trace && trace->active) {
								if (ImGui::SmallButton("Stop tracing"))
									enqueue_method_trace(component.instance_id, method_index, false);
							} else {
								render_trace_button([&](bool capture_return) {
									enqueue_method_trace(component.instance_id, method_index, true, false, 0,
										capture_return);
								});
							}
							ImGui::SameLine();
							ImGui::BeginDisabled(!invokable_method(method));
							if (ImGui::SmallButton("Read data"))
								enqueue_method_invoke(component.instance_id, method_index, method);
							ImGui::EndDisabled();
							render_method_result(snapshot, component.instance_id, static_cast<std::size_t>(method_index));
							ImGui::TreePop();
						}
						ImGui::PopID();
					};
					if (!component.dynamic_bridge.serialized_data_method_indices.empty()) {
						ImGui::TextDisabled("Serialized data:");
						for (const int method_index : component.dynamic_bridge.serialized_data_method_indices)
							render_bridge_method(method_index, "Data");
					}
					if (!component.dynamic_bridge.object_reference_method_indices.empty()) {
						ImGui::TextDisabled("Object references:");
						for (const int method_index : component.dynamic_bridge.object_reference_method_indices)
							render_bridge_method(method_index, "Object");
					}
					if (component.dynamic_bridge.serialized_data_method_indices.empty() &&
						component.dynamic_bridge.object_reference_method_indices.empty() &&
						component.dynamic_bridge.type_getter_method_index < 0)
						ImGui::TextDisabled("No bridge accessors were found. Inspect the component members below.");
                    if (!component.dynamic_bridge.diagnostic.empty())
                        ImGui::TextColored(ImVec4(0.72f, 0.60f, 0.42f, 1.0f), "Bridge diagnostics: %s",
                                           component.dynamic_bridge.diagnostic.c_str());
					ImGui::TextDisabled("Returned objects and arrays can be opened in the Object Inspector.");
                }
                std::array<char, 128> &filter_buffer = component_filter(component.instance_id);
                if (!fixed_filter) {
                    const float clear_width = ImGui::CalcTextSize("Clear").x + ImGui::GetStyle().FramePadding.x * 2.0f;
                    ImGui::SetNextItemWidth(std::max(80.0f, ImGui::GetContentRegionAvail().x - clear_width -
                        ImGui::GetStyle().ItemSpacing.x));
                    ImGui::InputTextWithHint("##member-filter", "Search name, type, owner, parameter...",
                                              filter_buffer.data(), filter_buffer.size());
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Clear"))
                        filter_buffer.fill('\0');
                }
                const std::string_view filter(fixed_filter ? fixed_filter : filter_buffer.data());
                const auto visible_member_count = [&](const auto &members) {
                    return static_cast<std::size_t>(
                        std::count_if(members.begin(), members.end(), [&](const auto &member) {
                            const bool declared_here =
                                member.declaring_type.empty() || member.declaring_type == component.type_name;
							return (show_inherited || declared_here) &&
								   member_matches_filter(member.name, member.type_name, member.declaring_type, filter);
                        }));
                };
                const auto visible_method_count = [&] {
                    return static_cast<std::size_t>(
                        std::count_if(metadata.methods.begin(), metadata.methods.end(), [&](const auto &method) {
                            const bool declared_here =
                                method.declaring_type.empty() || method.declaring_type == component.type_name;
							return (show_inherited || declared_here) && method_matches_filter(method, filter);
                        }));
                };
                auto member_table = [&](const char *id, const auto &members, const auto *values, CommandKind command,
                                        bool properties) {
                    if (!ImGui::BeginTable(id, 2,
                                           ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp |
                                               ImGuiTableFlags_NoPadOuterX))
                        return;
                    ImGui::TableSetupColumn("Member", ImGuiTableColumnFlags_WidthFixed, 190.0f);
                    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
                    std::vector<std::size_t> visible_members;
                    visible_members.reserve(members.size());
                    for (std::size_t index = 0; index < members.size(); ++index) {
                        const bool declared_here = members[index].declaring_type.empty() ||
                                                   members[index].declaring_type == component.type_name;
                        if ((show_inherited || declared_here) &&
							member_matches_filter(members[index].name, members[index].type_name,
								members[index].declaring_type, filter))
                            visible_members.push_back(index);
                    }
                    ImGuiListClipper clipper;
                    clipper.Begin(static_cast<int>(visible_members.size()), ImGui::GetFrameHeightWithSpacing());
                    while (clipper.Step())
                        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                            const std::size_t index = visible_members[static_cast<std::size_t>(row)];
                            const auto &member = members[index];
                            const auto *value = values && index < values->size() ? &(*values)[index] : nullptr;
                            ImGui::PushID(static_cast<int>(index));
                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(0);
                            ImGui::TextUnformatted(member.name.c_str());
                            // Watching lives in the row menu now; a per-row button
                            // cost every member a column of its own.
                            const Snapshot::FieldWatch *row_watch =
                                field_watch_for(snapshot, component.instance_id, index, 0, properties);
                            const bool watching = row_watch && row_watch->active;
                            const auto watch_menu_item = [&] {
                                if (ImGui::MenuItem(watching ? "Stop watching" : "Watch this member"))
                                    enqueue_field_watch(component.instance_id, static_cast<int>(index), !watching, 0,
                                                        properties);
                                ImGui::Separator();
                            };
                            if constexpr (requires { member.can_write; })
                                render_property_context_menu(member, component_code, watch_menu_item);
                            else
                                render_field_context_menu(member, component_code, watch_menu_item);
                            if (watching)
                                ImGui::SameLine(), ImGui::TextDisabled("watching");
                            if (ImGui::IsItemHovered())
                                ImGui::SetTooltip("%s\nRight-click to watch, copy or generate code", member.type_name.c_str());
                            if constexpr (requires { member.is_static; }) {
                                if (member.is_static)
                                    ImGui::SameLine(), ImGui::TextDisabled("static");
                            }
							if constexpr (requires { member.is_read_only; }) {
								if (member.is_read_only)
									ImGui::SameLine(), ImGui::TextDisabled("ro");
							}
							if (!member.runtime_safe) {
								ImGui::SameLine();
								ImGui::TextDisabled("metadata only");
							}
                            if constexpr (requires { member.can_write; }) {
                                ImGui::SameLine(), ImGui::TextDisabled("%s", member.can_write ? "rw" : "ro");
                            }
                            ImGui::TableSetColumnIndex(1);
                            bool writable = true;
                            if constexpr (requires { member.can_write; })
                                writable = member.can_write;
							else if constexpr (requires { member.is_read_only; })
								writable = !member.is_read_only;
                            const std::uint64_t key = (static_cast<std::uint64_t>(component.instance_id) << 32) |
                                                      (properties ? 0x80000000ull : 0ull) | index;
                            const auto *reference = properties ? (live && index < live->property_references.size()
                                                                      ? &live->property_references[index]
                                                                      : nullptr)
                                                               : (live && index < live->field_references.size()
                                                                      ? &live->field_references[index]
                                                                      : nullptr);
                            render_live_value(command, component.instance_id, static_cast<int>(index), value, writable,
                                              key, reference, false, live_data,
                                              snapshot.locked_member_keys.contains(key), true, 0, member.runtime_safe,
                                              member.capability_reason, &snapshot.managed_references);
							render_member_write_result(snapshot, component.instance_id, index, properties);
                            ImGui::PopID();
                        }
                    clipper.End();
                    ImGui::EndTable();
                };

                const std::size_t visible_fields = visible_member_count(metadata.fields);
                const std::size_t visible_properties = visible_member_count(metadata.properties);
                const std::size_t visible_methods = visible_method_count();
                // One member kind at a time. Fields, properties and methods used
                // to open as three stacked tables, which is what buried the
                // member you were actually looking at.
                const std::size_t visible_counts[3] = {visible_fields, visible_properties, visible_methods};
                int &active_member_tab = component_member_tab(component.instance_id);
                int forced_member_tab = -1;
                // A search that only matches methods should land you on methods.
                if (!filter.empty() && visible_counts[active_member_tab] == 0)
                    for (int candidate = 0; candidate < 3; ++candidate)
                        if (visible_counts[candidate] > 0) {
                            forced_member_tab = candidate;
                            break;
                        }
                const auto member_tab_flags = [&](int slot) {
                    return forced_member_tab == slot ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
                };
                char member_tab_label[96];
                if (ImGui::BeginTabBar("##member-kinds", ImGuiTabBarFlags_FittingPolicyScroll)) {
                std::snprintf(member_tab_label, sizeof(member_tab_label), "Fields (%zu / %zu)###fields",
                              visible_fields, metadata.fields.size());
                if (ImGui::BeginTabItem(member_tab_label, nullptr, member_tab_flags(0))) {
                    active_member_tab = 0;
                    member_table("##fields", metadata.fields, live ? &live->fields : nullptr,
                                 CommandKind::SetFieldValue, false);
                    ImGui::EndTabItem();
                }
                std::snprintf(member_tab_label, sizeof(member_tab_label), "Properties (%zu / %zu)###properties",
                              visible_properties, metadata.properties.size());
                if (ImGui::BeginTabItem(member_tab_label, nullptr, member_tab_flags(1))) {
                    active_member_tab = 1;
                    member_table("##properties", metadata.properties, live ? &live->properties : nullptr,
                                 CommandKind::SetPropertyValue, true);
                    ImGui::EndTabItem();
                }
                std::snprintf(member_tab_label, sizeof(member_tab_label), "Methods (%zu / %zu)###methods",
                              visible_methods, metadata.methods.size());
                if (ImGui::BeginTabItem(member_tab_label, nullptr, member_tab_flags(2))) {
                    active_member_tab = 2;
                    for (std::size_t index = 0; index < metadata.methods.size(); ++index) {
                        const auto &method = metadata.methods[index];
                        const bool declared_here =
                            method.declaring_type.empty() || method.declaring_type == component.type_name;
                        if (!show_inherited && !declared_here)
                            continue;
						if (!method_matches_filter(method, filter))
                            continue;
                        ImGui::PushID(static_cast<int>(index));
                        std::string parameters = "(";
                        for (std::size_t parameter = 0; parameter < method.parameter_types.size(); ++parameter) {
                            if (parameter)
                                parameters += ", ";
                            parameters += method.parameter_types[parameter];
                        }
                        parameters += ")";
                        const std::string method_label = method.name + " " + parameters;
                        const bool method_open = ImGui::TreeNode("##method", "%s", method_label.c_str());
                        render_method_context_menu(method, component_code);
                        ImGui::SameLine();
                        ImGui::TextDisabled("%s", method.return_type.c_str());
						if (!method.runtime_callable) {
							ImGui::SameLine();
							ImGui::TextDisabled("metadata only");
						}
                        if (method_open && method.uses_generic_parameter) {
                            MemberBuffer& generic_type = member_buffer(generic_type_key(component.instance_id, index));
                            ImGui::SetNextItemWidth(-1.0f);
                            render_generic_type_input(snapshot, "##generic-type", generic_type.text);
                            ImGui::TextDisabled("Example: bolt.user.dll:Photon.Bolt.IPlayerState");
                        }
                        if (method_open && !method.parameter_types.empty()) {
                            ImGui::Indent();
                            for (std::size_t parameter = 0; parameter < method.parameter_types.size(); ++parameter) {
                                const std::string &type = method.parameter_types[parameter];
                                const std::string name = parameter < method.parameter_names.size() &&
                                                                 !method.parameter_names[parameter].empty()
                                                             ? method.parameter_names[parameter]
                                                             : "arg" + std::to_string(parameter + 1);
                                ImGui::PushID(static_cast<int>(parameter));
                                render_method_argument(component.instance_id, index, parameter, type, name, 0,
                                                       &snapshot.managed_references);
                                ImGui::PopID();
                            }
                            ImGui::Unindent();
                        }
                        if (method_open) {
                            const MethodTracer::Snapshot *trace = trace_for_method(snapshot.method_traces, method);
                            if (trace && trace->active) {
                                if (ImGui::SmallButton("Stop tracing"))
                                    enqueue_method_trace(component.instance_id, static_cast<int>(index), false);
                            } else {
                                render_trace_button([&](bool capture_return) {
                                    enqueue_method_trace(component.instance_id, static_cast<int>(index), true, false, 0,
                                                         capture_return);
                                });
                            }
                            ImGui::SameLine();
                            ImGui::BeginDisabled(!invokable_method(method));
                            if (ImGui::SmallButton("Execute"))
                                enqueue_method_invoke(component.instance_id, static_cast<int>(index), method);
                            ImGui::EndDisabled();
                            render_method_result(snapshot, component.instance_id, index);
                            ImGui::TreePop();
                        }
                        ImGui::PopID();
                    }
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
                }
            }
            ImGui::Unindent(12.0f);
        }
        if (open)
            ImGui::TreePop();
        ImGui::Spacing();
        ImGui::PopID();
    }
    if (!info.component_query_error.empty()) {
        ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.38f, 1.0f), "Component query failed");
        ImGui::TextWrapped("%s", info.component_query_error.c_str());
    }
    else if (info.components.empty())
        ImGui::TextDisabled("No additional components");
}

void add_component(int instance_id, std::string image, std::string namespc, std::string class_name) {
    Command command{};
    command.kind = CommandKind::AddComponent;
    command.instance_id = instance_id;
    command.image = std::move(image);
    command.namespc = std::move(namespc);
    command.class_name = std::move(class_name);
    RuntimeModel::instance().enqueue(std::move(command));
}

void request_component_class_catalog() {
    RuntimeModel::instance().enqueue(Command{.kind = CommandKind::LoadComponentClassCatalog});
}

void render_add_component_popup(const InspectorInfo &info, const Snapshot &snapshot) {
    const float width = std::min(280.0f, ImGui::GetContentRegionAvail().x);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (ImGui::GetContentRegionAvail().x - width) * 0.5f);
    if (ImGui::Button("Add Component", ImVec2(width, 0.0f)))
        ImGui::OpenPopup("##add-component");

    if (!ImGui::BeginPopup("##add-component"))
        return;

    struct CommonComponent {
        const char *label;
        const char *type;
    };
    static constexpr CommonComponent common[] = {
        {"Rigidbody", "Rigidbody"},
        {"Box Collider", "BoxCollider"},
        {"Sphere Collider", "SphereCollider"},
        {"Audio Source", "AudioSource"},
        {"Light", "Light"},
        {"Camera", "Camera"},
    };
    ImGui::TextDisabled("Common");
    for (const CommonComponent &component : common) {
        if (ImGui::MenuItem(component.label)) {
            add_component(info.instance_id, "", "UnityEngine", component.type);
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::Separator();
    ImGui::TextDisabled("Class Browser");
    ImGui::TextDisabled("Choose from loaded assemblies");
    AddComponentBuffers &buffers = component_buffers();
    if (!snapshot.component_class_catalog && !buffers.catalog_requested) {
        request_component_class_catalog();
        buffers.catalog_requested = true;
    }

    if (!snapshot.component_class_catalog) {
        ImGui::TextDisabled("Scanning loaded assemblies for addable components...");
    } else {
        ImGui::SetNextItemWidth(310.0f);
        ImGui::InputTextWithHint("##component-class-search", "Search class, namespace or assembly...",
                                 buffers.class_search.data(), buffers.class_search.size());
        ImGui::SameLine();
        if (ImGui::SmallButton("Rescan")) {
            request_component_class_catalog();
            buffers.catalog_requested = true;
        }

        const std::string_view query(buffers.class_search.data());
        const ComponentClassCatalog &catalog = *snapshot.component_class_catalog;
        constexpr std::size_t kMaxClassBrowserResults = 96;
        std::size_t shown = 0;
        if (ImGui::BeginChild("##component-class-browser", ImVec2(310.0f, 185.0f), true)) {
            for (const ComponentClassInfo &entry : catalog.classes) {
                if (!query.empty() && !contains_case_insensitive(entry.full_name, query) &&
                    !contains_case_insensitive(entry.image, query))
                    continue;
                if (shown++ >= kMaxClassBrowserResults) {
                    ImGui::TextDisabled("More results exist; refine the search.");
                    break;
                }
                const std::string label = entry.full_name + "##component-class-" + entry.image;
                if (ImGui::Selectable(label.c_str())) {
                    add_component(info.instance_id, entry.image, entry.namespc, entry.class_name);
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Assembly: %s\nNamespace: %s\nClass: %s", entry.image.c_str(),
                                      entry.namespc.empty() ? "<global>" : entry.namespc.c_str(),
                                      entry.class_name.c_str());
            }
            if (shown == 0)
                ImGui::TextDisabled(query.empty() ? "No addable component classes were found."
                                                  : "No classes match this search.");
            ImGui::EndChild();
        }
        ImGui::TextDisabled("%zu addable classes indexed", catalog.classes.size());
    }

    ImGui::Separator();
    ImGui::TextDisabled("Manual entry");
    ImGui::SetNextItemWidth(310.0f);
    ImGui::InputTextWithHint("##component-image", "Assembly image (optional)", buffers.image.data(),
                             buffers.image.size());
    ImGui::SetNextItemWidth(310.0f);
    ImGui::InputTextWithHint("##component-namespace", "Namespace", buffers.namespc.data(), buffers.namespc.size());
    ImGui::SetNextItemWidth(310.0f);
    ImGui::InputTextWithHint("##component-class", "Class name", buffers.class_name.data(), buffers.class_name.size());
    const bool can_add = buffers.class_name[0] != '\0';
    ImGui::BeginDisabled(!can_add);
    if (ImGui::Button("Add", ImVec2(310.0f, 0.0f))) {
        add_component(info.instance_id, buffers.image.data(), buffers.namespc.data(), buffers.class_name.data());
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::EndPopup();
}

} // namespace
void render_current_inspector(const Snapshot &snapshot) {
    ImGui::BeginChild("##inspector-scroll", ImVec2(0.0f, 0.0f), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);

    const InspectorInfo &info = snapshot.inspector;
    if (!info.valid) {
        const ImVec2 available = ImGui::GetContentRegionAvail();
        const char *message = "Select a GameObject from the Hierarchy to inspect it.";
        const ImVec2 size = ImGui::CalcTextSize(message);
        ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + std::max(0.0f, (available.x - size.x) * 0.5f),
                                   ImGui::GetCursorPosY() + std::max(24.0f, (available.y - size.y) * 0.35f)));
        ImGui::TextDisabled("%s", message);
        ImGui::EndChild();
        return;
    }

    render_identity(info);
    ImGui::Separator();
    if (ImGui::SmallButton("Focus"))
        enqueue_simple(CommandKind::FocusSelected, info.instance_id);
    ImGui::SameLine();
    ImGui::BeginDisabled(!snapshot.camera_focus_active);
    if (ImGui::SmallButton("Return"))
        enqueue_simple(CommandKind::RestoreCamera, 0);
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::SmallButton("Save Reference")) {
        Command command{};
        command.kind = CommandKind::PinManagedReference;
        command.instance_id = info.instance_id;
        RuntimeModel::instance().enqueue(std::move(command));
    }
    ImGui::SameLine();
    if (info.camera_distance_valid)
        ImGui::TextDisabled("%.1f units", info.camera_distance);
    else
        ImGui::TextDisabled("distance unavailable");
    if (!snapshot.managed_references.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("Saved refs: %zu", snapshot.managed_references.size());
        ImGui::SameLine();
        if (ImGui::SmallButton("Saved refs"))
            ImGui::OpenPopup("##saved-reference-shelf");
        if (ImGui::BeginPopup("##saved-reference-shelf")) {
            ImGui::TextUnformatted("Saved runtime references");
            for (const ManagedReferenceInfo& reference : snapshot.managed_references) {
                ImGui::PushID(static_cast<int>(reference.token));
                ImGui::TextUnformatted(reference.display.c_str());
                ImGui::SameLine();
                if (ImGui::SmallButton("Inspect"))
                    enqueue_reference_inspection(reference.token);
                ImGui::SameLine();
                if (ImGui::SmallButton("Remove")) {
                    Command command{};
                    command.kind = CommandKind::ReleaseManagedReference;
                    command.reference_token = reference.token;
                    RuntimeModel::instance().enqueue(std::move(command));
                }
                ImGui::TextDisabled("%s | %s", reference.type_name.c_str(), reference.source.c_str());
                ImGui::PopID();
            }
            ImGui::EndPopup();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear saved")) {
            Command command{};
            command.kind = CommandKind::ClearManagedReferences;
            RuntimeModel::instance().enqueue(std::move(command));
        }
    }
    render_transform(info, snapshot.transform_clipboard);
    std::array<char, 128>& filter = component_filter(info.instance_id);
    bool& show_inherited = component_show_inherited(info.instance_id);
    char components_heading[64];
    std::snprintf(components_heading, sizeof(components_heading), "Components (%zu)", info.components.size());
    ImGui::SeparatorText(components_heading);
    ImGui::SetNextItemWidth(std::max(120.0f, ImGui::GetContentRegionAvail().x - 135.0f));
    ImGui::InputTextWithHint("##inspector-member-search", "Search Inspector members...", filter.data(), filter.size());
    ImGui::SameLine();
    ImGui::Checkbox("Debug", &show_inherited);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Show inherited runtime members");
    if (ImGui::SmallButton("Expand all"))
        component_bulk_toggle() = 1;
    ImGui::SameLine();
    if (ImGui::SmallButton("Collapse all"))
        component_bulk_toggle() = -1;
    if (filter[0] != '\0') {
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear search"))
            filter.fill('\0');
        ImGui::SameLine();
        ImGui::TextDisabled("showing matching components only");
    }
    render_components(info, snapshot, 0, filter.data(), show_inherited);
    render_add_component_popup(info, snapshot);
    ImGui::EndChild();
}
void render_inspector(const Snapshot &snapshot) {
    render_current_inspector(snapshot);
}

} // namespace Explorer::UI

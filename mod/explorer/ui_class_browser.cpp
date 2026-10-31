// Copyright (c) 2026 Jadis0x. All rights reserved.
// Class Browser: assembly/class search, static state, instances and members.
#include "ui_members.h"

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
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Explorer::UI {
namespace {

struct ClassBrowserUiState {
    std::array<char, 192> search{};
    std::array<char, 128> assembly_filter{};
    std::array<char, 128> member_filter{};
    BrowserClassInfo selected{};
    std::uint64_t target_token = 0;
    bool catalog_requested = false;
    bool components_only = false;
    bool unity_objects_only = false;
    bool show_interfaces = true;
    bool show_value_types = true;
    bool show_abstract = true;
    bool include_all_loaded = true;
    const ClassBrowserCatalog *cached_catalog = nullptr;
    std::string cached_filter_key;
    std::vector<std::size_t> matching_indices;
    MethodTracer::CallerMethod pending_caller_method;
    std::string focused_method;
    std::uintptr_t focused_method_address = 0;
    std::string inspected_method;
    int active_member_tab = 0;
    bool force_method_tab = false;
};

ClassBrowserUiState &class_browser_ui_state() {
    static ClassBrowserUiState state;
    return state;
}

bool same_browser_class(const BrowserClassInfo &left, const BrowserClassInfo &right) {
    if (left.metadata_address != 0 && right.metadata_address != 0)
        return left.metadata_address == right.metadata_address;
    return left.image == right.image && left.namespc == right.namespc &&
           left.class_name == right.class_name;
}

std::string generated_source_member(std::string_view value) {
    if (value.size() < 3 || value.front() != '<')
        return {};
    const std::size_t closing = value.find('>');
    return closing == std::string_view::npos || closing == 1
        ? std::string{} : std::string(value.substr(1, closing - 1));
}

void render_generated_type_context(const ClassBrowserUiState &state) {
    const std::string_view name = state.selected.class_name;
    const bool closure = name.find("<>c__DisplayClass") != std::string_view::npos;
    const bool state_machine = name.size() > 4 && name.front() == '<' &&
        name.find(">d__") != std::string_view::npos;
    const bool lambda_cache = name == "<>c" || name.rfind("<>c`", 0) == 0;
    if (!closure && !state_machine && !lambda_cache)
        return;

    ImGui::TextColored(Unity::Skin::action_amber, "%s",
        closure ? "Compiler-generated closure" :
        state_machine ? "Compiler-generated async/iterator state machine" :
                        "Compiler-generated lambda cache");
    if (!state.selected.declaring_type_name.empty())
        ImGui::TextDisabled("Declared in: %s", state.selected.declaring_type_name.c_str());
    std::string source_member = generated_source_member(
        state_machine ? name : std::string_view(state.inspected_method));
    if (!source_member.empty())
        ImGui::TextDisabled("Source member: %s", source_member.c_str());
    ImGui::TextWrapped("%s", closure
        ? "Fields are captured locals and references. Find a live instance, then use it as the target to inspect those values."
        : state_machine
            ? "MoveNext contains the generated body; fields hold execution state and captured locals."
            : "This type stores delegates used by compiler-generated lambdas.");
}


// Fields and properties found through the Class Browser are watchable the same
// way Inspector members are, as long as a live target is selected: the watch
// reads through that instance, so without one there is nothing to sample.
void render_class_member_watch(const Snapshot &snapshot, std::size_t member_index, bool property,
                               std::uint64_t target_token, bool instance_readable, const char *unavailable_reason) {
    const Snapshot::FieldWatch *watch = field_watch_for(snapshot, 0, member_index, target_token, property);
    const bool watching = watch && watch->active;
    const bool available = target_token != 0 && instance_readable;
    ImGui::SameLine();
    ImGui::BeginDisabled(!available);
    if (ImGui::SmallButton(watching ? "Unwatch" : "Watch"))
        enqueue_field_watch(0, static_cast<int>(member_index), !watching, target_token, property, true);
    ImGui::EndDisabled();
    if (!ImGui::IsItemHovered())
        return;
    const char *reason = !instance_readable ? unavailable_reason
                         : target_token == 0
                             ? "Select a live target above: a watch reads the member through an instance"
                         : watching ? "Stop reporting changes to this member"
                                    : "Report every change to this member in Value Watches";
    ImGui::SetTooltip("%s", reason);
}

} // namespace

bool &class_browser_window_requested() {
    static bool requested = false;
    return requested;
}

void inspect_class_browser_method(const MethodTracer::CallerMethod &method) {
    if (!method.inspectable())
        return;
    ClassBrowserUiState &state = class_browser_ui_state();
    state.pending_caller_method = method;
    class_browser_window_requested() = true;
}

// The shell needs to know when an Object Inspector tab belongs to the browser.
std::uint64_t class_browser_target_token() {
    return class_browser_ui_state().target_token;
}

void render_class_browser(const Snapshot &snapshot) {
    ClassBrowserUiState &state = class_browser_ui_state();
    if (!snapshot.class_browser_catalog && !state.catalog_requested) {
        RuntimeModel::instance().enqueue(Command{.kind = CommandKind::LoadClassBrowserCatalog});
        state.catalog_requested = true;
    }

    ImGui::TextDisabled("Search every loaded %s type. Instance search follows Unity roots and static references.",
                        ModConfig::backend_name);
    if (!snapshot.class_browser_catalog) {
        ImGui::TextDisabled("Scanning class metadata...");
        return;
    }

    const ClassBrowserCatalog &catalog = *snapshot.class_browser_catalog;
    if (state.pending_caller_method.inspectable()) {
        const MethodTracer::CallerMethod requested = state.pending_caller_method;
        const auto same_type = [&requested](const BrowserClassInfo &entry) {
            return entry.namespc == requested.namespc && entry.class_name == requested.class_name;
        };
        auto selected = std::find_if(catalog.classes.begin(), catalog.classes.end(),
                                     [&requested](const BrowserClassInfo &entry) {
                                         return requested.class_address != 0 &&
                                                entry.metadata_address == requested.class_address;
                                     });
        if (selected == catalog.classes.end())
            selected = std::find_if(catalog.classes.begin(), catalog.classes.end(),
                                    [&requested, &same_type](const BrowserClassInfo &entry) {
                                        return same_type(entry) && entry.image == requested.image;
                                    });
        // Some backends expose an assembly display name while the catalog has
        // the image filename. The managed namespace/name pair is still exact;
        // use it only as a fallback, and only when it identifies one type.
        if (selected == catalog.classes.end()) {
            const auto fallback = std::find_if(catalog.classes.begin(), catalog.classes.end(), same_type);
            if (fallback != catalog.classes.end() &&
                std::find_if(std::next(fallback), catalog.classes.end(), same_type) == catalog.classes.end())
                selected = fallback;
        }
        BrowserClassInfo target{};
        if (selected != catalog.classes.end()) {
            target = *selected;
        } else {
            // The caller index can cover more metadata than the browser's
            // bounded catalog. Its exact class pointer still lets members be
            // loaded without falling back to an ambiguous name lookup.
            target.metadata_address = requested.class_address;
            target.image = requested.image;
            target.namespc = requested.namespc;
            target.class_name = requested.class_name;
            target.full_name = !requested.class_display_name.empty()
                ? requested.class_display_name
                : requested.namespc.empty() ? requested.class_name
                                             : requested.namespc + "." + requested.class_name;
            char pointer[32]{};
            std::snprintf(pointer, sizeof(pointer), "0x%llX",
                          static_cast<unsigned long long>(requested.class_address));
            target.pointer_text = pointer;
        }
        if (target.metadata_address != 0) {
            state.selected = target;
            state.target_token = 0;
            copy_text(state.search, target.full_name);
            copy_text(state.assembly_filter, target.image);
            copy_text(state.member_filter, requested.method_name);
            state.focused_method = requested.method_name;
            state.focused_method_address = requested.method_address;
            state.inspected_method = requested.method_name;
            state.active_member_tab = 2;
            state.force_method_tab = true;

            Command command{};
            command.kind = CommandKind::LoadClassBrowserMembers;
            command.metadata_address = target.metadata_address;
            command.image = target.image;
            command.namespc = target.namespc;
            command.class_name = target.class_name;
            RuntimeModel::instance().enqueue(std::move(command));
        }
        state.pending_caller_method = {};
    }
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##class-browser-search", "Search class or namespace...", state.search.data(),
                             state.search.size());
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##class-browser-assembly", "Assembly filter...", state.assembly_filter.data(),
                             state.assembly_filter.size());
    ImGui::Checkbox("Components only", &state.components_only);
    ImGui::SameLine();
    ImGui::Checkbox("Unity objects only", &state.unity_objects_only);
    ImGui::SameLine();
    ImGui::Checkbox("Interfaces", &state.show_interfaces);
    ImGui::SameLine();
    ImGui::Checkbox("Value types", &state.show_value_types);
    ImGui::SameLine();
    ImGui::Checkbox("Abstract", &state.show_abstract);

    const std::string_view search(state.search.data());
    const std::string_view assembly_filter(state.assembly_filter.data());
    std::string filter_key;
    filter_key.reserve(search.size() + assembly_filter.size() + 8);
    filter_key.append(search);
    filter_key.push_back('\n');
    filter_key.append(assembly_filter);
    filter_key.push_back(static_cast<char>(state.components_only));
    filter_key.push_back(static_cast<char>(state.unity_objects_only));
    filter_key.push_back(static_cast<char>(state.show_interfaces));
    filter_key.push_back(static_cast<char>(state.show_value_types));
    filter_key.push_back(static_cast<char>(state.show_abstract));
    if (state.cached_catalog != &catalog || state.cached_filter_key != filter_key) {
        state.cached_catalog = &catalog;
        state.cached_filter_key = std::move(filter_key);
        state.matching_indices.clear();
        constexpr std::size_t kMaxCachedClasses = 257;
        for (std::size_t index = 0; index < catalog.classes.size(); ++index) {
            const BrowserClassInfo &entry = catalog.classes[index];
            if (state.components_only && !entry.is_component)
                continue;
            if (state.unity_objects_only && !entry.is_unity_object)
                continue;
            if (!state.show_interfaces && entry.is_interface)
                continue;
            if (!state.show_value_types && (entry.is_value_type || entry.is_enum))
                continue;
            if (!state.show_abstract && entry.is_abstract)
                continue;
            if (!search.empty() && !contains_case_insensitive(entry.full_name, search) &&
                !contains_case_insensitive(entry.image, search) &&
                !contains_case_insensitive(Labels::search_text(Labels::Kind::Type, entry.full_name), search))
                continue;
            if (!assembly_filter.empty() && !contains_case_insensitive(entry.image, assembly_filter))
                continue;
            state.matching_indices.push_back(index);
            if (state.matching_indices.size() >= kMaxCachedClasses)
                break;
        }
    }

    constexpr std::size_t kMaxShownClasses = 256;
    const std::size_t visible_count = std::min(kMaxShownClasses, state.matching_indices.size());
    const float list_row = ImGui::GetTextLineHeightWithSpacing();
    const float list_height =
        std::clamp(static_cast<float>(std::max<std::size_t>(visible_count, 1) + 1) * list_row +
                       ImGui::GetStyle().WindowPadding.y * 2.0f,
                   list_row * 4.0f, list_row * 14.0f);
    ImGui::BeginChild("##class-browser-results", ImVec2(0.0f, list_height), true);
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(visible_count));
    while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            const BrowserClassInfo &entry = catalog.classes[state.matching_indices[static_cast<std::size_t>(row)]];
            const bool selected = same_browser_class(state.selected, entry);
            const std::string label =
                type_name_text(entry.full_name) + "##class-browser-" + entry.image + "-" + entry.pointer_text;
            if (ImGui::Selectable(label.c_str(), selected)) {
                if (!selected) {
                    state.target_token = 0;
                    state.member_filter.fill('\0');
                    state.focused_method.clear();
                    state.focused_method_address = 0;
                    state.inspected_method.clear();
                    state.active_member_tab = 0;
                    state.force_method_tab = false;
                }
                state.selected = entry;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Assembly: %s\n%s%s%s%s", entry.image.c_str(),
                                  entry.is_component ? "Component\n" : "", entry.is_interface ? "Interface\n" : "",
                                  entry.is_static ? "Static class\n" : "",
                                  entry.parent_name.empty() ? "" : entry.parent_name.c_str());
        }
    }
    if (state.matching_indices.empty())
        ImGui::TextDisabled("No classes match the current filters.");
    else if (state.matching_indices.size() > kMaxShownClasses)
        ImGui::TextDisabled("More matches exist; refine the search.");
    ImGui::EndChild();
    ImGui::TextDisabled("%zu indexed loaded types", catalog.classes.size());

    if (state.selected.class_name.empty())
        return;
    ImGui::SeparatorText("Selected Class");
    ImGui::TextColored(ImVec4(0.62f, 0.72f, 0.82f, 1.0f), "%s", type_name_text(state.selected.full_name).c_str());
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Right-click to label this class");
    if (ImGui::BeginPopupContextItem("##selected-class-label")) {
        render_label_editor(Labels::Kind::Type, state.selected.full_name);
        ImGui::EndPopup();
    }
    ImGui::TextDisabled("Assembly: %s", state.selected.image.c_str());
    if (!state.selected.parent_name.empty())
        ImGui::TextDisabled("Base: %s", state.selected.parent_name.c_str());
    if (!state.selected.interfaces.empty()) {
        std::string interfaces;
        for (const std::string &interface_name : state.selected.interfaces) {
            if (!interfaces.empty())
                interfaces += ", ";
            interfaces += interface_name;
        }
        ImGui::TextDisabled("Interfaces: %s", interfaces.c_str());
    }
    std::string kind;
    if (state.selected.is_static)
        kind = "static class";
    else if (state.selected.is_interface)
        kind = "interface";
    else if (state.selected.is_enum)
        kind = "enum";
    else if (state.selected.is_value_type)
        kind = "value type";
    else if (state.selected.is_component)
        kind = "component";
    else if (state.selected.is_unity_object)
        kind = "Unity object";
    else
        kind = "managed class";
    ImGui::TextDisabled("Kind: %s", kind.c_str());
    render_generated_type_context(state);
    if (ImGui::SmallButton("Copy class info")) {
        const std::string details = type_details_text(state.selected.image, state.selected.namespc,
                                                      state.selected.class_name, state.selected.full_name);
        ImGui::SetClipboardText(details.c_str());
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Copy type addr"))
        ImGui::SetClipboardText(state.selected.pointer_text.c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton(state.selected.is_static ? "View static state" : "View static fields")) {
        Command command{};
        command.kind = CommandKind::LoadClassBrowserStaticState;
        command.metadata_address = state.selected.metadata_address;
        command.image = state.selected.image;
        command.namespc = state.selected.namespc;
        command.class_name = state.selected.class_name;
        RuntimeModel::instance().enqueue(std::move(command));
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("View members")) {
        Command command{};
        command.kind = CommandKind::LoadClassBrowserMembers;
        command.metadata_address = state.selected.metadata_address;
        command.image = state.selected.image;
        command.namespc = state.selected.namespc;
        command.class_name = state.selected.class_name;
        RuntimeModel::instance().enqueue(std::move(command));
    }
    ImGui::SameLine();
    ImGui::Checkbox("Include inactive / assets", &state.include_all_loaded);
    ImGui::SameLine();
    if (state.selected.is_static) {
        ImGui::BeginDisabled();
        ImGui::SmallButton("Find instances");
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Static classes cannot have instances. Use View static state.");
    } else if (ImGui::SmallButton("Find instances")) {
        state.target_token = 0;
        Command command{};
        command.kind = CommandKind::FindClassInstances;
        command.metadata_address = state.selected.metadata_address;
        command.image = state.selected.image;
        command.namespc = state.selected.namespc;
        command.class_name = state.selected.class_name;
        command.int_value = state.selected.is_component ? 1 : 0;
        command.bool_value = state.include_all_loaded;
        command.class_is_unity_object = state.selected.is_unity_object;
        RuntimeModel::instance().enqueue(std::move(command));
    }

    if (same_browser_class(snapshot.class_browser_members_query, state.selected) && snapshot.class_browser_members) {
        const ComponentInfo::Metadata &members = *snapshot.class_browser_members;
        const CodeContext class_code = code_context(state.selected.image, state.selected.namespc,
                                                    state.selected.class_name, state.selected.full_name);
        ImGui::SeparatorText("Members");
        ImGui::TextDisabled("Select a live target below to read, edit, execute and trace its runtime members.");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##class-member-filter", "Filter member name, type, owner or parameter...",
                                 state.member_filter.data(), state.member_filter.size());
        const std::string_view member_filter = state.member_filter.data();
        const auto visible_member_count = [&](const auto &collection) {
            return static_cast<std::size_t>(std::count_if(
                collection.begin(), collection.end(), [&](const auto &member) {
                    return member_matches_filter(member.name, member.type_name,
                                                 member.declaring_type, member_filter);
                }));
        };
        const std::size_t visible_fields = visible_member_count(members.fields);
        const std::size_t visible_properties = visible_member_count(members.properties);
        const bool focus_by_address = state.focused_method_address != 0 &&
            std::any_of(members.methods.begin(), members.methods.end(), [&](const ComponentInfo::Method &method) {
                return method.metadata_address == state.focused_method_address;
            });
        const std::size_t visible_methods = static_cast<std::size_t>(std::count_if(
            members.methods.begin(), members.methods.end(), [&](const ComponentInfo::Method &method) {
                const bool focused = focus_by_address
                    ? method.metadata_address == state.focused_method_address
                    : !state.focused_method.empty() && method.name == state.focused_method;
                return focused || method_matches_filter(method, member_filter);
            }));
        const std::size_t visible_counts[3] = {visible_fields, visible_properties, visible_methods};
        if (state.active_member_tab < 0 || state.active_member_tab > 2)
            state.active_member_tab = 0;
        int forced_member_tab = state.force_method_tab ? 2 : -1;
        if (forced_member_tab < 0 && !member_filter.empty() &&
            visible_counts[state.active_member_tab] == 0) {
            for (int candidate = 0; candidate < 3; ++candidate) {
                if (visible_counts[candidate] > 0) {
                    forced_member_tab = candidate;
                    break;
                }
            }
        }
        const auto member_tab_flags = [&](int slot) {
            return forced_member_tab == slot ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
        };
        const float member_row = ImGui::GetTextLineHeightWithSpacing();
        const bool static_state_below = same_browser_class(snapshot.class_browser_static_query, state.selected);
        const float members_height = std::max(member_row * 12.0f, ImGui::GetContentRegionAvail().y -
                                                                       (static_state_below ? member_row * 11.0f : 0.0f));
        ImGui::BeginChild("##class-browser-members", ImVec2(0.0f, members_height), true);
        // Class members follow the same one-kind-at-a-time layout.
        if (ImGui::BeginTabBar("##class-member-kinds", ImGuiTabBarFlags_FittingPolicyScroll)) {
        char class_tab_label[96];
        std::snprintf(class_tab_label, sizeof(class_tab_label), "Fields (%zu / %zu)###cfields",
                      visible_fields, members.fields.size());
        if (Unity::begin_member_tab(class_tab_label, Unity::Skin::action_blue, member_tab_flags(0))) {
            state.active_member_tab = 0;
            for (std::size_t field_index = 0; field_index < members.fields.size(); ++field_index) {
                const ComponentInfo::Field &field = members.fields[field_index];
                if (!member_matches_filter(field.name, field.type_name, field.declaring_type, member_filter))
                    continue;
                ImGui::PushID(static_cast<int>(field_index));
                if (field.is_static) {
                    ImGui::TextDisabled("static");
                    ImGui::SameLine();
                }
                render_member_name(Labels::Kind::Field,
                                   field.declaring_type.empty() ? state.selected.full_name : field.declaring_type,
                                   field.name);
                render_field_context_menu(field, class_code, {});
                ImGui::SameLine();
                ImGui::TextDisabled(": %s", Labels::humanize(field.type_name).c_str());
                if (!field.declaring_type.empty() && field.declaring_type != state.selected.full_name) {
                    ImGui::SameLine();
                    ImGui::TextDisabled("[%s]", Labels::humanize(field.declaring_type).c_str());
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Copy addr"))
                    ImGui::SetClipboardText(field.pointer_text.c_str());
                render_class_member_watch(snapshot, field_index, false, state.target_token, !field.is_static,
                                          "Static fields have no instance to watch; read them from Static State above");
                ImGui::PopID();
            }
            ImGui::EndTabItem();
        }
        std::snprintf(class_tab_label, sizeof(class_tab_label), "Properties (%zu / %zu)###cproperties",
                      visible_properties, members.properties.size());
        if (Unity::begin_member_tab(class_tab_label, Unity::Skin::action_green, member_tab_flags(1))) {
            state.active_member_tab = 1;
            for (std::size_t property_index = 0; property_index < members.properties.size(); ++property_index) {
                const ComponentInfo::Property &property = members.properties[property_index];
                if (!member_matches_filter(property.name, property.type_name, property.declaring_type, member_filter))
                    continue;
                ImGui::PushID(static_cast<int>(property_index));
                render_member_name(Labels::Kind::Property,
                                   property.declaring_type.empty() ? state.selected.full_name
                                                                   : property.declaring_type,
                                   property.name);
                render_property_context_menu(property, class_code, {});
                ImGui::SameLine();
                ImGui::TextDisabled(": %s  %s%s", Labels::humanize(property.type_name).c_str(),
                                    property.can_read ? "get" : "", property.can_write ? "/set" : "");
                ImGui::SameLine();
                if (ImGui::SmallButton("Copy addr"))
                    ImGui::SetClipboardText(property.pointer_text.c_str());
                render_class_member_watch(snapshot, property_index, true, state.target_token, property.can_read,
                                          "This property has no getter, so its value cannot be sampled");
                ImGui::PopID();
            }
            ImGui::EndTabItem();
        }
        std::snprintf(class_tab_label, sizeof(class_tab_label), "Methods (%zu / %zu)###cmethods",
                      visible_methods, members.methods.size());
        if (Unity::begin_member_tab(class_tab_label, Unity::Skin::action_amber, member_tab_flags(2))) {
            state.active_member_tab = 2;
            for (std::size_t method_index = 0; method_index < members.methods.size(); ++method_index) {
                const ComponentInfo::Method &method = members.methods[method_index];
                const bool focus_method = focus_by_address
                    ? method.metadata_address == state.focused_method_address
                    : !state.focused_method.empty() && method.name == state.focused_method;
                // Exact metadata navigation must win over a display-name
                // filter (notably when an invalid UTF-8 name was normalized).
                if (!focus_method && !method_matches_filter(method, member_filter))
                    continue;
                ImGui::PushID(static_cast<int>(method_index));
                std::string parameters;
                for (std::size_t index = 0; index < method.parameter_types.size(); ++index) {
                    if (!parameters.empty())
                        parameters += ", ";
                    parameters += Labels::humanize(method.parameter_types[index]);
                    if (index < method.parameter_names.size() && !method.parameter_names[index].empty())
                        parameters += " " + Labels::parameter_display(method.parameter_names[index]);
                }
                const std::string method_name = member_name_text(
                    Labels::Kind::Method, method.declaring_type.empty() ? state.selected.full_name : method.declaring_type,
                    method.name, Labels::method_signature(method.parameter_types));
                const std::string signature = (method.is_static ? "static " : "") + method_name + "(" + parameters +
                                              ") : " + Labels::humanize(method.return_type);
                if (focus_method)
                    ImGui::SetNextItemOpen(true, ImGuiCond_Always);
                const bool method_open = ImGui::TreeNode("##class-method", "%s", signature.c_str());
                if (focus_method) {
                    ImGui::SetScrollHereY(0.5f);
                    state.focused_method.clear();
                    state.focused_method_address = 0;
                    state.force_method_tab = false;
                }
                render_method_context_menu(method, class_code);
                if (!method.signature_complete) {
                    ImGui::SameLine();
                    ImGui::TextDisabled("[metadata only]");
                }
                const MethodTracer::Snapshot *row_trace = trace_for_method(snapshot.method_traces, method);
                const bool row_tracing = row_trace && row_trace->active;
                ImGui::SameLine();
                ImGui::BeginDisabled(!method.signature_complete && !row_tracing);
                if (ImGui::SmallButton(row_tracing ? "Stop tracing" : "Trace")) {
                    Command command{};
                    command.kind = CommandKind::SetMethodTrace;
                    command.metadata_address = state.selected.metadata_address;
                    command.member_index = static_cast<int>(method_index);
                    command.class_browser_target = true;
                    command.image = state.selected.image;
                    command.namespc = state.selected.namespc;
                    command.class_name = state.selected.class_name;
                    command.bool_value = !row_tracing;
                    RuntimeModel::instance().enqueue(std::move(command));
                }
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    ImGui::SetTooltip("%s", !method.signature_complete
                        ? "Tracing is disabled because the runtime did not expose a complete method signature"
                        : row_tracing ? "Stop recording calls to this method"
                                      : "Record every call to this method in Method Calls");
                ImGui::SameLine();
                if (ImGui::SmallButton("Copy addr"))
                    ImGui::SetClipboardText(method.pointer_text.c_str());
                if (method_open) {
                    if (!method.capability_reason.empty())
                        ImGui::TextWrapped("%s", method.capability_reason.c_str());
                    const std::uint64_t static_scope =
                        0xcb00000000000000ull |
                        (std::hash<std::string>{}(state.selected.image + "\n" + state.selected.full_name) &
                         0x00ffffffffffffffull);
                    const std::uint64_t execution_scope =
                        state.target_token != 0 ? state.target_token : static_scope;
                    if (method.uses_generic_parameter) {
                        MemberBuffer& generic_type = member_buffer(generic_type_key(0, method_index, execution_scope));
                        ImGui::SetNextItemWidth(-1.0f);
                        render_generic_type_input(snapshot, "##generic-type", generic_type.text);
                        ImGui::TextDisabled("Example: bolt.user.dll:Photon.Bolt.IPlayerState");
                    }
                    for (std::size_t parameter = 0; parameter < method.parameter_types.size(); ++parameter) {
                        const std::string name =
                            parameter < method.parameter_names.size() && !method.parameter_names[parameter].empty()
                                ? method.parameter_names[parameter]
                                : "arg" + std::to_string(parameter + 1);
                        ImGui::PushID(static_cast<int>(parameter));
                        render_method_argument(0, method_index, parameter, method.parameter_types[parameter], name,
                                               execution_scope, &snapshot.managed_references);
                        ImGui::PopID();
                    }
                    const bool constructor = method.name == ".ctor" && !method.is_static;
                    const bool has_target = constructor || method.is_static || state.target_token != 0;
                    ImGui::BeginDisabled(!has_target || !invokable_method(method));
                    if (ImGui::SmallButton(constructor ? "Create instance" : "Execute")) {
                        Command command{};
                        command.kind = constructor ? CommandKind::CreateClassInstance : CommandKind::InvokeMethod;
                        command.metadata_address = state.selected.metadata_address;
                        command.member_index = static_cast<int>(method_index);
                        command.class_browser_target = true;
                        command.reference_token = state.target_token;
                        command.object_inspector_token = execution_scope;
                        command.image = state.selected.image;
                        command.namespc = state.selected.namespc;
                        command.class_name = state.selected.class_name;
                        if (method.uses_generic_parameter) {
                            const std::uint64_t key = generic_type_key(0, method_index, execution_scope);
                            command.generic_type_arguments.push_back(member_buffer(key).text.data());
                        }
                        for (std::size_t parameter = 0; parameter < method.parameter_types.size(); ++parameter) {
                            const std::uint64_t key =
                                method_argument_key(0, method_index, parameter, execution_scope);
                            command.method_arguments.push_back(boolean_type(method.parameter_types[parameter])
                                ? (method_boolean_argument(key) ? "true" : "false")
                                : member_buffer(key).text.data());
                        }
                        RuntimeModel::instance().enqueue(std::move(command));
                    }
                    ImGui::EndDisabled();
                    if (!has_target) {
                        ImGui::SameLine();
                        ImGui::TextDisabled("Select a live target for instance methods");
                    }
                    render_method_result(snapshot, 0, method_index, execution_scope);
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
        }
        ImGui::EndChild();
    }

    if (same_browser_class(snapshot.class_browser_static_query, state.selected)) {
        ImGui::SeparatorText("Static State");
        ImGui::TextDisabled("%zu static field/property member(s)", snapshot.class_browser_static_fields.size());
        ImGui::BeginChild("##class-browser-static-fields", ImVec2(0.0f, 145.0f), true);
        for (const ClassBrowserStaticFieldInfo &field : snapshot.class_browser_static_fields) {
            ImGui::PushID(field.is_property ? "property" : "field");
            ImGui::PushID(static_cast<int>(field.member_index));
            render_member_name(field.is_property ? Labels::Kind::Property : Labels::Kind::Field,
                               state.selected.full_name, field.name);
            if (field.is_property) {
                const ComponentInfo::Property property_member{
                    field.name, field.type_name, state.selected.full_name, field.readable, field.writable};
                render_property_context_menu(property_member,
                    code_context(state.selected.image, state.selected.namespc,
                                 state.selected.class_name, state.selected.full_name), {});
            } else {
                const ComponentInfo::Field field_member{
                    field.name, field.type_name, state.selected.full_name, true};
                render_field_context_menu(field_member,
                    code_context(state.selected.image, state.selected.namespc,
                                 state.selected.class_name, state.selected.full_name), {});
            }
            ImGui::SameLine();
            ImGui::TextDisabled("%s = %s", Labels::humanize(field.type_name).c_str(), field.display.c_str());
            if (field.token != 0) {
                ImGui::SameLine();
                if (ImGui::SmallButton("Inspect"))
                    enqueue_reference_inspection(field.token);
                ImGui::SameLine();
                if (ImGui::SmallButton("Copy Ptr"))
                    ImGui::SetClipboardText(field.pointer_text.c_str());
            }
            if (field.writable) {
                const std::uint64_t scope = std::hash<std::string>{}(state.selected.image + "\n" +
                                                                    state.selected.full_name);
                MemberBuffer &buffer =
                    member_buffer(scoped_ui_key(scope,
                        field.is_property ? 0xcc00000000000000ull : 0xcb00000000000000ull,
                        field.member_index));
                if (!buffer.active) {
                    buffer.active = true;
                    const std::string initial =
                        field.is_reference && !field.pointer_text.empty() ? field.pointer_text : field.display;
                    copy_text(buffer.text, initial == "<unavailable>" ? "" : initial);
                    buffer.bool_value = field.value.bool_value;
                    buffer.bool_initialized = true;
                }
                if (field.value.kind == URK::Unity::Inspect::ValueKind::Boolean) {
                    ImGui::Checkbox("New value", &buffer.bool_value);
                    copy_text(buffer.text, buffer.bool_value ? "true" : "false");
                } else {
                    ImGui::SetNextItemWidth(std::max(160.0f, ImGui::GetContentRegionAvail().x - 70.0f));
                    input_text_dynamic("##static-field-value",
                                       field.is_reference ? "null/default or Copy Ptr address" : "New value",
                                       buffer.text);
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Set")) {
                    Command command{};
                    command.kind = CommandKind::SetClassBrowserStaticField;
                    command.member_index = static_cast<int>(field.member_index);
                    command.int_value = field.is_property ? 1 : 0;
                    command.text = buffer.text.data();
                    command.bool_value = buffer.bool_value;
                    command.metadata_address = state.selected.metadata_address;
                    command.image = state.selected.image;
                    command.namespc = state.selected.namespc;
                    command.class_name = state.selected.class_name;
                    RuntimeModel::instance().enqueue(std::move(command));
                }
            } else {
                ImGui::TextDisabled("Read-only");
            }
            ImGui::PopID();
            ImGui::PopID();
        }
        ImGui::EndChild();
    }

    if (same_browser_class(snapshot.class_browser_query, state.selected)) {
        ImGui::SeparatorText("Instances");
        ImGui::TextDisabled("%zu result(s) | %zu reachable objects | %zu scene roots | %zu static roots%s",
                            snapshot.class_browser_instances.size(), snapshot.class_browser_scanned_objects,
                            snapshot.class_browser_scene_roots, snapshot.class_browser_static_roots,
                            snapshot.class_browser_scan_truncated ? " (scan cap reached)" : "");
        if (snapshot.class_browser_scan_active) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.66f, 0.76f, 0.86f, 1.0f), "SCANNING...");
        }
        ImGui::BeginChild("##class-browser-instances", ImVec2(0.0f, 150.0f), true);
        for (const ClassBrowserInstanceInfo &instance : snapshot.class_browser_instances) {
            ImGui::PushID(
                static_cast<int>(
                    static_cast<std::uint32_t>(instance.token >> 32)));

            ImGui::PushID(
                static_cast<int>(
                    static_cast<std::uint32_t>(instance.token)));
            ImGui::TextUnformatted(instance.name.c_str());
            if (!instance.source.empty()) {
                ImGui::SameLine();
                ImGui::TextDisabled("[%s]", instance.source.c_str());
            }
            ImGui::SameLine();
            const bool is_target = state.target_token == instance.token;
            if (ImGui::SmallButton(is_target ? "Target active" : "Use as target")) {
                state.target_token = instance.token;
                enqueue_reference_inspection(instance.token, false);
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Open Inspector"))
                enqueue_reference_inspection(instance.token);
            if (instance.game_object_instance_id != 0) {
                ImGui::SameLine();
                if (ImGui::SmallButton("Select owner"))
                    enqueue_simple(CommandKind::Select, instance.game_object_instance_id);
                ImGui::SameLine();
                ImGui::TextDisabled("%s", instance.game_object_name.c_str());
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Copy Ptr"))
                ImGui::SetClipboardText(instance.pointer_text.c_str());
            ImGui::PopID();
            ImGui::PopID();
        }
        ImGui::EndChild();
    }

    if (state.target_token != 0) {
        ImGui::SeparatorText("Live Target Workspace");
        if (!snapshot.object_inspector.valid || snapshot.object_inspector.token != state.target_token) {
            ImGui::TextDisabled("Resolving the selected runtime instance...");
        } else {
        ImGui::TextColored(ImVec4(0.60f, 0.68f, 0.60f, 1.0f), "Target: %s",
                               snapshot.object_inspector.type_name.c_str());
            ImGui::TextDisabled(
                "Operations below use the rooted runtime instance. Interface selections dispatch through its concrete type.");
            render_current_object_inspector(snapshot);
        }
    }
}

} // namespace Explorer::UI

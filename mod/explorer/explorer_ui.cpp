// Copyright (c) 2026 Jadis0x. All rights reserved.
#include "explorer_ui.h"

#include "config/mod_config.h"
#include "explorer_model.h"

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
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Explorer::UI {
namespace {

struct InspectorBuffers {
    int instance_id = 0;
    std::array<char, 256> name{};
    std::array<char, 128> tag{};
};

struct AddComponentBuffers {
    std::array<char, 128> image{};
    std::array<char, 128> namespc{};
    std::array<char, 128> class_name{};
    std::array<char, 192> class_search{};
    bool catalog_requested = false;
};

InspectorBuffers &inspector_buffers() {
    static InspectorBuffers buffers;
    return buffers;
}

AddComponentBuffers &component_buffers() {
    static AddComponentBuffers buffers;
    return buffers;
}

std::array<char, 128> &search_buffer() {
    static std::array<char, 128> buffer{};
    return buffer;
}

constexpr std::size_t kMaxRememberedMemberEditors = 4096;

void copy_text(auto &buffer, std::string_view value) {
    std::snprintf(buffer.data(), buffer.size(), "%.*s", static_cast<int>(value.size()), value.data());
}

void copy_text(std::vector<char> &buffer, std::string_view value) {
    buffer.resize(std::max<std::size_t>(256, value.size() + 1));
    std::memcpy(buffer.data(), value.data(), value.size());
    buffer[value.size()] = '\0';
}

int resize_text_buffer(ImGuiInputTextCallbackData *data) {
    if (data->EventFlag != ImGuiInputTextFlags_CallbackResize)
        return 0;
    auto *buffer = static_cast<std::vector<char> *>(data->UserData);
    buffer->resize(static_cast<std::size_t>(data->BufTextLen) + 1);
    data->Buf = buffer->data();
    return 0;
}

bool input_text_dynamic(const char *label, const char *hint, std::vector<char> &buffer) {
    if (buffer.empty())
        buffer.resize(256, '\0');
    const ImGuiInputTextFlags flags = ImGuiInputTextFlags_CallbackResize | ImGuiInputTextFlags_EnterReturnsTrue;
    return hint
               ? ImGui::InputTextWithHint(label, hint, buffer.data(), buffer.size(), flags, resize_text_buffer, &buffer)
               : ImGui::InputText(label, buffer.data(), buffer.size(), flags, resize_text_buffer, &buffer);
}

char ascii_lower(char value) {
    const unsigned char character = static_cast<unsigned char>(value);
    return static_cast<char>(std::tolower(character));
}

bool contains_case_insensitive(std::string_view text, std::string_view filter) {
    if (filter.empty())
        return true;
    if (filter.size() > text.size())
        return false;

    for (std::size_t start = 0; start <= text.size() - filter.size(); ++start) {
        std::size_t index = 0;
        while (index < filter.size() && ascii_lower(text[start + index]) == ascii_lower(filter[index])) {
            ++index;
        }
        if (index == filter.size())
            return true;
    }
    return false;
}

bool equals_case_insensitive(std::string_view left, std::string_view right) {
    return left.size() == right.size() && contains_case_insensitive(left, right);
}

void open_external_url(const char *url) {
    if (url && url[0])
        ShellExecuteA(nullptr, "open", url, nullptr, nullptr, SW_SHOWNORMAL);
}

bool workspace_link_button(const char *label, const char *url, const ImVec4 &color) {
    ImGui::PushStyleColor(ImGuiCol_Button, color);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(color.x + 0.08f, color.y + 0.08f, color.z + 0.08f, color.w));
    const bool pressed = ImGui::SmallButton(label);
    ImGui::PopStyleColor(2);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", url);
    if (pressed)
        open_external_url(url);
    return pressed;
}

using NodeMatchSet = std::unordered_set<int>;

NodeMatchSet &hierarchy_filter_matches() {
    static NodeMatchSet matches;
    return matches;
}

bool collect_matching_nodes(const HierarchyNode &node, std::string_view filter, bool include_inactive,
                            NodeMatchSet &matches) {
    bool matches_filter = include_inactive || node.active;
    matches_filter = matches_filter && contains_case_insensitive(node.name, filter);
    for (const HierarchyNode &child : node.children)
        matches_filter = collect_matching_nodes(child, filter, include_inactive, matches) || matches_filter;
    if (matches_filter)
        matches.insert(node.instance_id);
    return matches_filter;
}

void enqueue_simple(CommandKind kind, int instance_id) {
    RuntimeModel::instance().enqueue(Command{.kind = kind, .instance_id = instance_id});
}

void enqueue_hierarchy_command(CommandKind kind, const HierarchyNode &node, std::uint64_t revision) {
    Command command{
         .kind = kind,
         .instance_id = node.instance_id
    };


    if (kind == CommandKind::DeleteObject ||
        kind == CommandKind::DuplicateObject) {
        command.hierarchy_revision = revision;
    }

    // command.expected_object_address = node.object_address;

    RuntimeModel::instance().enqueue(std::move(command));

}

void render_context_menu(const HierarchyNode &node, std::uint64_t revision) {
    if (!ImGui::BeginPopupContextItem("##game-object-context"))
        return;
    ImGui::TextDisabled("%s", node.name.c_str());
    ImGui::Separator();
    if (ImGui::MenuItem("Copy Ptr"))
        ImGui::SetClipboardText(node.pointer_text.c_str());
    if (ImGui::MenuItem("Duplicate"))
        enqueue_hierarchy_command(CommandKind::DuplicateObject, node, revision);
    if (ImGui::MenuItem("Delete"))
        enqueue_hierarchy_command(CommandKind::DeleteObject, node, revision);
    ImGui::EndPopup();
}

void render_node(const HierarchyNode &node, int selected_instance_id, const NodeMatchSet *matches,
                  bool include_inactive, std::uint64_t revision) {
    if (!include_inactive && !node.active)
        return;
    if (matches && !matches->contains(node.instance_id))
        return;

    ImGui::PushID(node.instance_id);
    ImGuiTreeNodeFlags flags =
        ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (node.children.empty())
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    if (node.instance_id == selected_instance_id)
        flags |= ImGuiTreeNodeFlags_Selected;
    if (matches)
        flags |= ImGuiTreeNodeFlags_DefaultOpen;

    if (!node.active)
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    const bool open = ImGui::TreeNodeEx("##node", flags, "%s", node.name.c_str());
    if (!node.active)
        ImGui::PopStyleColor();

	if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
		enqueue_hierarchy_command(CommandKind::FocusSelected, node, revision);
	}
	else if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
		enqueue_hierarchy_command(CommandKind::Select, node, revision);
    render_context_menu(node, revision);

    if (open && !node.children.empty()) {
        for (const HierarchyNode &child : node.children)
            render_node(child, selected_instance_id, matches, include_inactive, revision);
        ImGui::TreePop();
    }
    ImGui::PopID();
}

void render_hierarchy(const HierarchyInfo &hierarchy, int selected_instance_id) {
    ImGui::BeginChild("##hierarchy", ImVec2(0.0f, 0.0f), true);
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##hierarchy-search", "Search GameObjects...", search_buffer().data(),
                             search_buffer().size());
    static bool include_inactive = true;
    ImGui::Checkbox("Inactive", &include_inactive);
    ImGui::Separator();

    const std::string_view filter(search_buffer().data());
    NodeMatchSet *matches = nullptr;
    if (!filter.empty()) {
        static std::uint64_t cached_hierarchy_revision = 0;
        static std::string cached_filter;
        static bool cached_include_inactive = true;
        NodeMatchSet &cached_matches = hierarchy_filter_matches();
        if (cached_hierarchy_revision != hierarchy.revision || cached_filter != filter ||
            cached_include_inactive != include_inactive) {
            cached_matches.clear();
            if (cached_matches.bucket_count() < hierarchy.objects)
                cached_matches.reserve(hierarchy.objects);
            for (const SceneNode &scene : hierarchy.scenes)
                for (const HierarchyNode &root : scene.roots)
                    collect_matching_nodes(root, filter, include_inactive, cached_matches);
            cached_hierarchy_revision = hierarchy.revision;
            cached_filter = filter;
            cached_include_inactive = include_inactive;
        }
        matches = &cached_matches;
    }
    for (const SceneNode &scene : hierarchy.scenes) {
        const int group_id = scene.dont_destroy_on_load ? -1 : scene.hide_and_dont_save ? -2 : scene.handle;
        ImGui::PushID(group_id);
        ImGuiTreeNodeFlags scene_flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth;
        const ImVec4 scene_color = scene.dont_destroy_on_load ? ImVec4(0.38f, 0.76f, 0.92f, 1.0f)
                                   : scene.hide_and_dont_save ? ImVec4(0.86f, 0.61f, 0.34f, 1.0f)
                                                              : ImVec4(0.62f, 0.78f, 0.94f, 1.0f);
        const char *marker = scene.dont_destroy_on_load ? "[DDOL] "
                             : scene.hide_and_dont_save ? "[Hidden] "
                             : scene.active             ? "[Active] "
                                                        : "";
        ImGui::PushStyleColor(ImGuiCol_Text, scene_color);
        const bool open = ImGui::TreeNodeEx("##scene", scene_flags, "%s%s", marker, scene.name.c_str());
        ImGui::PopStyleColor();
        if (open) {
            for (const HierarchyNode &root : scene.roots)
                render_node(root, selected_instance_id, matches, include_inactive, hierarchy.revision);
            if (scene.roots.empty())
                ImGui::TextDisabled("  No root GameObjects");
            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    if (hierarchy.scenes.empty())
        ImGui::TextDisabled("Waiting for a loaded scene...");

    if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemHovered()) {
        enqueue_simple(CommandKind::ClearSelection, 0);
    }
    ImGui::EndChild();
}

bool property_label(const char *label) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::TableSetColumnIndex(1);
    ImGui::SetNextItemWidth(-1.0f);
    return true;
}

std::string type_details_text(std::string_view assembly, std::string_view namespc, std::string_view class_name,
                              std::string_view full_name) {
    return "Assembly: " + std::string(assembly.empty() ? "<unavailable>" : assembly) +
           "\nNamespace: " + std::string(namespc.empty() ? "<global>" : namespc) +
           "\nClass: " + std::string(class_name.empty() ? "<unavailable>" : class_name) +
           "\nFull type: " + std::string(full_name.empty() ? "<unavailable>" : full_name);
}

std::string member_qualified_name(std::string_view declaring_type, std::string_view name) {
    return declaring_type.empty() ? std::string(name) : std::string(declaring_type) + "." + std::string(name);
}

std::string field_signature(const ComponentInfo::Field &field) {
    return std::string(field.is_static ? "static " : "") +
           (field.type_name.empty() ? "<unavailable>" : field.type_name) + " " +
           member_qualified_name(field.declaring_type, field.name);
}

std::string property_signature(const ComponentInfo::Property &property) {
    std::string signature = (property.type_name.empty() ? "<unavailable>" : property.type_name) + " " +
                            member_qualified_name(property.declaring_type, property.name) + " { ";
    if (property.can_read)
        signature += "get; ";
    if (property.can_write)
        signature += "set; ";
    return signature + "}";
}

std::string method_signature(const ComponentInfo::Method &method) {
    std::string signature = std::string(method.is_static ? "static " : "") +
                            (method.return_type.empty() ? "<unavailable>" : method.return_type) + " " +
                            member_qualified_name(method.declaring_type, method.name) + "(";
    for (std::size_t index = 0; index < method.parameter_types.size(); ++index) {
        if (index)
            signature += ", ";
        signature += method.parameter_types[index].empty() ? "<unavailable>" : method.parameter_types[index];
        if (index < method.parameter_names.size() && !method.parameter_names[index].empty())
            signature += " " + method.parameter_names[index];
    }
    return signature + ")";
}

struct CodeContext {
    std::string image;
    std::string namespc;
    std::string class_name;
    std::string game_object_name;
};

CodeContext code_context(std::string_view image, std::string_view namespc, std::string_view class_name,
                         std::string_view full_name = {}) {
    CodeContext result{std::string(image), std::string(namespc), std::string(class_name)};
    if (!result.class_name.empty() || full_name.empty())
        return result;
    const std::size_t separator = full_name.rfind('.');
    if (separator == std::string_view::npos)
        result.class_name = full_name;
    else {
        result.namespc = full_name.substr(0, separator);
        result.class_name = full_name.substr(separator + 1);
    }
    return result;
}

CodeContext declaring_code_context(CodeContext context, std::string_view declaring_type) {
    if (declaring_type.empty())
        return context;
    return code_context(context.image, {}, {}, declaring_type);
}

std::string cpp_string_literal(std::string_view text) {
    std::string literal{"\""};
    literal.reserve(text.size() + 8);
    for (const unsigned char byte : text) {
        switch (byte) {
        case '\\': literal += "\\\\"; break;
        case '\"': literal += "\\\""; break;
        case '\n': literal += "\\n"; break;
        case '\r': literal += "\\r"; break;
        case '\t': literal += "\\t"; break;
        default:
            if (byte < 0x20u) {
                char escape[5]{};
                std::snprintf(escape, sizeof(escape), "\\%03o", static_cast<unsigned>(byte));
                literal += escape;
            } else {
                // Preserve valid UTF-8 so copied Unicode identifiers stay readable.
                literal.push_back(static_cast<char>(byte));
            }
        }
    }
    return literal + "\"";
}

std::string urkit_cpp_type(std::string_view managed_type) {
    static constexpr std::pair<std::string_view, std::string_view> types[] = {
        {"System.Void", "void"}, {"System.Boolean", "bool"}, {"System.Byte", "std::uint8_t"},
        {"System.SByte", "std::int8_t"}, {"System.Int16", "std::int16_t"},
        {"System.UInt16", "std::uint16_t"}, {"System.Int32", "std::int32_t"},
        {"System.UInt32", "std::uint32_t"}, {"System.Int64", "std::int64_t"},
        {"System.UInt64", "std::uint64_t"}, {"System.Single", "float"}, {"System.Double", "double"},
        {"System.Char", "char16_t"}, {"System.String", "std::string"},
        {"UnityEngine.Vector2", "URK::Unity::Vector2"}, {"UnityEngine.Vector3", "URK::Unity::Vector3"},
        {"UnityEngine.Vector4", "URK::Unity::Vector4"}, {"UnityEngine.Quaternion", "URK::Unity::Quaternion"},
        {"UnityEngine.Color", "URK::Unity::Color"}, {"UnityEngine.Color32", "URK::Unity::Color32"},
        {"UnityEngine.Rect", "URK::Unity::Rect"}, {"UnityEngine.Bounds", "URK::Unity::Bounds"},
    };
    for (const auto &[managed, cpp] : types)
        if (managed_type == managed)
            return std::string(cpp);
    std::string identifier = "Managed_";
    for (const unsigned char byte : managed_type) {
        if ((byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') || (byte >= '0' && byte <= '9'))
            identifier.push_back(static_cast<char>(byte));
        else
            identifier.push_back('_');
    }
    return identifier;
}

std::string hook_cpp_type(std::string_view managed_type) {
    const std::string type = urkit_cpp_type(managed_type);
    return type == "std::string" || type.rfind("Managed_", 0) == 0 ? "URK::il2cpp::Object*" : type;
}

std::string code_value_placeholder(std::string_view managed_type, std::size_t index) {
    const std::string type = urkit_cpp_type(managed_type);
    if (type == "bool")
        return "/* arg" + std::to_string(index + 1) + " */ false";
    if (type == "float")
        return "/* arg" + std::to_string(index + 1) + " */ 0.0f";
    if (type == "double")
        return "/* arg" + std::to_string(index + 1) + " */ 0.0";
    if (type == "void")
        return {};
    return "/* arg" + std::to_string(index + 1) + " */ " + type + "{}";
}

std::string type_ref_expression(const CodeContext &context) {
    return "URK::Unity::TypeRef{" + cpp_string_literal(context.image) + ", " +
           cpp_string_literal(context.namespc) + ", " + cpp_string_literal(context.class_name) + "}";
}

bool custom_value_type_requires_abi(std::string_view managed_type, bool is_value_type) {
    return is_value_type && urkit_cpp_type(managed_type).rfind("Managed_", 0) == 0;
}

std::string managed_type_alias(std::string_view managed_type, bool is_value_type = false) {
    const std::string type = urkit_cpp_type(managed_type);
    if (type.rfind("Managed_", 0) != 0)
        return {};
    if (is_value_type)
        return "// " + std::string(managed_type) +
               " is a value type; define its exact native ABI struct before using it.\n";
    return "using " + type + " = URK::Unity::Object; // " + std::string(managed_type) + "\n";
}

std::string member_type_aliases(const ComponentInfo::Method &method) {
    std::string aliases = managed_type_alias(method.return_type, method.return_is_value_type);
    for (std::size_t index = 0; index < method.parameter_types.size(); ++index) {
        const std::string &type = method.parameter_types[index];
        const bool is_value_type = index < method.parameter_is_value_types.size() && method.parameter_is_value_types[index];
        const std::string alias = managed_type_alias(type, is_value_type);
        if (!alias.empty() && aliases.find(alias) == std::string::npos)
            aliases += alias;
    }
    return aliases;
}

bool method_requires_custom_value_abi(const ComponentInfo::Method &method) {
    if (custom_value_type_requires_abi(method.return_type, method.return_is_value_type))
        return true;
    for (std::size_t index = 0; index < method.parameter_types.size(); ++index) {
        if (custom_value_type_requires_abi(
                method.parameter_types[index],
                index < method.parameter_is_value_types.size() && method.parameter_is_value_types[index]))
            return true;
    }
    return false;
}

std::string context_managed_name(const CodeContext &context) {
    return context.namespc.empty() ? context.class_name : context.namespc + "." + context.class_name;
}

std::string target_prelude(const CodeContext &context) {
    const std::string component_type = context_managed_name(context);
    const std::string cpp_type = urkit_cpp_type(component_type);
    const std::string alias = managed_type_alias(component_type);
    const std::string target_declaration = "const " + cpp_type + " target = ";
    if (!context.game_object_name.empty()) {
        return alias + target_declaration + "URK::Unity::GameObject::Find(" +
               cpp_string_literal(context.game_object_name) + ").GetComponent(" + cpp_string_literal(context.image) +
               ", " + cpp_string_literal(context.namespc) + ", " + cpp_string_literal(context.class_name) + ");\n";
    }
    return alias + target_declaration + "URK::Unity::Object::FindObjectOfTypeAll<URK::Unity::Object>(" +
           cpp_string_literal(context.image) + ", " + cpp_string_literal(context.namespc) + ", " +
           cpp_string_literal(context.class_name) + ");\n";
}

std::string managed_type_note(const char *role, std::string_view type) {
    return std::string("// ") + role + ": " + (type.empty() ? "<unavailable>" : std::string(type)) + "\n";
}

std::string field_get_code(const ComponentInfo::Field &field, const CodeContext &owner) {
    const CodeContext context = declaring_code_context(owner, field.declaring_type);
    const std::string type = urkit_cpp_type(field.type_name);
    const std::string name = cpp_string_literal(field.name);
    if (field.is_static)
        return managed_type_note("Managed field type", field.type_name) + managed_type_alias(field.type_name, field.is_value_type) +
               "const " + type + " value = URK::Unity::Object::StaticGetField<" + type + ">(\n    " +
               type_ref_expression(context) + ", " + name + ");";
    return managed_type_note("Managed field type", field.type_name) + managed_type_alias(field.type_name, field.is_value_type) + target_prelude(owner) +
           "const " + type + " value = target.GetField<" + type + ">(" + name + ");";
}

std::string field_set_code(const ComponentInfo::Field &field, const CodeContext &owner) {
    const CodeContext context = declaring_code_context(owner, field.declaring_type);
    const std::string type = urkit_cpp_type(field.type_name);
    const std::string name = cpp_string_literal(field.name);
    const std::string value = "/* new value */ " + type + "{}";
    if (field.is_static)
        return managed_type_note("Managed field type", field.type_name) + managed_type_alias(field.type_name, field.is_value_type) +
               "URK::Unity::Object::StaticSetField<" + type + ">(\n    " + type_ref_expression(context) + ", " +
               name + ", " + value + ");";
    return managed_type_note("Managed field type", field.type_name) + managed_type_alias(field.type_name, field.is_value_type) + target_prelude(owner) +
           "target.SetField<" + type + ">(" + name + ", " + value + ");";
}

std::string property_get_code(const ComponentInfo::Property &property, const CodeContext &context) {
    const std::string type = urkit_cpp_type(property.type_name);
    return managed_type_note("Managed property type", property.type_name) + managed_type_alias(property.type_name, property.is_value_type) + target_prelude(context) +
           "const " + type + " value = target.GetProperty<" + type + ">(" +
           cpp_string_literal(property.name) + ");";
}

std::string property_set_code(const ComponentInfo::Property &property, const CodeContext &context) {
    const std::string type = urkit_cpp_type(property.type_name);
    return managed_type_note("Managed property type", property.type_name) + managed_type_alias(property.type_name, property.is_value_type) + target_prelude(context) +
           "target.SetProperty<" + type + ">(" + cpp_string_literal(property.name) +
           ", /* new value */ " + type + "{});";
}

std::string method_arguments(const ComponentInfo::Method &method) {
    std::string arguments;
    for (std::size_t index = 0; index < method.parameter_types.size(); ++index) {
        if (!arguments.empty())
            arguments += ", ";
        arguments += code_value_placeholder(method.parameter_types[index], index);
    }
    return arguments;
}

std::string method_parameter_type_literals(const ComponentInfo::Method &method) {
    std::string types;
    for (const std::string &type : method.parameter_types) {
        if (!types.empty())
            types += ", ";
        types += cpp_string_literal(type);
    }
    return types;
}

std::string method_call_code(const ComponentInfo::Method &method, const CodeContext &owner) {
    const CodeContext context = declaring_code_context(owner, method.declaring_type);
    const std::string return_type = urkit_cpp_type(method.return_type);
    const std::string arguments = method_arguments(method);
    const std::string signature = method_parameter_type_literals(method);
    const std::string invocation = method.is_static
        ? "URK::Unity::detail::InvokeStatic<" + return_type + ">(\n    " + type_ref_expression(context) + ", " +
              cpp_string_literal(method.name) + (arguments.empty() ? ");" : ", " + arguments + ");")
        : "target.CallExact<" + return_type + ">(" + cpp_string_literal(method.name) + ", {" + signature + "}" +
              (arguments.empty() ? ");" : ", " + arguments + ");");
    const std::string prelude = method.is_static ? std::string{} : target_prelude(owner);
    std::string notes = managed_type_note("Managed return type", method.return_type);
    for (std::size_t index = 0; index < method.parameter_types.size(); ++index) {
        const std::string_view name = index < method.parameter_names.size() && !method.parameter_names[index].empty()
            ? std::string_view(method.parameter_names[index]) : std::string_view{};
        notes += "// Managed parameter " + std::to_string(index + 1) + (name.empty() ? "" : " (" + std::string(name) + ")") +
                 ": " + method.parameter_types[index] + "\n";
    }
    return notes + member_type_aliases(method) + prelude +
           (return_type == "void" ? invocation : "const " + return_type + " result = " + invocation);
}

std::uint32_t signature_hash(std::string_view text) {
    std::uint32_t hash = 2166136261u;
    for (const unsigned char byte : text) {
        hash ^= byte;
        hash *= 16777619u;
    }
    return hash;
}

std::string hex_u32(std::uint32_t value) {
    constexpr char hex[] = "0123456789ABCDEF";
    std::string text(8, '0');
    for (int index = 7; index >= 0; --index) {
        text[static_cast<std::size_t>(index)] = hex[value & 0x0Fu];
        value >>= 4u;
    }
    return text;
}

std::string method_hook_code(const ComponentInfo::Method &method, const CodeContext &owner) {
    const CodeContext context = declaring_code_context(owner, method.declaring_type);
    const std::string id = "method_" + hex_u32(signature_hash(method_signature(method)));
    const std::string return_type = hook_cpp_type(method.return_type);
    std::string parameters;
    std::string call_arguments;
    if (!method.is_static) {
        parameters = "URK::il2cpp::Object* self";
        call_arguments = "self";
    }
    for (std::size_t index = 0; index < method.parameter_types.size(); ++index) {
        const std::string argument_name = "arg" + std::to_string(index + 1);
        if (!parameters.empty())
            parameters += ", ";
        parameters += hook_cpp_type(method.parameter_types[index]) + " " + argument_name;
        if (!call_arguments.empty())
            call_arguments += ", ";
        call_arguments += argument_name;
    }
    if (!parameters.empty())
        parameters += ", ";
    parameters += "const URK::il2cpp::Method* method";
    if (!call_arguments.empty())
        call_arguments += ", ";
    call_arguments += "method";

    const std::string fn = id + "_fn";
    const std::string original = "g_original_" + id;
    const std::string detour = "detour_" + id;
    std::string code = "// Include sdk/il2cpp/il2cpp_helpers.h and sdk/runtime_api.h.\n";
    code += "// Managed signature: " + method_signature(method) + "\n";
    code += "using " + fn + " = " + return_type + "(__fastcall*) (" + parameters + ");\n";
    code += "inline " + fn + " " + original + "{};\n\n";
    code += return_type + " __fastcall " + detour + "(" + parameters + ") {\n";
    code += "    // Inspect or modify the arguments here.\n";
    if (return_type == "void") {
        code += "    if (" + original + ")\n        " + original + "(" + call_arguments + ");\n";
    } else {
        const std::string fallback = return_type == "URK::il2cpp::Object*" ? "nullptr" : return_type + "{}";
        code += "    return " + original + " ? " + original + "(" + call_arguments + ") : " + fallback + ";\n";
    }
    code += "}\n\n";
    code += "bool install_" + id + "() {\n";
    code += "    URK::HookOptions options{};\n    options.size = sizeof(options);\n";
    code += "    options.backend = static_cast<std::uint32_t>(URK::hook_backend_detours);\n";
    code += "    return Il2CppHook::attach(" + cpp_string_literal(context.image) + ", " +
            cpp_string_literal(context.namespc) + ", " + cpp_string_literal(context.class_name) + ", " +
            cpp_string_literal(method.name) + ", {" + method_parameter_type_literals(method) + "}, &" + original +
            ", &" + detour + ", nullptr, &options);\n}";
    return code;
}

void copy_code_menu_item(const char *label, const std::string &code, bool enabled = true) {
    if (ImGui::MenuItem(label, nullptr, false, enabled))
        ImGui::SetClipboardText(code.c_str());
}

void render_field_context_menu(const ComponentInfo::Field &field, const CodeContext &context) {
    if (!ImGui::BeginPopupContextItem("##field-code-menu"))
        return;
    copy_code_menu_item("Copy signature", field_signature(field));
    ImGui::Separator();
    const bool code_supported = field.runtime_safe && !custom_value_type_requires_abi(field.type_name, field.is_value_type);
    copy_code_menu_item("Copy URKit field read", field_get_code(field, context), code_supported);
    copy_code_menu_item("Copy URKit field write", field_set_code(field, context), code_supported);
    if (!code_supported)
        ImGui::TextDisabled("%s", field.capability_reason.empty() ? "Custom value type: define its native ABI struct first." : field.capability_reason.c_str());
    ImGui::EndPopup();
}

void render_property_context_menu(const ComponentInfo::Property &property, const CodeContext &context) {
    if (!ImGui::BeginPopupContextItem("##property-code-menu"))
        return;
    copy_code_menu_item("Copy signature", property_signature(property));
    ImGui::Separator();
    const bool code_supported = property.runtime_safe && !custom_value_type_requires_abi(property.type_name, property.is_value_type);
    copy_code_menu_item("Copy URKit property get", property_get_code(property, context), property.can_read && code_supported);
    copy_code_menu_item("Copy URKit property set", property_set_code(property, context), property.can_write && code_supported);
    if (!code_supported)
        ImGui::TextDisabled("%s", property.capability_reason.empty() ? "Custom value type: define its native ABI struct first." : property.capability_reason.c_str());
    ImGui::EndPopup();
}

void render_method_context_menu(const ComponentInfo::Method &method, const CodeContext &context) {
    if (!ImGui::BeginPopupContextItem("##method-code-menu"))
        return;
    copy_code_menu_item("Copy signature", method_signature(method));
    ImGui::Separator();
    const bool code_supported = method.runtime_callable && !method_requires_custom_value_abi(method);
    copy_code_menu_item("Copy URKit method call", method_call_code(method, context), code_supported);
    copy_code_menu_item("Copy Detours managed-method hook", method_hook_code(method, context), code_supported);
    if (!code_supported)
        ImGui::TextDisabled("%s", method.capability_reason.empty() ? "Custom value type: define its native ABI struct first." : method.capability_reason.c_str());
    ImGui::EndPopup();
}

void render_type_details(const char *label, std::string_view assembly, std::string_view namespc,
                         std::string_view class_name, std::string_view full_name) {
    if (!ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen))
        return;
    if (ImGui::BeginTable("##runtime-type", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoPadOuterX)) {
        ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, 88.0f);
        ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
        property_label("Assembly");
        ImGui::TextUnformatted(assembly.empty() ? "<unavailable>" : assembly.data());
        property_label("Namespace");
        ImGui::TextUnformatted(namespc.empty() ? "<global>" : namespc.data());
        property_label("Class");
        ImGui::TextUnformatted(class_name.empty() ? "<unavailable>" : class_name.data());
        property_label("Full type");
        ImGui::TextUnformatted(full_name.empty() ? "<unavailable>" : full_name.data());
        ImGui::EndTable();
    }
    if (ImGui::SmallButton("Copy type info")) {
        const std::string details = type_details_text(assembly, namespc, class_name, full_name);
        ImGui::SetClipboardText(details.c_str());
    }
}

void send_text_command(CommandKind kind, const InspectorInfo &info, const char *text) {
    Command command{};
    command.kind = kind;
    command.instance_id = info.instance_id;
    command.text = text ? text : "";
    RuntimeModel::instance().enqueue(std::move(command));
}

void send_vector_command(CommandKind kind, const InspectorInfo &info, const float value[3]) {
    Command command{};
    command.kind = kind;
    command.instance_id = info.instance_id;
    command.vector_value = {value[0], value[1], value[2]};
    RuntimeModel::instance().enqueue(std::move(command));
}

void render_identity(const InspectorInfo &info) {
    InspectorBuffers &buffers = inspector_buffers();
    if (buffers.instance_id != info.instance_id) {
        buffers.instance_id = info.instance_id;
        copy_text(buffers.name, info.name);
        copy_text(buffers.tag, info.tag);
    }

    bool active = info.active;
    if (ImGui::Checkbox("##active", &active)) {
        Command command{.kind = CommandKind::SetActive, .instance_id = info.instance_id};
        command.bool_value = active;
        RuntimeModel::instance().enqueue(std::move(command));
    }
    ImGui::SameLine();
    const float copy_width = ImGui::CalcTextSize("Copy Ptr").x + ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::SetNextItemWidth(std::max(80.0f, ImGui::GetContentRegionAvail().x - copy_width - 6.0f));
    ImGui::InputText("##name", buffers.name.data(), buffers.name.size());
    if (ImGui::IsItemDeactivatedAfterEdit())
        send_text_command(CommandKind::Rename, info, buffers.name.data());
    ImGui::SameLine();
    if (ImGui::SmallButton("Copy Ptr"))
        ImGui::SetClipboardText(info.pointer_text.c_str());

    if (ImGui::BeginTable("##identity", 2, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, 72.0f);
        ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);

        property_label("Tag");
        ImGui::InputText("##tag", buffers.tag.data(), buffers.tag.size());
        if (ImGui::IsItemDeactivatedAfterEdit())
            send_text_command(CommandKind::SetTag, info, buffers.tag.data());

        int layer = info.layer;
        property_label("Layer");
        if (ImGui::DragInt("##layer", &layer, 1.0f, 0, 31, "%d", ImGuiSliderFlags_AlwaysClamp)) {
            Command command{.kind = CommandKind::SetLayer, .instance_id = info.instance_id};
            command.int_value = layer;
            RuntimeModel::instance().enqueue(std::move(command));
        }

        bool is_static = info.is_static;
        property_label("Static");
        if (ImGui::Checkbox("##static", &is_static)) {
            Command command{.kind = CommandKind::SetStatic, .instance_id = info.instance_id};
            command.bool_value = is_static;
            RuntimeModel::instance().enqueue(std::move(command));
        }
        ImGui::EndTable();
    }
    render_type_details("GameObject Type", info.assembly_name, info.namespace_name, info.class_name, info.type_name);
}

void render_transform(const InspectorInfo &info) {
    if (!ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
        return;
    if (!ImGui::BeginTable("##transform", 2, ImGuiTableFlags_SizingStretchProp))
        return;
    ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, 72.0f);
    ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);

    float position[3]{info.local_position.x, info.local_position.y, info.local_position.z};
    property_label("Position");
    if (ImGui::DragFloat3("##position", position, 0.05f, 0.0f, 0.0f, "%.3f"))
        send_vector_command(CommandKind::SetLocalPosition, info, position);
    ImGui::SameLine();
    if (ImGui::SmallButton("Reset##position")) {
        const float reset[3]{};
        send_vector_command(CommandKind::SetLocalPosition, info, reset);
    }

    float rotation[3]{info.local_rotation.x, info.local_rotation.y, info.local_rotation.z};
    property_label("Rotation");
    if (ImGui::DragFloat3("##rotation", rotation, 0.25f, 0.0f, 0.0f, "%.2f"))
        send_vector_command(CommandKind::SetLocalRotation, info, rotation);
    ImGui::SameLine();
    if (ImGui::SmallButton("Reset##rotation")) {
        const float reset[3]{};
        send_vector_command(CommandKind::SetLocalRotation, info, reset);
    }

    float scale[3]{info.local_scale.x, info.local_scale.y, info.local_scale.z};
    property_label("Scale");
    if (ImGui::DragFloat3("##scale", scale, 0.02f, 0.0f, 0.0f, "%.3f"))
        send_vector_command(CommandKind::SetLocalScale, info, scale);
    ImGui::SameLine();
    if (ImGui::SmallButton("Reset##scale")) {
        const float reset[3]{1.0f, 1.0f, 1.0f};
        send_vector_command(CommandKind::SetLocalScale, info, reset);
    }

    ImGui::EndTable();
}

struct MemberBuffer {
    std::vector<char> text = std::vector<char>(256, '\0');
    bool active = false;
    bool dirty = false;
    bool pending = false;
    double pending_since = 0.0;
    bool bool_initialized = false;
    bool bool_value = false;
    bool structured_initialized = false;
    std::size_t component_count = 0;
    std::array<float, 8> components{};
    bool sample_requested = false;
};

std::unordered_map<std::uint64_t, MemberBuffer> &member_buffers() {
    static std::unordered_map<std::uint64_t, MemberBuffer> buffers;
    return buffers;
}

MemberBuffer &member_buffer(std::uint64_t key) {
    auto &buffers = member_buffers();
    if (const auto found = buffers.find(key); found != buffers.end())
        return found->second;
    if (buffers.size() >= kMaxRememberedMemberEditors)
        buffers.clear();
    return buffers[key];
}

std::unordered_map<int, std::array<char, 128>> &component_filters() {
    static std::unordered_map<int, std::array<char, 128>> filters;
    return filters;
}

std::array<char, 128> &component_filter(int component_id) {
    auto &filters = component_filters();
    if (const auto found = filters.find(component_id); found != filters.end())
        return found->second;
    if (filters.size() >= 1024)
        filters.clear();
    return filters[component_id];
}

std::unordered_map<int, bool> &component_inheritance_filters() {
    static std::unordered_map<int, bool> filters;
    return filters;
}

bool &component_show_inherited(int component_id) {
    auto &filters = component_inheritance_filters();
    if (const auto found = filters.find(component_id); found != filters.end())
        return found->second;
    if (filters.size() >= 1024)
        filters.clear();
    // Show inherited members by default.
    return filters.try_emplace(component_id, true).first->second;
}

bool member_matches_filter(std::string_view name, std::string_view type, std::string_view filter) {
    return filter.empty() || contains_case_insensitive(name, filter) || contains_case_insensitive(type, filter);
}

void paste_into(MemberBuffer &buffer) {
    if (const char *clipboard = ImGui::GetClipboardText())
        copy_text(buffer.text, clipboard);
}

bool editable_value(const URK::Unity::Inspect::ValueInfo &value) {
    using URK::Unity::Inspect::ValueKind;
    return value.kind == ValueKind::Boolean || value.kind == ValueKind::SignedInteger ||
           value.kind == ValueKind::UnsignedInteger || value.kind == ValueKind::FloatingPoint ||
           value.kind == ValueKind::String || value.kind == ValueKind::Enum || value.kind == ValueKind::Structured;
}

void enqueue_member_value(CommandKind kind, int component_id, int member_index, const char *text,
                          bool bool_value = false, bool object_inspector_target = false, std::uint64_t member_key = 0,
                          bool lock_value = false, bool unlock_value = false,
                          std::uint64_t object_inspector_token = 0) {
    Command command{};
    command.kind = kind;
    command.instance_id = component_id;
    command.member_index = member_index;
    command.text = text ? text : "";
    command.bool_value = bool_value;
    command.object_inspector_target = object_inspector_target;
    command.reference_token = member_key;
    command.object_inspector_token = object_inspector_token;
    command.lock_value = lock_value;
    command.unlock_value = unlock_value;
    RuntimeModel::instance().enqueue(std::move(command));
}

void render_member_lock(CommandKind kind, int component_id, int member_index, const char *text, bool bool_value,
                        bool object_inspector_target, std::uint64_t member_key, bool locked, bool lockable = true) {
    // Kept as a no-op while the caller interface is retired.
    (void)kind;
    (void)component_id;
    (void)member_index;
    (void)text;
    (void)bool_value;
    (void)object_inspector_target;
    (void)member_key;
    (void)locked;
    (void)lockable;
}

void request_object_reference_tab(std::uint64_t token);

void enqueue_reference_inspection(std::uint64_t token, bool request_object_tab = true) {
    if (token == 0)
        return;
    if (request_object_tab)
        request_object_reference_tab(token);
    Command command{};
    command.kind = CommandKind::InspectReference;
    command.reference_token = token;
    RuntimeModel::instance().enqueue(std::move(command));
}

void enqueue_raw_reference_inspection(std::uint64_t address) {
    if (address == 0)
        return;
    Command command{};
    command.kind = CommandKind::InspectRawReference;
    command.reference_token = address;
    RuntimeModel::instance().enqueue(std::move(command));
}

void enqueue_member_sample(CommandKind value_kind, int component_id, int member_index, bool object_inspector_target,
                           std::uint64_t object_inspector_token = 0) {
    Command command{};
    command.kind = CommandKind::SampleMemberValue;
    command.instance_id = component_id;
    command.member_index = member_index;
    command.bool_value = value_kind == CommandKind::SetPropertyValue;
    command.object_inspector_target = object_inspector_target;
    command.object_inspector_token = object_inspector_token;
    RuntimeModel::instance().enqueue(std::move(command));
}

void render_reference_button(const ComponentInfo::LiveValues::Reference *reference) {
    if (!reference || reference->is_null || reference->token == 0)
        return;
    const bool is_game_object = reference->type_name == "UnityEngine.GameObject" ||
                                reference->type_name == "GameObject";
    if (ImGui::SmallButton(is_game_object ? "Select" : "Inspect"))
        enqueue_reference_inspection(reference->token, !is_game_object);
    if (ImGui::BeginPopupContextItem("##reference-actions")) {
        ImGui::TextDisabled("%s", reference->type_name.c_str());
        if (ImGui::MenuItem("Copy address"))
            ImGui::SetClipboardText(reference->pointer_text.c_str());
        ImGui::EndPopup();
    }
}

bool pending_timed_out(const MemberBuffer &buffer, bool live_data) {
    return live_data && buffer.pending && ImGui::GetTime() - buffer.pending_since > 1.5;
}

bool structured_matches(const MemberBuffer &buffer, const URK::Unity::Inspect::ValueInfo &value) {
    if (buffer.component_count != value.component_count)
        return false;
    for (std::size_t index = 0; index < value.component_count; ++index) {
        if (std::abs(static_cast<double>(buffer.components[index]) - value.components[index]) > 0.0001)
            return false;
    }
    return true;
}

std::string structured_value_text(const MemberBuffer &buffer) {
    std::string text;
    for (std::size_t index = 0; index < buffer.component_count; ++index) {
        if (index)
            text += ',';
        char component[48]{};
        std::snprintf(component, sizeof(component), "%.9g", static_cast<double>(buffer.components[index]));
        text += component;
    }
    return text;
}

void commit_structured_value(CommandKind kind, int component_id, int member_index, MemberBuffer &buffer,
                             bool object_inspector_target, std::uint64_t member_key,
                             std::uint64_t object_inspector_token) {
    const std::string text = structured_value_text(buffer);
    enqueue_member_value(kind, component_id, member_index, text.c_str(), false, object_inspector_target, member_key,
                         false, false, object_inspector_token);
    buffer.dirty = false;
    buffer.pending = true;
    buffer.pending_since = ImGui::GetTime();
}

void render_structured_value(CommandKind kind, int component_id, int member_index,
                             const URK::Unity::Inspect::ValueInfo &value, MemberBuffer &buffer,
                             bool object_inspector_target, std::uint64_t member_key, bool locked, bool lockable,
                             bool live_data, std::uint64_t object_inspector_token) {
    if (buffer.pending && structured_matches(buffer, value))
        buffer.pending = false;
    if (!buffer.active && !buffer.dirty && (!buffer.pending || pending_timed_out(buffer, live_data))) {
        buffer.component_count = value.component_count;
        for (std::size_t index = 0; index < value.component_count; ++index)
            buffer.components[index] = static_cast<float>(value.components[index]);
        buffer.structured_initialized = true;
        buffer.pending = false;
    }
    if (!buffer.structured_initialized || buffer.component_count == 0) {
        ImGui::TextDisabled("%s", value.display.c_str());
        return;
    }

    const bool integral = URK::Unity::Inspect::structured_integer_type(value.type_name) ||
                          URK::Unity::Inspect::structured_byte_type(value.type_name);
    const bool is_color = equals_case_insensitive(value.type_name, "unityengine.color");
    bool changed = false;
    bool deactivated = false;
    if (integral) {
        int values[8]{};
        for (std::size_t index = 0; index < buffer.component_count; ++index)
            values[index] = static_cast<int>(buffer.components[index]);
        if (buffer.component_count == 2)
            changed = ImGui::DragInt2("##structured", values, 1.0f);
        else if (buffer.component_count == 3)
            changed = ImGui::DragInt3("##structured", values, 1.0f);
        else if (buffer.component_count == 4)
            changed = ImGui::DragInt4(
                "##structured", values, 1.0f, URK::Unity::Inspect::structured_byte_type(value.type_name) ? 0 : 0,
                URK::Unity::Inspect::structured_byte_type(value.type_name) ? 255 : 0, "%d",
                URK::Unity::Inspect::structured_byte_type(value.type_name) ? ImGuiSliderFlags_AlwaysClamp
                                                                           : ImGuiSliderFlags_None);
        else {
            changed = ImGui::DragInt3("##structured-a", values, 1.0f);
            deactivated = ImGui::IsItemDeactivatedAfterEdit();
            changed = ImGui::DragInt3("##structured-b", values + 3, 1.0f) || changed;
        }
        for (std::size_t index = 0; index < buffer.component_count; ++index)
            buffer.components[index] = static_cast<float>(values[index]);
    } else if (is_color) {
        changed = ImGui::ColorEdit4("##structured", buffer.components.data(), ImGuiColorEditFlags_Float);
    } else if (buffer.component_count == 2) {
        changed = ImGui::DragFloat2("##structured", buffer.components.data(), 0.01f, 0.0f, 0.0f, "%.4f");
    } else if (buffer.component_count == 3) {
        changed = ImGui::DragFloat3("##structured", buffer.components.data(), 0.01f, 0.0f, 0.0f, "%.4f");
    } else if (buffer.component_count == 4) {
        changed = ImGui::DragFloat4("##structured", buffer.components.data(), 0.01f, 0.0f, 0.0f, "%.4f");
    } else {
        changed = ImGui::DragFloat3("##structured-a", buffer.components.data(), 0.01f, 0.0f, 0.0f, "%.4f");
        deactivated = ImGui::IsItemDeactivatedAfterEdit();
        changed =
            ImGui::DragFloat3("##structured-b", buffer.components.data() + 3, 0.01f, 0.0f, 0.0f, "%.4f") || changed;
    }
    buffer.active = ImGui::IsItemActive();
    deactivated = ImGui::IsItemDeactivatedAfterEdit() || deactivated;
    buffer.dirty = buffer.dirty || changed;
    if (deactivated && buffer.dirty)
        commit_structured_value(kind, component_id, member_index, buffer, object_inspector_target, member_key,
                                object_inspector_token);
    const std::string lock_text = structured_value_text(buffer);
    render_member_lock(kind, component_id, member_index, lock_text.c_str(), false, object_inspector_target, member_key,
                       locked, lockable);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s - drag components; release to apply", value.type_name.c_str());
}

void render_live_value(CommandKind kind, int component_id, int member_index,
                       const URK::Unity::Inspect::ValueInfo *value, bool writable, std::uint64_t buffer_key,
                       const ComponentInfo::LiveValues::Reference *reference, bool object_inspector_target = false,
                       bool live_data = false, bool locked = false, bool lockable = true,
                       std::uint64_t object_inspector_token = 0, bool runtime_safe = true,
                       std::string_view capability_reason = {}) {
    using URK::Unity::Inspect::ValueKind;
    if (!runtime_safe) {
        ImGui::TextDisabled("Metadata only");
        if (ImGui::IsItemHovered() && !capability_reason.empty())
            ImGui::SetTooltip("%.*s", static_cast<int>(capability_reason.size()), capability_reason.data());
        return;
    }
    if (!value) {
        ImGui::TextDisabled("Sampling...");
        return;
    }
    MemberBuffer &buffer = member_buffers()[buffer_key];
    if (!value->readable) {
        if (value->display == "Not sampled") {
            if (live_data && !buffer.sample_requested) {
                enqueue_member_sample(kind, component_id, member_index, object_inspector_target,
                                      object_inspector_token);
                buffer.sample_requested = true;
            }
            if (live_data) {
                ImGui::TextDisabled("Reading...");
            } else if (ImGui::SmallButton("Read")) {
                enqueue_member_sample(kind, component_id, member_index, object_inspector_target,
                                      object_inspector_token);
                buffer.sample_requested = true;
            } else {
                ImGui::SameLine();
                ImGui::TextDisabled("Not sampled");
            }
        } else {
            buffer.sample_requested = false;
            ImGui::TextColored(ImVec4(1.0f, 0.48f, 0.34f, 1.0f), "%s",
                               value->display.empty() ? "Unavailable" : value->display.c_str());
        }
        render_reference_button(reference);
        return;
    }
    buffer.sample_requested = false;
    if (!writable) {
        ImGui::TextDisabled("%s", value->display.empty() ? "<unsupported>" : value->display.c_str());
        if (reference && !reference->is_null && reference->token != 0) {
            ImGui::SameLine();
            render_reference_button(reference);
        }
        return;
    }
    const bool reference_value = value->kind == ValueKind::ObjectReference ||
                                 value->kind == ValueKind::ArrayReference || value->kind == ValueKind::Null;
    if (reference_value) {
        const char *display = value->kind == ValueKind::Null
                                  ? "null"
                                  : (!value->display.empty() ? value->display.c_str() : value->type_name.c_str());
        ImGui::TextColored(ImVec4(0.45f, 0.82f, 0.92f, 1.0f), "%s", display);
        if (reference && !reference->is_null && reference->token != 0) {
            ImGui::SameLine();
            render_reference_button(reference);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Assign...")) {
            if (reference && !reference->pointer_text.empty())
                copy_text(buffer.text, reference->pointer_text);
            else
                copy_text(buffer.text, "null");
            ImGui::OpenPopup("##assign-reference");
        }
        if (ImGui::BeginPopup("##assign-reference")) {
            ImGui::TextUnformatted("Assign managed reference");
            ImGui::TextDisabled("%s", value->type_name.c_str());
            ImGui::SetNextItemWidth(300.0f);
            input_text_dynamic("##reference-address", "null, default, or Copy Ptr address", buffer.text);
            if (ImGui::Button("Paste address"))
                paste_into(buffer);
            ImGui::SameLine();
            if (ImGui::Button("Set null"))
                copy_text(buffer.text, "null");
            ImGui::SameLine();
            if (ImGui::Button("Apply")) {
                enqueue_member_value(kind, component_id, member_index, buffer.text.data(), false,
                                     object_inspector_target, buffer_key, false, false, object_inspector_token);
                buffer.pending = true;
                buffer.pending_since = ImGui::GetTime();
                ImGui::CloseCurrentPopup();
            }
            render_member_lock(kind, component_id, member_index, buffer.text.data(), false, object_inspector_target,
                               buffer_key, locked, lockable);
            ImGui::EndPopup();
        }
        return;
    }
    if (!editable_value(*value)) {
        ImGui::TextDisabled("%s", value->display.empty() ? "<unsupported>" : value->display.c_str());
        render_reference_button(reference);
        return;
    }
    if (value->kind == ValueKind::Boolean) {
        if (buffer.pending && buffer.bool_value == value->bool_value)
            buffer.pending = false;
        if (!buffer.bool_initialized || (!buffer.pending || pending_timed_out(buffer, live_data))) {
            buffer.bool_value = value->bool_value;
            buffer.bool_initialized = true;
            buffer.pending = false;
        }
        if (ImGui::Checkbox("##value", &buffer.bool_value)) {
            enqueue_member_value(kind, component_id, member_index, nullptr, buffer.bool_value, object_inspector_target,
                                 buffer_key, false, false, object_inspector_token);
            buffer.pending = true;
            buffer.pending_since = ImGui::GetTime();
        }
        render_member_lock(kind, component_id, member_index, nullptr, buffer.bool_value, object_inspector_target,
                           buffer_key, locked, lockable);
        render_reference_button(reference);
        return;
    }
    if (value->kind == ValueKind::Structured) {
        render_structured_value(kind, component_id, member_index, *value, buffer, object_inspector_target, buffer_key,
                                locked, lockable, live_data, object_inspector_token);
        return;
    }
    if (buffer.pending && std::string_view(buffer.text.data()) == value->display)
        buffer.pending = false;
    if (!buffer.active && (!buffer.pending || pending_timed_out(buffer, live_data))) {
        copy_text(buffer.text, value->display);
        buffer.pending = false;
    }
    const float apply_width = ImGui::CalcTextSize("Apply").x + ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::SetNextItemWidth(
        std::max(40.0f, ImGui::GetContentRegionAvail().x - apply_width - ImGui::GetStyle().ItemSpacing.x));
    const bool text_changed_or_submitted = input_text_dynamic("##value", nullptr, buffer.text);
    buffer.active = ImGui::IsItemActive();
    const bool apply_on_deactivate = ImGui::IsItemDeactivatedAfterEdit();
    const bool apply_on_enter = text_changed_or_submitted && ImGui::IsKeyPressed(ImGuiKey_Enter, false);
    ImGui::SameLine();
    const bool apply_clicked = ImGui::SmallButton("Apply");
    if (apply_clicked || apply_on_enter || apply_on_deactivate) {
        enqueue_member_value(kind, component_id, member_index, buffer.text.data(), false, object_inspector_target,
                             buffer_key, false, false, object_inspector_token);
        buffer.pending = true;
        buffer.pending_since = ImGui::GetTime();
    }
    render_member_lock(kind, component_id, member_index, buffer.text.data(), false, object_inspector_target, buffer_key,
                       locked, lockable);
    render_reference_button(reference);
}

bool invokable_method(const ComponentInfo::Method &method) {
    return !method.name.empty() && method.runtime_callable;
}

std::unordered_map<std::uint64_t, bool> &method_boolean_arguments() {
    static std::unordered_map<std::uint64_t, bool> arguments;
    return arguments;
}

bool &method_boolean_argument(std::uint64_t key) {
    auto &arguments = method_boolean_arguments();
    if (const auto found = arguments.find(key); found != arguments.end())
        return found->second;
    if (arguments.size() >= kMaxRememberedMemberEditors)
        arguments.clear();
    return arguments[key];
}

std::uint64_t scoped_ui_key(std::uint64_t scope, std::uint64_t domain, std::size_t first, std::size_t second = 0) {
    // Keep editor state separate for each Object Inspector tab.
    std::uint64_t key = scope ^ domain;
    key ^= static_cast<std::uint64_t>(first) + 0x9e3779b97f4a7c15ull + (key << 6) + (key >> 2);
    key ^= static_cast<std::uint64_t>(second) + 0x9e3779b97f4a7c15ull + (key << 6) + (key >> 2);
    return key;
}

std::uint64_t method_argument_key(int component_id, std::size_t method, std::size_t parameter,
                                  std::uint64_t object_inspector_token = 0) {
    if (object_inspector_token != 0)
        return scoped_ui_key(object_inspector_token, 0x7100000000000000ull, method, parameter);
    return 0x7000000000000000ull | (static_cast<std::uint64_t>(static_cast<std::uint32_t>(component_id)) << 24) |
           ((static_cast<std::uint64_t>(method) & 0x0fffull) << 12) |
           (static_cast<std::uint64_t>(parameter) & 0x0fffull);
}

bool boolean_type(std::string_view type) {
    return equals_case_insensitive(type, "system.boolean") || equals_case_insensitive(type, "bool");
}

void write_structured_argument(MemberBuffer &buffer) {
    const std::string text = structured_value_text(buffer);
    copy_text(buffer.text, text);
}

void render_method_argument(int component_id, std::size_t method_index, std::size_t parameter_index,
                            std::string_view type, std::string_view name, std::uint64_t object_inspector_token = 0) {
    const std::uint64_t key = method_argument_key(component_id, method_index, parameter_index, object_inspector_token);
    if (boolean_type(type)) {
        bool &value = method_boolean_argument(key);
        ImGui::Checkbox(name.data(), &value);
        return;
    }

    MemberBuffer &buffer = member_buffer(key);
    const std::size_t count = URK::Unity::Inspect::structured_component_count(type);
    if (count != 0) {
        if (!buffer.structured_initialized || buffer.component_count != count) {
            buffer.structured_initialized = true;
            buffer.component_count = count;
            buffer.components.fill(0.0f);
            write_structured_argument(buffer);
        }
        ImGui::TextDisabled("%.*s (%.*s)", static_cast<int>(name.size()), name.data(), static_cast<int>(type.size()),
                            type.data());
        const bool integral =
            URK::Unity::Inspect::structured_integer_type(type) || URK::Unity::Inspect::structured_byte_type(type);
        bool changed = false;
        if (integral) {
            int values[8]{};
            for (std::size_t index = 0; index < count; ++index)
                values[index] = static_cast<int>(buffer.components[index]);
            if (count == 2)
                changed = ImGui::DragInt2("##argument", values, 1.0f);
            else if (count == 3)
                changed = ImGui::DragInt3("##argument", values, 1.0f);
            else if (count == 4)
                changed = ImGui::DragInt4("##argument", values, 1.0f);
            else {
                changed = ImGui::DragInt3("##argument-a", values, 1.0f);
                changed = ImGui::DragInt3("##argument-b", values + 3, 1.0f) || changed;
            }
            for (std::size_t index = 0; index < count; ++index)
                buffer.components[index] = static_cast<float>(values[index]);
        } else if (equals_case_insensitive(type, "unityengine.color")) {
            changed = ImGui::ColorEdit4("##argument", buffer.components.data(), ImGuiColorEditFlags_Float);
        } else if (count == 2)
            changed = ImGui::DragFloat2("##argument", buffer.components.data(), 0.01f);
        else if (count == 3)
            changed = ImGui::DragFloat3("##argument", buffer.components.data(), 0.01f);
        else if (count == 4)
            changed = ImGui::DragFloat4("##argument", buffer.components.data(), 0.01f);
        else {
            changed = ImGui::DragFloat3("##argument-a", buffer.components.data(), 0.01f);
            changed = ImGui::DragFloat3("##argument-b", buffer.components.data() + 3, 0.01f) || changed;
        }
        if (changed)
            write_structured_argument(buffer);
        return;
    }

    const bool is_string = equals_case_insensitive(type, "system.string");
    const bool system_type = type.size() >= 7 && contains_case_insensitive(type.substr(0, 7), "system.");
    const std::string hint = is_string     ? std::string(name) + " (text)"
                             : system_type ? std::string(type) + " " + std::string(name)
                                           : std::string(name) + " (value, default, null, or Copy Ptr address)";
    ImGui::SetNextItemWidth(-1.0f);
    input_text_dynamic("##argument", hint.c_str(), buffer.text);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%.*s", static_cast<int>(type.size()), type.data());
}

void enqueue_method_invoke(int component_id, int method_index, const ComponentInfo::Method &method,
                           bool object_inspector_target = false, std::uint64_t object_inspector_token = 0) {
    Command command{};
    command.kind = CommandKind::InvokeMethod;
    command.instance_id = component_id;
    command.member_index = method_index;
    command.object_inspector_target = object_inspector_target;
    command.object_inspector_token = object_inspector_token;
    command.method_arguments.reserve(method.parameter_types.size());
    for (std::size_t parameter = 0; parameter < method.parameter_types.size(); ++parameter) {
        const std::uint64_t key = method_argument_key(component_id, static_cast<std::size_t>(method_index), parameter,
                                                      object_inspector_token);
        if (boolean_type(method.parameter_types[parameter]))
            command.method_arguments.push_back(method_boolean_argument(key) ? "true" : "false");
        else
            command.method_arguments.push_back(member_buffer(key).text.data());
    }
    RuntimeModel::instance().enqueue(std::move(command));
}

void enqueue_method_trace(int component_id, int method_index, bool enabled, bool object_inspector_target = false,
                          std::uint64_t object_inspector_token = 0) {
    Command command{};
    command.kind = CommandKind::SetMethodTrace;
    command.instance_id = component_id;
    command.member_index = method_index;
    command.bool_value = enabled;
    command.object_inspector_target = object_inspector_target;
    command.object_inspector_token = object_inspector_token;
    RuntimeModel::instance().enqueue(std::move(command));
}

void enqueue_method_trace_clear(MethodTracer::TraceId id) {
    Command command{};
    command.kind = CommandKind::ClearMethodTrace;
    command.reference_token = id;
    RuntimeModel::instance().enqueue(std::move(command));
}

const MethodTracer::Snapshot *trace_for_method(const std::vector<MethodTracer::Snapshot> &traces,
                                               const ComponentInfo::Method &method) {
    const auto found = std::find_if(traces.begin(), traces.end(), [&method](const MethodTracer::Snapshot &trace) {
        return !method.pointer_text.empty() && trace.method_pointer_text == method.pointer_text;
    });
    return found == traces.end() ? nullptr : &*found;
}

const Snapshot::FieldWatch *field_watch_for(const Snapshot &snapshot, int component_instance_id,
                                             std::size_t field_index) {
    const auto found = std::find_if(snapshot.field_watches.begin(), snapshot.field_watches.end(),
                                    [=](const Snapshot::FieldWatch &watch) {
                                        return watch.component_instance_id == component_instance_id &&
                                               watch.field_index == field_index;
                                    });
    return found == snapshot.field_watches.end() ? nullptr : &*found;
}

void enqueue_field_watch(int component_id, int field_index, bool enabled) {
    Command command{};
    command.kind = CommandKind::SetFieldWatch;
    command.instance_id = component_id;
    command.member_index = field_index;
    command.bool_value = enabled;
    RuntimeModel::instance().enqueue(std::move(command));
}

void enqueue_field_watch_clear(std::uint64_t id) {
    Command command{};
    command.kind = CommandKind::ClearFieldWatch;
    command.reference_token = id;
    RuntimeModel::instance().enqueue(std::move(command));
}

void enqueue_field_watch_close(std::uint64_t id) {
    Command command{};
    command.kind = CommandKind::CloseFieldWatch;
    command.reference_token = id;
    RuntimeModel::instance().enqueue(std::move(command));
}

std::string traced_value(std::string_view type, std::uint64_t value) {
    char buffer[96]{};
    if (type == "System.Boolean" || type == "Boolean" || type == "bool")
        return value ? "true" : "false";
    if (type == "System.Single" || type == "Single" || type == "float") {
        const float number = std::bit_cast<float>(static_cast<std::uint32_t>(value));
        std::snprintf(buffer, sizeof(buffer), "%g", static_cast<double>(number));
        return buffer;
    }
    if (type == "System.Double" || type == "Double" || type == "double") {
        const double number = std::bit_cast<double>(value);
        std::snprintf(buffer, sizeof(buffer), "%g", number);
        return buffer;
    }
    if (type == "System.Int32" || type == "Int32" || type == "int") {
        std::snprintf(buffer, sizeof(buffer), "%d", static_cast<std::int32_t>(value));
        return buffer;
    }
    if (type == "System.UInt32" || type == "UInt32" || type == "uint") {
        std::snprintf(buffer, sizeof(buffer), "%u", static_cast<std::uint32_t>(value));
        return buffer;
    }
    if (type == "System.Int64" || type == "Int64" || type == "long") {
        std::snprintf(buffer, sizeof(buffer), "%lld", static_cast<long long>(value));
        return buffer;
    }
    if (type == "System.UInt64" || type == "UInt64" || type == "ulong") {
        std::snprintf(buffer, sizeof(buffer), "%llu", static_cast<unsigned long long>(value));
        return buffer;
    }
    if (type == "System.IntPtr" || type == "IntPtr" || type == "System.UIntPtr" || type == "UIntPtr") {
        std::snprintf(buffer, sizeof(buffer), "native pointer (0x%llX)", static_cast<unsigned long long>(value));
        return buffer;
    }
    std::snprintf(buffer, sizeof(buffer), "0x%llX", static_cast<unsigned long long>(value));
    return buffer;
}

std::string trace_address(std::uintptr_t address);

std::string traced_arguments(const MethodTracer::Snapshot &trace, const MethodTracer::Record &record) {
    std::string arguments;
    for (std::size_t index = 0; index < record.arguments.size(); ++index) {
        if (!arguments.empty())
            arguments += ", ";
        const std::string_view name = index < trace.parameter_names.size() ? std::string_view(trace.parameter_names[index])
                                                                            : std::string_view{"arg"};
        const std::string_view type = index < trace.parameter_types.size() ? std::string_view(trace.parameter_types[index])
                                                                            : std::string_view{};
        const bool value_type = index < trace.parameter_is_value_type.size() && trace.parameter_is_value_type[index];
        const bool opaque = index < trace.parameter_is_opaque.size() && trace.parameter_is_opaque[index];
        std::string value;
        if (opaque) {
            value = "runtime-specific ABI pointer " + trace_address(record.arguments[index]);
        }
        else if (value_type) {
            value = "value ABI " + trace_address(record.arguments[index]);
            if (index < record.argument_xmm_low.size() &&
                (record.argument_xmm_low[index] != 0 ||
                 (index < record.argument_xmm_high.size() && record.argument_xmm_high[index] != 0))) {
                value += " [xmm=" + trace_address(record.argument_xmm_low[index]);
                if (index < record.argument_xmm_high.size())
                    value += ":" + trace_address(record.argument_xmm_high[index]);
                value += "]";
            }
        }
        else {
            value = index < record.argument_displays.size() && !record.argument_displays[index].empty()
                        ? record.argument_displays[index]
                        : traced_value(type, record.arguments[index]);
        }
        arguments += std::string(name) + "=" + value;
    }
    return arguments;
}

std::string trace_address(std::uintptr_t address) {
    char text[32]{};
    std::snprintf(text, sizeof(text), "0x%llX", static_cast<unsigned long long>(address));
    return text;
}

std::string traced_return(const MethodTracer::Snapshot &trace, const MethodTracer::Record &record) {
    if (trace.return_type == "System.Void" || trace.return_type == "Void" || trace.return_type == "void")
        return "void";
    if (!record.return_captured)
        return "<pending return>";
    if (trace.return_is_opaque)
        return "<runtime-specific return ABI; not decoded>";
    if (trace.return_uses_indirect_abi)
        return "<value-type return uses a hidden output buffer; not decoded>";
    if (!record.return_display.empty())
        return record.return_display;
    const std::uint64_t raw = trace.return_is_floating ? record.return_xmm_low : record.return_rax;
    if (trace.return_is_reference)
        return raw == 0 ? "null" : "managed reference " + trace_address(raw);
    return traced_value(trace.return_type, raw);
}

std::string raw_traced_return(const MethodTracer::Snapshot &trace, const MethodTracer::Record &record) {
    if (!record.return_captured)
        return "<pending>";
    std::string text = "rax=" + trace_address(record.return_rax);
    if (trace.return_is_floating)
        text += " xmm0=" + trace_address(record.return_xmm_low) + ":" + trace_address(record.return_xmm_high);
    return text;
}

std::string trace_elapsed_text(double seconds) {
    char text[64]{};
    if (seconds < 0.001)
        std::snprintf(text, sizeof(text), "%.3f us", seconds * 1000000.0);
    else if (seconds < 1.0)
        std::snprintf(text, sizeof(text), "%.6f ms", seconds * 1000.0);
    else
        std::snprintf(text, sizeof(text), "%.9f s", seconds);
    return text;
}

std::string trace_seconds_json(double seconds) {
    char text[64]{};
    std::snprintf(text, sizeof(text), "%.9f", seconds);
    return text;
}

std::string raw_trace_arguments(const MethodTracer::Snapshot &trace, const MethodTracer::Record &record) {
    std::string arguments;
    for (std::size_t index = 0; index < record.arguments.size(); ++index) {
        if (!arguments.empty())
            arguments += ", ";
        const std::string_view name = index < trace.parameter_names.size() ? std::string_view(trace.parameter_names[index])
                                                                            : std::string_view{"arg"};
        arguments += std::string(name) + "=" + trace_address(record.arguments[index]);
    }
    return arguments;
}

std::string raw_trace_abi_arguments(const MethodTracer::Snapshot &trace, const MethodTracer::Record &record) {
    std::string arguments;
    for (std::size_t index = 0; index < record.arguments.size(); ++index) {
        if (!arguments.empty())
            arguments += ", ";
        const std::string_view name = index < trace.parameter_names.size() ? std::string_view(trace.parameter_names[index])
                                                                            : std::string_view{"arg"};
        arguments += std::string(name) + "=" + trace_address(record.arguments[index]);
        if (index < record.argument_xmm_low.size() &&
            (record.argument_xmm_low[index] != 0 || (index < record.argument_xmm_high.size() && record.argument_xmm_high[index] != 0))) {
            arguments += " [xmm=" + trace_address(record.argument_xmm_low[index]);
            if (index < record.argument_xmm_high.size())
                arguments += ":" + trace_address(record.argument_xmm_high[index]);
            arguments += "]";
        }
    }
    return arguments;
}

void append_csv_value(std::string &out, std::string_view value) {
    out += '"';
    for (char character : value) {
        if (character == '"')
            out += '"';
        out += character;
    }
    out += '"';
}

void append_json_value(std::string &out, std::string_view value) {
    out += '"';
    for (char character : value) {
        switch (character) {
        case '\\':
            out += "\\\\";
            break;
        case '"':
            out += "\\\"";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            out += character;
            break;
        }
    }
    out += '"';
}

std::string trace_csv(const MethodTracer::Snapshot &trace) {
    std::string out = "sequence,seconds,thread,caller,caller_address,target,target_address,arguments,result,raw_arguments,raw_abi,raw_result\n";
    for (const MethodTracer::Record &record : trace.records) {
        const double seconds = trace.timestamp_frequency && record.timestamp_ticks >= trace.start_timestamp_ticks
                                   ? static_cast<double>(record.timestamp_ticks - trace.start_timestamp_ticks) /
                                         static_cast<double>(trace.timestamp_frequency)
                                   : 0.0;
        out += std::to_string(record.sequence) + "," + trace_seconds_json(seconds) + "," + std::to_string(record.thread_id) + ",";
        append_csv_value(out, record.caller_display);
        out += ",";
        append_csv_value(out, trace_address(record.caller_address));
        out += ",";
        append_csv_value(out, record.target_display);
        out += ",";
        append_csv_value(out, trace_address(record.target_address));
        out += ",";
        append_csv_value(out, traced_arguments(trace, record));
        out += ",";
        append_csv_value(out, traced_return(trace, record));
        out += ",";
        append_csv_value(out, raw_trace_arguments(trace, record));
        out += ",";
        append_csv_value(out, raw_trace_abi_arguments(trace, record));
        out += ",";
        append_csv_value(out, raw_traced_return(trace, record));
        out += "\n";
    }
    return out;
}

std::string trace_json(const MethodTracer::Snapshot &trace) {
    std::string out = "{\n  \"method\": ";
    append_json_value(out, trace.declaring_type + "." + trace.method_name);
    out += ",\n  \"totalCalls\": " + std::to_string(trace.total_calls);
    out += ",\n  \"overwrittenRecords\": " + std::to_string(trace.overwritten_records);
    out += ",\n  \"captureFaults\": " + std::to_string(trace.native_faults) + ",\n  \"records\": [\n";
    for (std::size_t index = 0; index < trace.records.size(); ++index) {
        const MethodTracer::Record &record = trace.records[index];
        const double seconds = trace.timestamp_frequency && record.timestamp_ticks >= trace.start_timestamp_ticks
                                   ? static_cast<double>(record.timestamp_ticks - trace.start_timestamp_ticks) /
                                         static_cast<double>(trace.timestamp_frequency)
                                   : 0.0;
        out += "    {\"sequence\": " + std::to_string(record.sequence) + ", \"seconds\": " + trace_seconds_json(seconds) +
               ", \"thread\": " + std::to_string(record.thread_id) + ", \"caller\": ";
        append_json_value(out, record.caller_display);
        out += ", \"callerAddress\": ";
        append_json_value(out, trace_address(record.caller_address));
        out += ", \"target\": ";
        append_json_value(out, record.target_display);
        out += ", \"targetAddress\": ";
        append_json_value(out, trace_address(record.target_address));
        out += ", \"arguments\": ";
        append_json_value(out, traced_arguments(trace, record));
        out += ", \"result\": ";
        append_json_value(out, traced_return(trace, record));
        out += ", \"rawArguments\": ";
        append_json_value(out, raw_trace_arguments(trace, record));
        out += ", \"rawAbi\": ";
        append_json_value(out, raw_trace_abi_arguments(trace, record));
        out += ", \"rawResult\": ";
        append_json_value(out, raw_traced_return(trace, record));
        out += "}";
        out += index + 1 == trace.records.size() ? "\n" : ",\n";
    }
    return out + "  ]\n}";
}

struct TraceViewState {
    std::array<char, 256> filter{};
    bool newest_first = true;
    bool show_summary = true;
    bool show_raw_abi = false;
};

TraceViewState &trace_view_state(MethodTracer::TraceId id) {
    static std::unordered_map<MethodTracer::TraceId, TraceViewState> states;
    return states[id];
}

std::string friendly_trace_caller(std::string_view caller) {
    if (caller.empty() || caller.find("GameAssembly.dll+") != std::string_view::npos)
        return "Game native code (method name could not be resolved)";
    if (caller == "<shared IL2CPP generic code>")
        return "shared generic IL2CPP code";
    return std::string(caller);
}

void render_method_trace(const MethodTracer::Snapshot &trace) {
    TraceViewState &state = trace_view_state(trace.id);
    ImGui::SeparatorText("Plain-language summary");
    ImGui::TextWrapped("%s.%s was observed %llu time%s.", trace.declaring_type.c_str(), trace.method_name.c_str(),
                       static_cast<unsigned long long>(trace.total_calls), trace.total_calls == 1 ? "" : "s");
    if (trace.records.empty()) {
        ImGui::TextDisabled("No call has been observed yet. The trace is attached and waiting.");
    } else {
        const MethodTracer::Record &latest = trace.records.back();
        ImGui::TextWrapped("Last call came from: %s", friendly_trace_caller(latest.caller_display).c_str());
        if (!trace.is_static)
            ImGui::TextWrapped("It ran on: %s", latest.target_display.empty() ? "an unavailable object" : latest.target_display.c_str());
        const std::string arguments = traced_arguments(trace, latest);
        ImGui::TextWrapped("It received: %s", arguments.empty() ? "no parameters" : arguments.c_str());
        ImGui::TextWrapped("It returned: %s", traced_return(trace, latest).c_str());
    }
    ImGui::Separator();
    if (trace.active) {
        ImGui::TextDisabled("%llu calls | %zu retained", static_cast<unsigned long long>(trace.total_calls),
                            trace.records.size());
    } else {
        ImGui::TextDisabled("Trace stopped: %llu calls recorded", static_cast<unsigned long long>(trace.total_calls));
    }
    if (trace.overwritten_records != 0) {
        ImGui::SameLine();
        ImGui::TextDisabled("| %llu older records overwritten", static_cast<unsigned long long>(trace.overwritten_records));
    }
    if (trace.native_faults != 0) {
        ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.30f, 1.0f), "%llu capture faults were isolated",
                           static_cast<unsigned long long>(trace.native_faults));
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear"))
        enqueue_method_trace_clear(trace.id);
    ImGui::SameLine();
    if (ImGui::SmallButton("Copy CSV"))
        ImGui::SetClipboardText(trace_csv(trace).c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("Copy JSON"))
        ImGui::SetClipboardText(trace_json(trace).c_str());
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Copies the retained trace records to the clipboard.");
    ImGui::SetNextItemWidth(240.0f);
    ImGui::InputTextWithHint("##trace-filter", "Filter caller, target or arguments...", state.filter.data(), state.filter.size());
    ImGui::SameLine();
    ImGui::Checkbox("Newest first", &state.newest_first);
    ImGui::SameLine();
    ImGui::Checkbox("Show raw ABI", &state.show_raw_abi);
    const std::string_view filter = state.filter.data();

    if (ImGui::CollapsingHeader("Technical details")) {
        ImGui::TextDisabled("Method metadata address: %s", trace.method_pointer_text.empty() ? "<unavailable>"
                                                                                              : trace.method_pointer_text.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Copy method address"))
            ImGui::SetClipboardText(trace.method_pointer_text.c_str());
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Copies the IL2CPP MethodInfo metadata address.");
        ImGui::TextDisabled("Raw ABI preserves arguments plus RAX/XMM0 return lanes. Managed references are shown as raw addresses; they are not rooted by a trace.");
    }

    std::unordered_map<std::string, std::size_t> caller_counts;
    std::unordered_map<std::uint32_t, std::size_t> thread_counts;
    double latest_elapsed = 0.0;
    for (const MethodTracer::Record &record : trace.records) {
        ++caller_counts[record.caller_display.empty() ? "<unresolved native caller>" : record.caller_display];
        ++thread_counts[record.thread_id];
        if (trace.timestamp_frequency && record.timestamp_ticks >= trace.start_timestamp_ticks)
            latest_elapsed = std::max(latest_elapsed, static_cast<double>(record.timestamp_ticks - trace.start_timestamp_ticks) /
                                                          static_cast<double>(trace.timestamp_frequency));
    }
    if (ImGui::CollapsingHeader("Trace statistics", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("Recorded calls: %llu  |  Shown here: %zu  |  Rate: %.2f/s  |  Callers: %zu  |  Threads: %zu",
                    static_cast<unsigned long long>(trace.total_calls), trace.records.size(),
                    latest_elapsed > 0.0 ? trace.total_calls / latest_elapsed : 0.0, caller_counts.size(), thread_counts.size());
        std::vector<std::pair<std::string, std::size_t>> callers(caller_counts.begin(), caller_counts.end());
        std::sort(callers.begin(), callers.end(), [](const auto &left, const auto &right) { return left.second > right.second; });
        const std::size_t shown = std::min<std::size_t>(callers.size(), 6);
        for (std::size_t index = 0; index < shown; ++index)
            ImGui::BulletText("%zu × %s", callers[index].second, callers[index].first.c_str());
        if (callers.size() > shown)
            ImGui::TextDisabled("%zu additional caller sites", callers.size() - shown);
    }
    if (trace.records.empty()) {
        ImGui::TextDisabled("Waiting for a call...");
        return;
    }
    if (ImGui::BeginTable("##method-trace", 7,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp |
                              ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY,
                          ImVec2(0, std::max(180.0f, ImGui::GetContentRegionAvail().y)))) {
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 42.0f);
        ImGui::TableSetupColumn("When", ImGuiTableColumnFlags_WidthFixed, 74.0f);
        ImGui::TableSetupColumn("Thread", ImGuiTableColumnFlags_WidthFixed, 62.0f);
        ImGui::TableSetupColumn("Called from / object", ImGuiTableColumnFlags_WidthStretch, 1.1f);
        ImGui::TableSetupColumn("Arguments sent", ImGuiTableColumnFlags_WidthStretch, 1.6f);
        ImGui::TableSetupColumn("Result", ImGuiTableColumnFlags_WidthStretch, 1.1f);
        ImGui::TableSetupColumn("Addresses", ImGuiTableColumnFlags_WidthFixed, 106.0f);
        ImGui::TableHeadersRow();
        for (std::size_t displayed = 0; displayed < trace.records.size(); ++displayed) {
            const std::size_t record_index = state.newest_first ? trace.records.size() - 1 - displayed : displayed;
            const MethodTracer::Record &record = trace.records[record_index];
            const std::string arguments = traced_arguments(trace, record);
            const std::string result = traced_return(trace, record);
            if (!filter.empty() && !contains_case_insensitive(record.caller_display, filter) &&
                !contains_case_insensitive(record.target_display, filter) && !contains_case_insensitive(arguments, filter) &&
                !contains_case_insensitive(result, filter))
                continue;
            const double elapsed = trace.timestamp_frequency && record.timestamp_ticks >= trace.start_timestamp_ticks
                                       ? static_cast<double>(record.timestamp_ticks - trace.start_timestamp_ticks) /
                                             static_cast<double>(trace.timestamp_frequency)
                                       : 0.0;
            ImGui::PushID(static_cast<int>(record.sequence));
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%llu", static_cast<unsigned long long>(record.sequence));
            ImGui::TableSetColumnIndex(1);
            const std::string elapsed_text = trace_elapsed_text(elapsed);
            ImGui::TextUnformatted(elapsed_text.c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%u", record.thread_id);
            ImGui::TableSetColumnIndex(3);
            ImGui::TextUnformatted(record.caller_display.empty() ? "<unknown caller>" : record.caller_display.c_str());
            if (!trace.is_static) {
                ImGui::SameLine();
                ImGui::TextDisabled("this=%s", record.target_display.empty() ? "<unavailable>" : record.target_display.c_str());
            }
            ImGui::TableSetColumnIndex(4);
            ImGui::TextUnformatted(arguments.empty() ? "-" : arguments.c_str());
            if (state.show_raw_abi) {
                const std::string raw_abi = raw_trace_abi_arguments(trace, record);
                ImGui::TextDisabled("%s", raw_abi.empty() ? "<no ABI arguments>" : raw_abi.c_str());
            }
            if (ImGui::BeginPopupContextItem("##trace-arguments-actions")) {
                const std::string raw_arguments = raw_trace_abi_arguments(trace, record);
                ImGui::TextDisabled("Raw ABI argument values");
                ImGui::TextWrapped("%s", raw_arguments.empty() ? "<none>" : raw_arguments.c_str());
                if (ImGui::MenuItem("Copy raw argument values"))
                    ImGui::SetClipboardText(raw_arguments.c_str());
                if (record.return_captured && ImGui::MenuItem("Copy raw return value"))
                    ImGui::SetClipboardText(raw_traced_return(trace, record).c_str());
                if (record.return_captured && trace.return_is_reference && record.return_rax != 0) {
                    if (ImGui::MenuItem("Inspect returned reference"))
                        enqueue_raw_reference_inspection(record.return_rax);
                }
                for (std::size_t argument_index = 0; argument_index < record.arguments.size(); ++argument_index) {
                    if (argument_index >= trace.parameter_is_reference.size() || !trace.parameter_is_reference[argument_index] ||
                        record.arguments[argument_index] == 0)
                        continue;
                    const std::string name = argument_index < trace.parameter_names.size()
                                                 ? trace.parameter_names[argument_index]
                                                 : "argument " + std::to_string(argument_index);
                    const std::string label = "Inspect " + name;
                    if (ImGui::MenuItem(label.c_str()))
                        enqueue_raw_reference_inspection(record.arguments[argument_index]);
                }
                ImGui::EndPopup();
            }
            ImGui::TableSetColumnIndex(5);
            ImGui::TextUnformatted(result.c_str());
            if (state.show_raw_abi)
                ImGui::TextDisabled("%s", raw_traced_return(trace, record).c_str());
            ImGui::TableSetColumnIndex(6);
            const std::string caller_address = trace_address(record.caller_address);
            if (ImGui::SmallButton("Copy caller"))
                ImGui::SetClipboardText(caller_address.c_str());
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", caller_address.c_str());
            if (!trace.is_static) {
                ImGui::SameLine();
                const std::string target_address = trace_address(record.target_address);
                if (ImGui::SmallButton("Copy this"))
                    ImGui::SetClipboardText(target_address.c_str());
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", target_address.c_str());
                ImGui::SameLine();
                if (ImGui::SmallButton("Inspect this"))
                    enqueue_raw_reference_inspection(record.target_address);
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

void enqueue_method_trace_stop(MethodTracer::TraceId id) {
    Command command{};
    command.kind = CommandKind::SetMethodTrace;
    command.bool_value = false;
    command.reference_token = id;
    RuntimeModel::instance().enqueue(std::move(command));
}

void enqueue_method_trace_close(MethodTracer::TraceId id) {
    Command command{};
    command.kind = CommandKind::CloseMethodTrace;
    command.reference_token = id;
    RuntimeModel::instance().enqueue(std::move(command));
}

std::string short_trace_type_name(std::string_view type_name) {
    const std::size_t separator = type_name.rfind('.');
    return separator == std::string_view::npos ? std::string(type_name) : std::string(type_name.substr(separator + 1));
}

void render_method_traces(const Snapshot &snapshot) {
    if (snapshot.method_traces.empty()) {
        ImGui::TextDisabled("No traced methods. Use Trace next to a method to start one.");
        return;
    }
    static MethodTracer::TraceId selected = 0;
    const auto selected_found = std::find_if(snapshot.method_traces.begin(), snapshot.method_traces.end(),
                                             [](const MethodTracer::Snapshot &trace) { return trace.id == selected; });
    if (selected_found == snapshot.method_traces.end())
        selected = snapshot.method_traces.front().id;

    ImGui::BeginChild("##method-trace-list", ImVec2(245.0f, 0.0f), true);
    ImGui::TextDisabled("WATCHED METHODS");
    ImGui::Separator();
    for (const MethodTracer::Snapshot &trace : snapshot.method_traces) {
        const std::string display_name = short_trace_type_name(trace.declaring_type) + "." + trace.method_name;
        const std::string label = (trace.active ? "● " : "○ ") + display_name;
        if (ImGui::Selectable(label.c_str(), selected == trace.id))
            selected = trace.id;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s.%s\n%s | %llu calls", trace.declaring_type.c_str(), trace.method_name.c_str(),
                              trace.active ? "Recording" : "Stopped", static_cast<unsigned long long>(trace.total_calls));
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##method-trace-detail", ImVec2(0.0f, 0.0f), true);
    const MethodTracer::Snapshot *trace = &*std::find_if(snapshot.method_traces.begin(), snapshot.method_traces.end(),
                                                           [](const MethodTracer::Snapshot &entry) { return entry.id == selected; });
    ImGui::TextColored(trace->active ? ImVec4(0.42f, 0.88f, 0.82f, 1.0f) : ImVec4(0.72f, 0.72f, 0.72f, 1.0f),
                       "%s.%s", trace->declaring_type.c_str(), trace->method_name.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("[%s]", trace->active ? "RECORDING" : "STOPPED");
    ImGui::SameLine();
    if (trace->active && ImGui::SmallButton("Stop"))
        enqueue_method_trace_stop(trace->id);
    if (!trace->active) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Close"))
            enqueue_method_trace_close(trace->id);
    }
    render_method_trace(*trace);
    ImGui::EndChild();
}

std::string short_field_component_name(std::string_view type_name) {
    const std::size_t separator = type_name.rfind('.');
    return separator == std::string_view::npos ? std::string(type_name) : std::string(type_name.substr(separator + 1));
}

void render_field_watches(const Snapshot &snapshot) {
    if (snapshot.field_watches.empty()) {
        ImGui::TextDisabled("No watched fields. Use Watch next to an instance field to start one.");
        ImGui::TextWrapped("A watch samples the value four times per second and records old -> new changes.");
        return;
    }
    static std::uint64_t selected = 0;
    const auto selected_found = std::find_if(snapshot.field_watches.begin(), snapshot.field_watches.end(),
                                             [](const Snapshot::FieldWatch &watch) {
                                                 return watch.id == selected;
                                             });
    if (selected_found == snapshot.field_watches.end())
        selected = snapshot.field_watches.front().id;

    ImGui::BeginChild("##field-watch-list", ImVec2(245.0f, 0.0f), true);
    ImGui::TextDisabled("WATCHED FIELDS");
    ImGui::Separator();
    for (const Snapshot::FieldWatch &watch : snapshot.field_watches) {
        const std::string display_name = short_field_component_name(watch.component_type) + "." + watch.field_name;
        const std::string label = (watch.active ? "* " : "o ") + display_name;
        if (ImGui::Selectable(label.c_str(), selected == watch.id))
            selected = watch.id;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s.%s\n%s | %llu recorded changes", watch.component_type.c_str(), watch.field_name.c_str(),
                              watch.active ? "Watching" : "Stopped",
                              static_cast<unsigned long long>(watch.change_count));
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##field-watch-detail", ImVec2(0.0f, 0.0f), true);
    const Snapshot::FieldWatch *watch =
        &*std::find_if(snapshot.field_watches.begin(), snapshot.field_watches.end(), [](const auto &entry) {
            return entry.id == selected;
        });
    ImGui::TextColored(watch->active ? ImVec4(0.48f, 0.84f, 0.54f, 1.0f) : ImVec4(0.72f, 0.72f, 0.72f, 1.0f),
                       "%s.%s", watch->component_type.c_str(), watch->field_name.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("[%s]", watch->active ? "WATCHING" : "STOPPED");
    ImGui::SameLine();
    if (watch->active && ImGui::SmallButton("Stop"))
        enqueue_field_watch(watch->component_instance_id, static_cast<int>(watch->field_index), false);
    if (!watch->active) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Close"))
            enqueue_field_watch_close(watch->id);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear history"))
        enqueue_field_watch_clear(watch->id);

    ImGui::SeparatorText("What changed?");
    ImGui::TextWrapped("This field has changed %llu time%s. Current value: %s",
                       static_cast<unsigned long long>(watch->change_count), watch->change_count == 1 ? "" : "s",
                       watch->current_value.empty() ? "<waiting for a sample>" : watch->current_value.c_str());
    if (!watch->current_reference.is_null && watch->current_reference.token != 0) {
        ImGui::SameLine();
        render_reference_button(&watch->current_reference);
    }
    ImGui::TextDisabled("Samples every 0.25 seconds. It shows the value change, not the native code that wrote it.");
    if (watch->events.empty()) {
        ImGui::TextDisabled(watch->active ? "Waiting for the value to change..." : "No changes were recorded.");
        ImGui::EndChild();
        return;
    }
    if (ImGui::BeginTable("##field-watch-events", 4,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp |
                              ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY,
                          ImVec2(0, std::max(150.0f, ImGui::GetContentRegionAvail().y)))) {
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 42.0f);
        ImGui::TableSetupColumn("When", ImGuiTableColumnFlags_WidthFixed, 82.0f);
        ImGui::TableSetupColumn("Old value", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("New value", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();
        for (std::size_t displayed = 0; displayed < watch->events.size(); ++displayed) {
            const Snapshot::FieldWatchEvent &event = watch->events[watch->events.size() - 1 - displayed];
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%llu", static_cast<unsigned long long>(event.sequence));
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("+%.3fs", event.seconds_since_start);
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(event.previous_value.c_str());
            ImGui::TableSetColumnIndex(3);
            ImGui::TextUnformatted(event.current_value.c_str());
            if (!event.current_reference.is_null && event.current_reference.token != 0) {
                ImGui::SameLine();
                ImGui::PushID(static_cast<int>(event.sequence));
                render_reference_button(&event.current_reference);
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();
}

void render_method_result(const Snapshot &snapshot, int component_id, std::size_t method_index,
                          std::uint64_t object_inspector_token = 0) {
    const auto found =
        std::find_if(snapshot.method_results.begin(), snapshot.method_results.end(), [=](const auto &entry) {
            const Snapshot::MethodResult &result = entry.second;
            return result.component_instance_id == component_id && result.method_index == method_index &&
                   result.object_inspector_token == object_inspector_token;
        });
    if (found == snapshot.method_results.end())
        return;
    const Snapshot::MethodResult &result = found->second;
    ImGui::SameLine();
    ImGui::TextDisabled("Returns:");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.45f, 0.82f, 0.92f, 1.0f), "%s", result.display.c_str());
    if (!result.reference.is_null && result.reference.token != 0) {
        ImGui::SameLine();
        render_reference_button(&result.reference);
    }
}

struct ComponentTabState {
    int activate_component_id = 0;
    std::vector<int> open_component_ids;
};

struct ObjectTabState {
    struct Tab {
        int instance_id = 0;
        std::string name;
    };
    std::vector<Tab> tabs;
    std::unordered_set<int> closed_instance_ids;
    int last_seen_instance_id = 0;
    int activate_instance_id = 0;
};

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

void request_object_reference_tab(std::uint64_t token) {
    if (token == 0)
        return;
    ObjectReferenceTabState &state = object_reference_tabs();
    // An explicit Inspect click is a new open request.  Do not let the
    // previous close action suppress the same reference token forever.
    state.closed_tokens.erase(token);
    state.requested_tokens.erase(token);
    state.pending_activation_token = token;
    const auto found = std::find_if(state.tabs.begin(), state.tabs.end(),
                                    [token](const ObjectReferenceTabState::Tab &tab) { return tab.token == token; });
    if (found != state.tabs.end())
        state.selected_token = token;
}

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
    // Only an explicit Inspect action selects an existing tab.
    if (state.pending_activation_token == info.token || (added && state.tabs.size() == 1)) {
        state.selected_token = info.token;
        state.pending_activation_token = 0;
    }
}

void close_object_reference_tab(std::uint64_t token) {
    if (token == 0)
        return;
    Command command{};
    command.kind = CommandKind::CloseObjectInspectorTab;
    command.object_inspector_token = token;
    RuntimeModel::instance().enqueue(std::move(command));
}

ObjectTabState &object_tabs() {
    static ObjectTabState state;
    return state;
}

void remember_object_tab(const InspectorInfo &info) {
    if (!info.valid)
        return;
    ObjectTabState &state = object_tabs();
    const bool changed_selection = state.last_seen_instance_id != info.instance_id;
    if (changed_selection) {
        state.closed_instance_ids.erase(info.instance_id);
    }
    state.last_seen_instance_id = info.instance_id;
    if (state.closed_instance_ids.contains(info.instance_id))
        return;
    const auto found = std::find_if(state.tabs.begin(), state.tabs.end(), [&info](const ObjectTabState::Tab &tab) {
        return tab.instance_id == info.instance_id;
    });
    if (found == state.tabs.end()) {
        state.tabs.push_back({info.instance_id, info.name});
        state.activate_instance_id = info.instance_id;
    } else {
        found->name = info.name;
        if (changed_selection)
            state.activate_instance_id = info.instance_id;
    }
}

std::unordered_map<int, ComponentTabState> &component_tabs() {
    static std::unordered_map<int, ComponentTabState> states;
    return states;
}

ComponentTabState &component_tab_state(int owner_instance_id) {
    auto &states = component_tabs();
    if (const auto found = states.find(owner_instance_id); found != states.end())
        return found->second;
    if (states.size() >= 128)
        states.clear();
    return states[owner_instance_id];
}

void open_component_tab(int owner_instance_id, int component_instance_id) {
    ComponentTabState &state = component_tab_state(owner_instance_id);
    if (std::find(state.open_component_ids.begin(), state.open_component_ids.end(), component_instance_id) ==
        state.open_component_ids.end())
        state.open_component_ids.push_back(component_instance_id);
    state.activate_component_id = component_instance_id;
}

void render_component_list(const InspectorInfo &info) {
    ImGui::SeparatorText("Components");
    if (info.components.empty()) {
        ImGui::TextDisabled("No additional components");
        return;
    }
    for (const ComponentInfo &component : info.components) {
        ImGui::PushID(component.instance_id);
        if (component.enabled_supported) {
            bool enabled = component.enabled;
            if (ImGui::Checkbox("##enabled", &enabled)) {
                Command command{.kind = CommandKind::SetComponentEnabled, .instance_id = component.instance_id};
                command.bool_value = enabled;
                RuntimeModel::instance().enqueue(std::move(command));
            }
            ImGui::SameLine();
        }
        if (ImGui::Selectable(component.type_name.c_str(), false, ImGuiSelectableFlags_None,
                              ImVec2(0.0f, ImGui::GetFrameHeight())))
            open_component_tab(info.instance_id, component.instance_id);
        ImGui::PopID();
    }
}

void render_components(const InspectorInfo &info, const Snapshot &snapshot, int only_component_id = 0,
                       const char *fixed_filter = nullptr, bool show_inherited = false) {
    const bool live_data = snapshot.live_data;
    ImGui::SeparatorText("Component Inspector");
    for (const ComponentInfo &component : info.components) {
        if (only_component_id != 0 && component.instance_id != only_component_id)
            continue;
        ImGui::PushID(component.instance_id);
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
        const bool open = ImGui::TreeNodeEx("##component",
                                            ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick |
                                                (only_component_id != 0 ? ImGuiTreeNodeFlags_DefaultOpen : 0),
                                            "%s", component.type_name.c_str());
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(110, 48, 48, 210));
        if (ImGui::SmallButton("Delete"))
            enqueue_simple(CommandKind::DeleteComponent, component.instance_id);
        ImGui::PopStyleColor();
        ImGui::SameLine();
        if (ImGui::SmallButton("Copy Ptr"))
            ImGui::SetClipboardText(component.pointer_text.c_str());
        if (open) {
            render_type_details("Component Type", component.assembly_name, component.namespace_name,
                                component.class_name, component.type_name);
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
                    ImGui::TextColored(ImVec4(1.0f, 0.48f, 0.34f, 1.0f), "%s",
                                       component.metadata_error.empty()
                                           ? "Reflection failed for this component."
                                           : component.metadata_error.c_str());
                    if (ImGui::SmallButton("Retry metadata load"))
                        enqueue_simple(CommandKind::LoadComponentMetadata, component.instance_id);
                } else if (!component.metadata_error.empty()) {
                    ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.32f, 1.0f), "Metadata diagnostics: %s",
                                       component.metadata_error.c_str());
                }
                if (component.dynamic_bridge.detected) {
                    ImGui::SeparatorText("Dynamic Script Bridge");
                    ImGui::TextDisabled("Bridge type: %s", component.type_name.c_str());
					if (!component.dynamic_bridge.behaviour_type.empty())
						ImGui::TextColored(ImVec4(0.56f, 0.88f, 0.72f, 1.0f), "Behaviour: %s",
												   component.dynamic_bridge.behaviour_type.c_str());
					else if (!component.dynamic_bridge.type_getter.empty())
						ImGui::TextDisabled("Behaviour type is read only on request.");
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
                    if (!component.dynamic_bridge.serialized_data_methods.empty()) {
                        ImGui::TextDisabled("Serialized data access:");
                        for (const std::string& method : component.dynamic_bridge.serialized_data_methods)
                            ImGui::BulletText("%s", method.c_str());
                    }
                    if (!component.dynamic_bridge.object_reference_methods.empty()) {
                        ImGui::TextDisabled("Object reference access:");
                        for (const std::string& method : component.dynamic_bridge.object_reference_methods)
                            ImGui::BulletText("%s", method.c_str());
                    }
                    if (!component.dynamic_bridge.diagnostic.empty())
                        ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.32f, 1.0f), "Bridge diagnostics: %s",
                                           component.dynamic_bridge.diagnostic.c_str());
                    ImGui::TextDisabled("Raw serialized data is intentionally not decoded until its format is known.");
                }
                std::array<char, 128> &filter_buffer = component_filter(component.instance_id);
                if (!fixed_filter) {
                    ImGui::SetNextItemWidth(-1.0f);
                    ImGui::InputTextWithHint("##member-filter", "Filter fields, properties and methods...",
                                             filter_buffer.data(), filter_buffer.size());
                }
                const std::string_view filter(fixed_filter ? fixed_filter : filter_buffer.data());
                // Report the reflected total as well as the filtered count.
                const auto visible_member_count = [&](const auto &members) {
                    return static_cast<std::size_t>(
                        std::count_if(members.begin(), members.end(), [&](const auto &member) {
                            const bool declared_here =
                                member.declaring_type.empty() || member.declaring_type == component.type_name;
                            return (show_inherited || declared_here) &&
                                   member_matches_filter(member.name, member.type_name, filter);
                        }));
                };
                const auto visible_method_count = [&] {
                    return static_cast<std::size_t>(
                        std::count_if(metadata.methods.begin(), metadata.methods.end(), [&](const auto &method) {
                            const bool declared_here =
                                method.declaring_type.empty() || method.declaring_type == component.type_name;
                            return (show_inherited || declared_here) &&
                                   member_matches_filter(method.name, method.return_type, filter);
                        }));
                };
                auto member_table = [&](const char *id, const auto &members, const auto *values, CommandKind command,
                                        bool properties) {
                    if (!ImGui::BeginTable(id, properties ? 2 : 3,
                                           ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp |
                                               ImGuiTableFlags_NoPadOuterX))
                        return;
                    ImGui::TableSetupColumn("Member", ImGuiTableColumnFlags_WidthFixed, 190.0f);
                    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
                    if (!properties)
                        ImGui::TableSetupColumn("Watch", ImGuiTableColumnFlags_WidthFixed, 82.0f);
                    std::vector<std::size_t> visible_members;
                    visible_members.reserve(members.size());
                    for (std::size_t index = 0; index < members.size(); ++index) {
                        const bool declared_here = members[index].declaring_type.empty() ||
                                                   members[index].declaring_type == component.type_name;
                        if ((show_inherited || declared_here) &&
                            member_matches_filter(members[index].name, members[index].type_name, filter))
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
                            if constexpr (requires { member.can_write; })
                                render_property_context_menu(member, component_code);
                            else
                                render_field_context_menu(member, component_code);
                            if (ImGui::IsItemHovered())
                                ImGui::SetTooltip("%s\nRight-click for copy/code options", member.type_name.c_str());
                            if constexpr (requires { member.is_static; }) {
                                if (member.is_static)
                                    ImGui::SameLine(), ImGui::TextDisabled("static");
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
                                              member.capability_reason);
                            if (!properties) {
                                const Snapshot::FieldWatch *watch = field_watch_for(snapshot, component.instance_id, index);
                                ImGui::TableSetColumnIndex(2);
                                const char *label = watch && watch->active ? "Stop watch" : "Watch";
                                if (ImGui::SmallButton(label))
                                    enqueue_field_watch(component.instance_id, static_cast<int>(index),
                                                        !(watch && watch->active));
                                if (ImGui::IsItemHovered())
                                    ImGui::SetTooltip(watch && watch->active
                                                          ? "Stop recording changes for this field"
                                                          : "Record old -> new changes for this field");
                            }
                            ImGui::PopID();
                        }
                    clipper.End();
                    ImGui::EndTable();
                };

                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.40f, 0.76f, 0.96f, 1.0f));
                const std::size_t visible_fields = visible_member_count(metadata.fields);
                const bool fields_open =
                    ImGui::TreeNode("##fields-section", "Fields (%zu / %zu)", visible_fields, metadata.fields.size());
                ImGui::PopStyleColor();
                if (fields_open) {
                    member_table("##fields", metadata.fields, live ? &live->fields : nullptr,
                                 CommandKind::SetFieldValue, false);
                    ImGui::TreePop();
                }
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.86f, 0.67f, 1.0f));
                const std::size_t visible_properties = visible_member_count(metadata.properties);
                const bool properties_open = ImGui::TreeNode("##properties-section", "Properties (%zu / %zu)",
                                                             visible_properties, metadata.properties.size());
                ImGui::PopStyleColor();
                if (properties_open) {
                    member_table("##properties", metadata.properties, live ? &live->properties : nullptr,
                                 CommandKind::SetPropertyValue, true);
                    ImGui::TreePop();
                }
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.92f, 0.73f, 0.40f, 1.0f));
                const std::size_t visible_methods = visible_method_count();
                const bool methods_open = ImGui::TreeNode("##methods-section", "Methods (%zu / %zu)", visible_methods,
                                                          metadata.methods.size());
                ImGui::PopStyleColor();
                if (methods_open) {
                    for (std::size_t index = 0; index < metadata.methods.size(); ++index) {
                        const auto &method = metadata.methods[index];
                        const bool declared_here =
                            method.declaring_type.empty() || method.declaring_type == component.type_name;
                        if (!show_inherited && !declared_here)
                            continue;
                        if (!member_matches_filter(method.name, method.return_type, filter))
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
                        if (method_open && !method.parameter_types.empty()) {
                            ImGui::Indent();
                            for (std::size_t parameter = 0; parameter < method.parameter_types.size(); ++parameter) {
                                const std::string &type = method.parameter_types[parameter];
                                const std::string name = parameter < method.parameter_names.size() &&
                                                                 !method.parameter_names[parameter].empty()
                                                             ? method.parameter_names[parameter]
                                                             : "arg" + std::to_string(parameter + 1);
                                ImGui::PushID(static_cast<int>(parameter));
                                render_method_argument(component.instance_id, index, parameter, type, name);
                                ImGui::PopID();
                            }
                            ImGui::Unindent();
                        }
                        if (method_open) {
                            const MethodTracer::Snapshot *trace = trace_for_method(snapshot.method_traces, method);
                            if (trace && trace->active) {
                                if (ImGui::SmallButton("Stop tracing"))
                                    enqueue_method_trace(component.instance_id, static_cast<int>(index), false);
                            } else if (ImGui::SmallButton("Trace")) {
                                enqueue_method_trace(component.instance_id, static_cast<int>(index), true);
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
                    ImGui::TreePop();
                }
            }
        }
        if (open)
            ImGui::TreePop();
        ImGui::PopID();
    }
    if (info.components.empty())
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

    ComponentTabState &tabs = component_tab_state(info.instance_id);
    ImGui::TextDisabled("Selected GameObject:");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.45f, 0.82f, 0.96f, 1.0f), "%s", info.name.c_str());
    ImGui::SameLine();
    if (info.camera_distance_valid)
        ImGui::TextColored(ImVec4(0.58f, 0.88f, 0.70f, 1.0f), "%.1f units away", info.camera_distance);
    else
        ImGui::TextDisabled("distance unavailable");
    if (ImGui::SmallButton("Focus camera"))
        enqueue_simple(CommandKind::FocusSelected, info.instance_id);
    ImGui::SameLine();
    ImGui::BeginDisabled(!snapshot.camera_focus_active);
    if (ImGui::SmallButton("Return camera"))
        enqueue_simple(CommandKind::RestoreCamera, 0);
    ImGui::EndDisabled();
    if (snapshot.camera_focus_active) {
        ImGui::SameLine();
        ImGui::TextDisabled("temporary focus");
    }
    // Component tabs use a separate accent from GameObject tabs.
    ImGui::PushStyleColor(ImGuiCol_Tab, ImVec4(0.20f, 0.26f, 0.24f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_TabHovered, ImVec4(0.26f, 0.47f, 0.38f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_TabSelected, ImVec4(0.16f, 0.58f, 0.42f, 1.0f));
    if (ImGui::BeginTabBar("##inspector-tabs", ImGuiTabBarFlags_FittingPolicyScroll)) {
        if (ImGui::BeginTabItem("GameObject")) {
            render_identity(info);
            render_transform(info);
            render_component_list(info);
            render_add_component_popup(info, snapshot);
            ImGui::EndTabItem();
        }
        for (auto it = tabs.open_component_ids.begin(); it != tabs.open_component_ids.end();) {
            const auto found =
                std::find_if(info.components.begin(), info.components.end(),
                             [id = *it](const ComponentInfo &component) { return component.instance_id == id; });
            if (found == info.components.end()) {
                it = tabs.open_component_ids.erase(it);
                continue;
            }
            bool open = true;
            ImGuiTabItemFlags flags =
                tabs.activate_component_id == *it ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
            const std::string label = found->type_name + "##component-tab-" + std::to_string(found->instance_id);
            if (ImGui::BeginTabItem(label.c_str(), &open, flags)) {
                std::array<char, 128> &filter = component_filter(found->instance_id);
                bool &show_inherited = component_show_inherited(found->instance_id);
                constexpr float scope_width = 190.0f;
                const float available_width = ImGui::GetContentRegionAvail().x;
                const bool scope_fits_inline = available_width >= scope_width + 150.0f;
                ImGui::SetNextItemWidth(
                    scope_fits_inline ? available_width - scope_width - ImGui::GetStyle().ItemSpacing.x : -1.0f);
                ImGui::InputTextWithHint("##sticky-member-filter", "Filter fields, properties and methods...",
                                         filter.data(), filter.size());
                if (scope_fits_inline)
                    ImGui::SameLine();
                ImGui::SetNextItemWidth(scope_fits_inline ? scope_width : -1.0f);
                if (ImGui::BeginCombo("##component-member-scope",
                                      show_inherited ? "All members" : "Declared members only")) {
                    if (ImGui::Selectable("All members (including inherited)", show_inherited))
                        show_inherited = true;
                    if (ImGui::Selectable("Declared members only", !show_inherited))
                        show_inherited = false;
                    ImGui::EndCombo();
                }
                ImGui::BeginChild("##component-tab-scroll", ImVec2(0.0f, 0.0f), false,
                                  ImGuiWindowFlags_AlwaysVerticalScrollbar);
                render_components(info, snapshot, found->instance_id, filter.data(), show_inherited);
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            if (!open)
                it = tabs.open_component_ids.erase(it);
            else
                ++it;
        }
        tabs.activate_component_id = 0;
        ImGui::EndTabBar();
    }
    ImGui::PopStyleColor(3);
    ImGui::EndChild();
}

void render_inspector(const Snapshot &snapshot) {
    ObjectTabState &tabs = object_tabs();
    if (snapshot.inspector.valid)
        remember_object_tab(snapshot.inspector);
    else
        tabs.last_seen_instance_id = 0;

    // A click on the empty Hierarchy area intentionally clears the selection.
    // The tab history is kept so objects can be revisited, but an empty
    // selection is not an asynchronous load.  Rendering the tab bar here
    // would make every remembered tab look stuck on "Loading..." until a new
    // GameObject is selected.
    if (!snapshot.inspector.valid) {
        render_current_inspector(snapshot);
        return;
    }

    if (tabs.tabs.empty()) {
        render_current_inspector(snapshot);
        return;
    }
    if (ImGui::BeginTabBar("##game-object-tabs", ImGuiTabBarFlags_FittingPolicyScroll)) {
        int select_after_close = 0;
        for (auto it = tabs.tabs.begin(); it != tabs.tabs.end();) {
            bool open = true;
            const int instance_id = it->instance_id;
            const ImGuiTabItemFlags flags =
                tabs.activate_instance_id == instance_id ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
            const std::string label = it->name + "##game-object-tab-" + std::to_string(instance_id);
            ImGui::PushStyleColor(ImGuiCol_Tab, ImVec4(0.16f, 0.27f, 0.38f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_TabHovered, ImVec4(0.24f, 0.48f, 0.68f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_TabSelected, ImVec4(0.08f, 0.53f, 0.82f, 1.0f));
            if (ImGui::BeginTabItem(label.c_str(), &open, flags)) {
                if (!snapshot.inspector.valid || snapshot.inspector.instance_id != instance_id) {
                    // ImGui's visible tab can lag the model selection.
                    const bool user_clicked_tab =
                        ImGui::IsItemClicked(ImGuiMouseButton_Left) ||
                        (ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Left));
                    if (user_clicked_tab)
                        enqueue_simple(CommandKind::Select, instance_id);
                    ImGui::TextDisabled("Loading %s...", it->name.c_str());
                } else {
                    render_current_inspector(snapshot);
                }
                ImGui::EndTabItem();
            }
            ImGui::PopStyleColor(3);
            if (!open) {
                const bool was_active = snapshot.inspector.valid && snapshot.inspector.instance_id == instance_id;
                tabs.closed_instance_ids.insert(instance_id);
                it = tabs.tabs.erase(it);
                if (was_active && !tabs.tabs.empty())
                    select_after_close = tabs.tabs.front().instance_id;
                else if (was_active)
                    select_after_close = -1;
            } else {
                ++it;
            }
        }
        if (select_after_close != 0)
            enqueue_simple(CommandKind::ClearSelection, 0);
        tabs.activate_instance_id = 0;
        ImGui::EndTabBar();
    }
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
    if (!info.pointer_text.empty()) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Copy Ptr"))
            ImGui::SetClipboardText(info.pointer_text.c_str());
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
                                  snapshot.live_data, false, false, info.token);
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
    auto render_members = [&](const char *id, const auto &members, const auto *values, const auto *references) {
        if (!ImGui::BeginTable(
                id, 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoPadOuterX))
            return;
        ImGui::TableSetupColumn("Member", ImGuiTableColumnFlags_WidthFixed, 190.0f);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(members.size()), ImGui::GetFrameHeightWithSpacing());
        while (clipper.Step())
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                const std::size_t index = static_cast<std::size_t>(row);
                const auto &member = members[index];
                const auto *value = values && index < values->size() ? &(*values)[index] : nullptr;
                const auto *reference = references && index < references->size() ? &(*references)[index] : nullptr;
                ImGui::PushID(static_cast<int>(index));
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(member.name.c_str());
                if constexpr (requires { member.can_write; })
                    render_property_context_menu(member, object_code);
                else
                    render_field_context_menu(member, object_code);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s\nRight-click for copy/code options", member.type_name.c_str());
                ImGui::TableSetColumnIndex(1);
                bool writable = !info.is_value_type || info.value_origin_component_id != 0;
                if constexpr (requires { member.can_write; })
                    writable = writable && member.can_write;
                const bool properties = requires { member.can_write; };
                const std::uint64_t key =
                    scoped_ui_key(info.token, properties ? 0x6100000000000000ull : 0x6000000000000000ull, index);
                render_live_value(properties ? CommandKind::SetPropertyValue : CommandKind::SetFieldValue, 0,
                                  static_cast<int>(index), value, writable, key, reference, true, snapshot.live_data,
                                  snapshot.locked_member_keys.contains(key), true, info.token, member.runtime_safe,
                                  member.capability_reason);
                ImGui::PopID();
            }
        clipper.End();
        ImGui::EndTable();
    };
    if (ImGui::TreeNode("##object-fields", "Fields (%zu)", metadata.fields.size())) {
        render_members("##object-field-table", metadata.fields, live ? &live->fields : nullptr,
                       live ? &live->field_references : nullptr);
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("##object-properties", "Properties (%zu)", metadata.properties.size())) {
        render_members("##object-property-table", metadata.properties, live ? &live->properties : nullptr,
                       live ? &live->property_references : nullptr);
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("##object-methods", "Methods (%zu)", metadata.methods.size())) {
        for (std::size_t index = 0; index < metadata.methods.size(); ++index) {
            const ComponentInfo::Method &method = metadata.methods[index];
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
            if (!method.parameter_types.empty()) {
                ImGui::Indent();
                for (std::size_t parameter = 0; parameter < method.parameter_types.size(); ++parameter) {
                    const std::string &type = method.parameter_types[parameter];
                    const std::string name =
                        parameter < method.parameter_names.size() && !method.parameter_names[parameter].empty()
                            ? method.parameter_names[parameter]
                            : "arg" + std::to_string(parameter + 1);
                    ImGui::PushID(static_cast<int>(parameter));
                    render_method_argument(0, index, parameter, type, name, info.token);
                    ImGui::PopID();
                }
                ImGui::Unindent();
            }
            const MethodTracer::Snapshot *trace = trace_for_method(snapshot.method_traces, method);
            if (trace && trace->active) {
                if (ImGui::SmallButton("Stop tracing"))
                    enqueue_method_trace(0, static_cast<int>(index), false, true, info.token);
            } else if (ImGui::SmallButton("Trace")) {
                enqueue_method_trace(0, static_cast<int>(index), true, true, info.token);
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(!invokable_method(method));
            if (ImGui::SmallButton("Execute"))
                enqueue_method_invoke(0, static_cast<int>(index), method, true, info.token);
            ImGui::EndDisabled();
            render_method_result(snapshot, 0, index, info.token);
            ImGui::PopID();
        }
        ImGui::TreePop();
    }
    ImGui::EndChild();
}

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
    // Keep object-tab selection explicit while content is updated asynchronously.
    if (tabs.selected_token == 0 ||
        std::none_of(tabs.tabs.begin(), tabs.tabs.end(),
                     [&tabs](const ObjectReferenceTabState::Tab &tab) { return tab.token == tabs.selected_token; }))
        tabs.selected_token = tabs.tabs.front().token;

    std::uint64_t close_token = 0;
    ImGui::BeginChild("##object-reference-tab-strip", ImVec2(0.0f, 31.0f), false,
                      ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    for (const ObjectReferenceTabState::Tab &tab : tabs.tabs) {
        ImGui::PushID(
            static_cast<int>(
                static_cast<std::uint32_t>(tab.token >> 32)));

        ImGui::PushID(
            static_cast<int>(
                static_cast<std::uint32_t>(tab.token)));
        const bool selected = tabs.selected_token == tab.token;
        ImGui::PushStyleColor(ImGuiCol_Button,
                              selected ? ImVec4(0.55f, 0.32f, 0.82f, 1.0f) : ImVec4(0.25f, 0.22f, 0.32f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.46f, 0.36f, 0.66f, 1.0f));
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

struct ClassBrowserUiState {
    std::array<char, 192> search{};
    std::array<char, 128> assembly_filter{};
    BrowserClassInfo selected{};
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
};

ClassBrowserUiState &class_browser_ui_state() {
    static ClassBrowserUiState state;
    return state;
}

void render_class_browser(const Snapshot &snapshot) {
    ClassBrowserUiState &state = class_browser_ui_state();
    if (!snapshot.class_browser_catalog && !state.catalog_requested) {
        RuntimeModel::instance().enqueue(Command{.kind = CommandKind::LoadClassBrowserCatalog});
        state.catalog_requested = true;
    }

    ImGui::TextDisabled("Search every loaded IL2CPP type. Instance search follows Unity roots and static references.");
    if (!snapshot.class_browser_catalog) {
        ImGui::TextDisabled("Scanning class metadata...");
        return;
    }

    const ClassBrowserCatalog &catalog = *snapshot.class_browser_catalog;
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
                !contains_case_insensitive(entry.image, search))
                continue;
            if (!assembly_filter.empty() && !contains_case_insensitive(entry.image, assembly_filter))
                continue;
            state.matching_indices.push_back(index);
            if (state.matching_indices.size() >= kMaxCachedClasses)
                break;
        }
    }

    ImGui::BeginChild("##class-browser-results", ImVec2(0.0f, 235.0f), true);
    constexpr std::size_t kMaxShownClasses = 256;
    const std::size_t visible_count = std::min(kMaxShownClasses, state.matching_indices.size());
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(visible_count));
    while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            const BrowserClassInfo &entry = catalog.classes[state.matching_indices[static_cast<std::size_t>(row)]];
            const bool selected = state.selected.image == entry.image && state.selected.full_name == entry.full_name;
            const std::string label = entry.full_name + "##class-browser-" + entry.image;
            if (ImGui::Selectable(label.c_str(), selected))
                state.selected = entry;
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
    ImGui::TextColored(ImVec4(0.45f, 0.82f, 0.96f, 1.0f), "%s", state.selected.full_name.c_str());
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
        command.image = state.selected.image;
        command.namespc = state.selected.namespc;
        command.class_name = state.selected.class_name;
        RuntimeModel::instance().enqueue(std::move(command));
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("View members")) {
        Command command{};
        command.kind = CommandKind::LoadClassBrowserMembers;
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
        Command command{};
        command.kind = CommandKind::FindClassInstances;
        command.image = state.selected.image;
        command.namespc = state.selected.namespc;
        command.class_name = state.selected.class_name;
        command.int_value = state.selected.is_component ? 1 : 0;
        command.bool_value = state.include_all_loaded;
        RuntimeModel::instance().enqueue(std::move(command));
    }

    if (snapshot.class_browser_members_query.full_name == state.selected.full_name &&
        snapshot.class_browser_members_query.image == state.selected.image && snapshot.class_browser_members) {
        const ComponentInfo::Metadata &members = *snapshot.class_browser_members;
        const CodeContext class_code = code_context(state.selected.image, state.selected.namespc,
                                                    state.selected.class_name, state.selected.full_name);
        ImGui::SeparatorText("Members");
        ImGui::TextDisabled(
            "Metadata addresses are copyable. Live values, editing and nested Inspect require a found instance.");
        ImGui::BeginChild("##class-browser-members", ImVec2(0.0f, 210.0f), true);
        if (ImGui::TreeNode("##class-browser-fields", "Fields (%zu)", members.fields.size())) {
            for (const ComponentInfo::Field &field : members.fields) {
                ImGui::PushID(&field);
                ImGui::TextDisabled("%s%s : %s", field.is_static ? "static " : "", field.name.c_str(),
                                    field.type_name.c_str());
                render_field_context_menu(field, class_code);
                if (!field.declaring_type.empty()) {
                    ImGui::SameLine();
                    ImGui::TextDisabled("[%s]", field.declaring_type.c_str());
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Copy addr"))
                    ImGui::SetClipboardText(field.pointer_text.c_str());
                ImGui::PopID();
            }
            ImGui::TreePop();
        }
        if (ImGui::TreeNode("##class-browser-properties", "Properties (%zu)", members.properties.size())) {
            for (const ComponentInfo::Property &property : members.properties) {
                ImGui::PushID(&property);
                ImGui::TextDisabled("%s : %s  %s%s", property.name.c_str(), property.type_name.c_str(),
                                    property.can_read ? "get" : "", property.can_write ? "/set" : "");
                render_property_context_menu(property, class_code);
                ImGui::SameLine();
                if (ImGui::SmallButton("Copy addr"))
                    ImGui::SetClipboardText(property.pointer_text.c_str());
                ImGui::PopID();
            }
            ImGui::TreePop();
        }
        if (ImGui::TreeNode("##class-browser-methods", "Methods (%zu)", members.methods.size())) {
            for (const ComponentInfo::Method &method : members.methods) {
                ImGui::PushID(&method);
                std::string parameters;
                for (std::size_t index = 0; index < method.parameter_types.size(); ++index) {
                    if (!parameters.empty())
                        parameters += ", ";
                    parameters += method.parameter_types[index];
                    if (index < method.parameter_names.size() && !method.parameter_names[index].empty())
                        parameters += " " + method.parameter_names[index];
                }
                ImGui::TextDisabled("%s%s(%s) : %s", method.is_static ? "static " : "", method.name.c_str(),
                                    parameters.c_str(), method.return_type.c_str());
                render_method_context_menu(method, class_code);
                ImGui::SameLine();
                if (ImGui::SmallButton("Copy addr"))
                    ImGui::SetClipboardText(method.pointer_text.c_str());
                ImGui::PopID();
            }
            ImGui::TreePop();
        }
        ImGui::EndChild();
    }

    if (snapshot.class_browser_static_query.full_name == state.selected.full_name &&
        snapshot.class_browser_static_query.image == state.selected.image) {
        ImGui::SeparatorText("Static State");
        ImGui::TextDisabled("%zu static field(s)", snapshot.class_browser_static_fields.size());
        ImGui::BeginChild("##class-browser-static-fields", ImVec2(0.0f, 145.0f), true);
        for (const ClassBrowserStaticFieldInfo &field : snapshot.class_browser_static_fields) {
            const ComponentInfo::Field field_member{field.name, field.type_name, state.selected.full_name, true};
            ImGui::PushID(field.name.c_str());
            ImGui::TextUnformatted(field.name.c_str());
            render_field_context_menu(field_member, code_context(state.selected.image, state.selected.namespc,
                                                                  state.selected.class_name, state.selected.full_name));
            ImGui::SameLine();
            ImGui::TextDisabled("%s = %s", field.type_name.c_str(), field.display.c_str());
            if (field.token != 0) {
                ImGui::SameLine();
                if (ImGui::SmallButton("Inspect"))
                    enqueue_reference_inspection(field.token);
                ImGui::SameLine();
                if (ImGui::SmallButton("Copy Ptr"))
                    ImGui::SetClipboardText(field.pointer_text.c_str());
            }
            ImGui::PopID();
        }
        ImGui::EndChild();
    }

    if (snapshot.class_browser_query.full_name == state.selected.full_name &&
        snapshot.class_browser_query.image == state.selected.image) {
        ImGui::SeparatorText("Instances");
        ImGui::TextDisabled("%zu result(s) | %zu reachable objects | %zu static roots%s",
                            snapshot.class_browser_instances.size(), snapshot.class_browser_scanned_objects,
                            snapshot.class_browser_static_roots,
                            snapshot.class_browser_scan_truncated ? " (scan cap reached)" : "");
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
            if (ImGui::SmallButton("Inspect"))
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
}

int push_explorer_theme(float opacity) {
    const auto color = [opacity](float r, float g, float b, float alpha) { return ImVec4(r, g, b, alpha * opacity); };

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.93f, 0.96f, 0.99f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, color(0.012f, 0.022f, 0.042f, 0.84f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, color(0.020f, 0.038f, 0.070f, 0.68f));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, color(0.028f, 0.050f, 0.086f, 0.94f));
    ImGui::PushStyleColor(ImGuiCol_Border, color(0.30f, 0.48f, 0.68f, 0.62f));
    ImGui::PushStyleColor(ImGuiCol_BorderShadow, color(0.0f, 0.0f, 0.0f, 0.42f));
    ImGui::PushStyleColor(ImGuiCol_TitleBg, color(0.010f, 0.020f, 0.040f, 0.88f));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, color(0.045f, 0.135f, 0.230f, 0.92f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, color(0.040f, 0.072f, 0.120f, 0.82f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, color(0.075f, 0.145f, 0.220f, 0.90f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, color(0.10f, 0.220f, 0.315f, 0.96f));
    ImGui::PushStyleColor(ImGuiCol_Header, color(0.070f, 0.190f, 0.295f, 0.76f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, color(0.105f, 0.300f, 0.445f, 0.90f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, color(0.135f, 0.390f, 0.535f, 0.96f));
    ImGui::PushStyleColor(ImGuiCol_Button, color(0.045f, 0.105f, 0.170f, 0.82f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, color(0.085f, 0.205f, 0.315f, 0.90f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, color(0.105f, 0.285f, 0.400f, 0.96f));
    ImGui::PushStyleColor(ImGuiCol_Separator, color(0.25f, 0.43f, 0.62f, 0.62f));
    ImGui::PushStyleColor(ImGuiCol_SeparatorHovered, color(0.36f, 0.67f, 0.86f, 0.90f));
    ImGui::PushStyleColor(ImGuiCol_SeparatorActive, color(0.48f, 0.80f, 0.96f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_TableRowBg, color(0.020f, 0.042f, 0.076f, 0.58f));
    ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, color(0.040f, 0.072f, 0.118f, 0.64f));
    ImGui::PushStyleColor(ImGuiCol_TableHeaderBg, color(0.055f, 0.145f, 0.225f, 0.82f));
    ImGui::PushStyleColor(ImGuiCol_TextDisabled, ImVec4(0.62f, 0.72f, 0.80f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(0.35f, 0.86f, 0.76f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_TextSelectedBg, color(0.12f, 0.50f, 0.72f, 0.90f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, color(0.035f, 0.060f, 0.080f, 0.80f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, color(0.25f, 0.40f, 0.46f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, color(0.34f, 0.57f, 0.63f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive, color(0.40f, 0.68f, 0.72f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 4.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 2.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(9.0f, 7.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 3.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 4.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(5.0f, 3.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, 16.0f);
    return 30;
}

int push_panel_accent(const ImVec4& accent, float opacity) {
    const auto color = [opacity](float r, float g, float b, float alpha) {
        return ImVec4(r, g, b, alpha * opacity);
    };
    const ImVec4 window_bg(0.025f + accent.x * 0.045f,
                           0.035f + accent.y * 0.045f,
                           0.055f + accent.z * 0.045f, 0.84f * opacity);
    const ImVec4 child_bg(0.040f + accent.x * 0.035f,
                          0.055f + accent.y * 0.035f,
                          0.080f + accent.z * 0.035f, 0.66f * opacity);
    const ImVec4 title_bg(accent.x * 0.15f, accent.y * 0.15f, accent.z * 0.15f, 0.90f * opacity);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, window_bg);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, child_bg);
    ImGui::PushStyleColor(ImGuiCol_Border, color(accent.x, accent.y, accent.z, 0.72f));
    ImGui::PushStyleColor(ImGuiCol_Separator, color(accent.x, accent.y, accent.z, 0.60f));
    ImGui::PushStyleColor(ImGuiCol_TitleBg, title_bg);
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, accent);
    return 6;
}

void draw_panel_accent_bar(const ImVec4& accent) {
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    if (!draw_list)
        return;
    const ImVec2 position = ImGui::GetWindowPos();
    const ImVec2 size = ImGui::GetWindowSize();
    draw_list->AddRectFilled(position, ImVec2(position.x + 4.0f, position.y + size.y),
                              ImGui::GetColorU32(accent));
}

void render_diagnostics(const Snapshot &snapshot) {
    if (snapshot.diagnostics.empty()) {
        ImGui::TextDisabled("No errors or external overwrite events.");
    } else {
        for (const std::string &diagnostic : snapshot.diagnostics) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.57f, 0.40f, 1.0f));
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
            ImGui::PushStyleColor(ImGuiCol_Text, fault ? ImVec4(1.0f, 0.42f, 0.30f, 1.0f)
                                                       : ImVec4(0.64f, 0.78f, 0.92f, 1.0f));
            ImGui::Text("#%llu  +%.2fs  %s  %s%s%s", static_cast<unsigned long long>(event.sequence),
                        event.seconds_since_start, event.stage.c_str(), event.operation.c_str(),
                        event.detail.empty() ? "" : " — ", event.detail.c_str());
            ImGui::PopStyleColor();
        }
    }
    if (ImGui::Button("Clear flight recorder"))
        RuntimeModel::instance().enqueue(Command{.kind = CommandKind::ClearFlightRecorder});
}

} // namespace

void render() {
    if (!ModConfig::show_menu)
        return;

    const auto snapshot = RuntimeModel::instance().snapshot();
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    // Wait for a valid viewport before building the dock layout.
    if (!viewport || viewport->WorkSize.x <= 1.0f || viewport->WorkSize.y <= 1.0f)
        return;
    static const auto empty_hierarchy = std::make_shared<const HierarchyInfo>();
    const auto hierarchy = snapshot->hierarchy ? snapshot->hierarchy : empty_hierarchy;
    const ImVec2 work_pos = viewport->WorkPos;
    const ImVec2 work_size = viewport->WorkSize;
    static float opacity = 0.86f;
    static float highlight_max_distance = 0.0f;
    static bool show_hierarchy = true;
    static bool show_inspector = true;
    static bool show_object_inspector = false;
    static bool show_class_browser = false;
    static bool show_method_traces = false;
    static bool show_field_watches = false;
    static bool show_diagnostics = false;
    // Preserve the workspace size while tab contents change.
    static ImVec2 inspector_window_size{};
    static int previous_selection_id = 0;
    static std::uint64_t previous_object_token = 0;
    static std::size_t previous_trace_count = 0;
    static std::size_t previous_field_watch_count = 0;
    struct DockPanelState {
        bool hierarchy;
        bool inspector;
        bool object_inspector;
        bool class_browser;
        bool method_traces;
        bool field_watches;
        bool diagnostics;

        bool operator==(const DockPanelState &other) const {
            return hierarchy == other.hierarchy && inspector == other.inspector &&
                   object_inspector == other.object_inspector && class_browser == other.class_browser &&
                   method_traces == other.method_traces && field_watches == other.field_watches &&
                   diagnostics == other.diagnostics;
        }
    };
    static DockPanelState previous_dock_panel_state{};
    static bool dock_layout_initialized = false;
    const bool selection_changed = snapshot->selected_instance_id != previous_selection_id;
    if (selection_changed) {
        // Editor state belongs to the previous selection.
        member_buffers().clear();
        method_boolean_arguments().clear();
        component_filters().clear();
        component_inheritance_filters().clear();
        component_buffers() = {};
        previous_selection_id = snapshot->selected_instance_id;
        // Selecting from the Hierarchy is also an explicit request to inspect
        // that GameObject, even if the user previously closed the Inspector.
        if (snapshot->selected_instance_id != 0)
            show_inspector = true;
    }
    if (snapshot->object_inspector.valid && snapshot->object_inspector.token != previous_object_token)
        show_object_inspector = true;
    previous_object_token = snapshot->object_inspector.valid ? snapshot->object_inspector.token : 0;
    if (snapshot->method_traces.size() > previous_trace_count)
        show_method_traces = true;
    previous_trace_count = snapshot->method_traces.size();
    previous_field_watch_count = snapshot->field_watches.size();

    const int pushed_colors = push_explorer_theme(opacity);
    const ImGuiID dockspace_id = ImGui::GetID("URKExplorerDockSpace");
    const DockPanelState dock_panel_state{
        show_hierarchy,
        show_inspector,
        show_object_inspector,
        show_class_browser,
        show_method_traces,
        show_field_watches,
        show_diagnostics,
    };
    // Rebuild the layout after panels are opened or closed.
    const bool dock_layout_changed = !dock_layout_initialized || !(dock_panel_state == previous_dock_panel_state);
    if (dock_layout_changed || !ImGui::DockBuilderGetNode(dockspace_id)) {
        if (ImGui::DockBuilderGetNode(dockspace_id))
            ImGui::DockBuilderRemoveNode(dockspace_id);
        ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockspace_id, work_size);
        ImGuiID workspace_dock = 0;
        ImGuiID content_dock = dockspace_id;
        ImGui::DockBuilderSplitNode(content_dock, ImGuiDir_Up, 0.12f, &workspace_dock, &content_dock);
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
        if (show_diagnostics)
            ImGui::DockBuilderDockWindow("###urk-diagnostics", diagnostics_dock);
        ImGui::DockBuilderFinish(dockspace_id);
        previous_dock_panel_state = dock_panel_state;
        dock_layout_initialized = true;
    }
    ImGui::DockSpaceOverViewport(dockspace_id, viewport, ImGuiDockNodeFlags_PassthruCentralNode);

    ImGui::SetNextWindowPos(ImVec2(work_pos.x + 12.0f, work_pos.y + 12.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(std::min(900.0f, work_size.x - 24.0f), 96.0f), ImGuiCond_FirstUseEver);
    // Keep the workspace toolbar available while the overlay is open.
    if (ImGui::Begin("URK Explorer Workspace", nullptr, ImGuiWindowFlags_NoCollapse)) {
        ImGui::TextColored(ImVec4(0.46f, 0.74f, 0.96f, 1.0f), "%s", ModConfig::display_name);
        ImGui::SameLine();
        ImGui::TextDisabled("IL2CPP Runtime Explorer  |  %s  |  v%s", ModConfig::author, ModConfig::version);
        ImGui::Separator();
        if (ImGui::SmallButton("Panels"))
            ImGui::OpenPopup("##workspace-panels");
        if (ImGui::BeginPopup("##workspace-panels")) {
            ImGui::TextDisabled("Visible panels");
            ImGui::Checkbox("Hierarchy", &show_hierarchy);
            ImGui::Checkbox("Inspector", &show_inspector);
            ImGui::Checkbox("Object Inspector", &show_object_inspector);
            ImGui::Checkbox("Class Browser", &show_class_browser);
            ImGui::Checkbox("Method Traces", &show_method_traces);
            ImGui::Checkbox("Field Watches", &show_field_watches);
            ImGui::Checkbox("Activity log", &show_diagnostics);
            ImGui::EndPopup();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Refresh"))
            enqueue_simple(CommandKind::Refresh, 0);
        ImGui::SameLine();
        if (ImGui::SmallButton("Options"))
            ImGui::OpenPopup("##workspace-options");
        ImGui::SameLine();
        workspace_link_button("GitHub", ModConfig::url, ImVec4(0.16f, 0.36f, 0.54f, 1.0f));
        ImGui::SameLine();
        workspace_link_button("Support", ModConfig::social, ImVec4(0.46f, 0.29f, 0.18f, 1.0f));
        if (ImGui::BeginPopup("##workspace-options")) {
            ImGui::SeparatorText("Overlay");
            bool live_data = snapshot->live_data;
            if (ImGui::Checkbox("Live Data", &live_data)) {
                Command command{.kind = CommandKind::SetLiveData};
                command.bool_value = live_data;
                RuntimeModel::instance().enqueue(std::move(command));
            }
            ImGui::SetNextItemWidth(180.0f);
            ImGui::SliderFloat("Opacity", &opacity, 0.20f, 1.0f, "%.2f");
            ImGui::TextDisabled("Applies to every Explorer panel and popup.");
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
            ImGui::TextDisabled(camera_focus_top_down
                ? "Overrides the game angle with vertical height and perspective tilt."
                : "Preserves the active camera angle (FPS, third-person, or isometric). Distance applies live.");
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
        const int panel_colors = push_panel_accent(ImVec4(0.12f, 0.62f, 0.62f, 1.0f), opacity);
        if (ImGui::Begin("Hierarchy##urk-hierarchy", &show_hierarchy, ImGuiWindowFlags_NoCollapse)) {
            draw_panel_accent_bar(ImVec4(0.12f, 0.62f, 0.62f, 1.0f));
            ImGui::TextColored(ImVec4(0.42f, 0.88f, 0.82f, 1.0f), "%zu objects", hierarchy->objects);
            ImGui::SameLine();
            ImGui::TextDisabled("in %zu roots", hierarchy->roots);
            render_hierarchy(*hierarchy, snapshot->selected_instance_id);
        }
        ImGui::End();
        ImGui::PopStyleColor(panel_colors);
    }

    if (show_inspector) {
        const float inspector_width = std::max(480.0f, work_size.x * 0.46f);
        ImGui::SetNextWindowPos(ImVec2(work_pos.x + work_size.x - inspector_width - 12.0f, work_pos.y + 120.0f),
                                ImGuiCond_FirstUseEver);
        if (inspector_window_size.x > 0.0f && inspector_window_size.y > 0.0f)
            ImGui::SetNextWindowSize(inspector_window_size, ImGuiCond_FirstUseEver);
        else
            ImGui::SetNextWindowSize(ImVec2(inspector_width, std::max(480.0f, work_size.y * 0.80f)),
                                     ImGuiCond_FirstUseEver);
        const std::string title = snapshot->inspector.valid
                                      ? "Inspector - " + snapshot->inspector.name + "###urk-inspector"
                                      : "Inspector###urk-inspector";
        const int panel_colors = push_panel_accent(ImVec4(0.12f, 0.43f, 0.82f, 1.0f), opacity);
        if (ImGui::Begin(title.c_str(), &show_inspector, ImGuiWindowFlags_NoCollapse)) {
            draw_panel_accent_bar(ImVec4(0.12f, 0.43f, 0.82f, 1.0f));
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
        const int panel_colors = push_panel_accent(ImVec4(0.55f, 0.28f, 0.82f, 1.0f), opacity);
        if (ImGui::Begin(title.c_str(), &show_object_inspector, ImGuiWindowFlags_NoCollapse)) {
            draw_panel_accent_bar(ImVec4(0.55f, 0.28f, 0.82f, 1.0f));
            render_object_inspector(*snapshot);
        }
        ImGui::End();
        ImGui::PopStyleColor(panel_colors);
    }

    if (show_class_browser) {
        ImGui::SetNextWindowPos(ImVec2(work_pos.x + work_size.x * 0.20f, work_pos.y + 150.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(std::max(520.0f, work_size.x * 0.42f), std::max(520.0f, work_size.y * 0.74f)),
                                 ImGuiCond_FirstUseEver);
        const int panel_colors = push_panel_accent(ImVec4(0.72f, 0.48f, 0.18f, 1.0f), opacity);
        if (ImGui::Begin("Class Browser###urk-class-browser", &show_class_browser, ImGuiWindowFlags_NoCollapse)) {
            draw_panel_accent_bar(ImVec4(0.72f, 0.48f, 0.18f, 1.0f));
            render_class_browser(*snapshot);
        }
        ImGui::End();
        ImGui::PopStyleColor(panel_colors);
    }

    if (show_method_traces) {
        ImGui::SetNextWindowPos(ImVec2(work_pos.x + work_size.x * 0.18f, work_pos.y + 145.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(std::max(680.0f, work_size.x * 0.58f), std::max(420.0f, work_size.y * 0.62f)),
                                 ImGuiCond_FirstUseEver);
        const std::string title = "Method Traces (" + std::to_string(snapshot->method_traces.size()) + ")###urk-method-traces";
        const int panel_colors = push_panel_accent(ImVec4(0.86f, 0.46f, 0.20f, 1.0f), opacity);
        if (ImGui::Begin(title.c_str(), &show_method_traces, ImGuiWindowFlags_NoCollapse)) {
            draw_panel_accent_bar(ImVec4(0.86f, 0.46f, 0.20f, 1.0f));
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
            "Field Watches (" + std::to_string(snapshot->field_watches.size()) + ")###urk-field-watches";
        const int panel_colors = push_panel_accent(ImVec4(0.24f, 0.68f, 0.46f, 1.0f), opacity);
        if (ImGui::Begin(title.c_str(), &show_field_watches, ImGuiWindowFlags_NoCollapse)) {
            draw_panel_accent_bar(ImVec4(0.24f, 0.68f, 0.46f, 1.0f));
            render_field_watches(*snapshot);
        }
        ImGui::End();
        ImGui::PopStyleColor(panel_colors);
    }

    if (show_diagnostics) {
        ImGui::SetNextWindowPos(ImVec2(work_pos.x + 30.0f, work_pos.y + work_size.y - 260.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(std::min(760.0f, work_size.x - 60.0f), 230.0f), ImGuiCond_FirstUseEver);
        const std::string title =
            "Activity Log (" + std::to_string(snapshot->diagnostics.size()) + ")###urk-diagnostics";
        const int panel_colors = push_panel_accent(ImVec4(0.72f, 0.26f, 0.22f, 1.0f), opacity);
        if (ImGui::Begin(title.c_str(), &show_diagnostics)) {
            draw_panel_accent_bar(ImVec4(0.72f, 0.26f, 0.22f, 1.0f));
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

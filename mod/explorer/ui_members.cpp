// Copyright (c) 2026 Jadis0x. All rights reserved.
// Member rendering primitives shared by every inspector panel, plus the C++
// code generation behind their context menus.
#include "ui_members.h"

#include "config/mod_config.h"
#include "explorer_model.h"
#include "method_tracer.h"
#include "ui_shared.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <shellapi.h>

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <bit>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <unordered_map>
#include <unordered_set>

namespace Explorer::UI {
namespace {
struct InspectorBuffers {
    int instance_id = 0;
    std::array<char, 256> name{};
    std::array<char, 128> tag{};
};

InspectorBuffers &inspector_buffers() {
    static InspectorBuffers buffers;
    return buffers;
}

} // namespace
AddComponentBuffers &component_buffers() {
    static AddComponentBuffers buffers;
    return buffers;
}
void copy_text(std::vector<char> &buffer, std::string_view value) {
    buffer.resize(std::max<std::size_t>(256, value.size() + 1));
    std::memcpy(buffer.data(), value.data(), value.size());
    buffer[value.size()] = '\0';
}
namespace {

int resize_text_buffer(ImGuiInputTextCallbackData *data) {
    if (data->EventFlag != ImGuiInputTextFlags_CallbackResize)
        return 0;
    auto *buffer = static_cast<std::vector<char> *>(data->UserData);
    buffer->resize(static_cast<std::size_t>(data->BufTextLen) + 1);
    data->Buf = buffer->data();
    return 0;
}

} // namespace
bool input_text_dynamic(const char *label, const char *hint, std::vector<char> &buffer) {
    if (buffer.empty())
        buffer.resize(256, '\0');
    const ImGuiInputTextFlags flags = ImGuiInputTextFlags_CallbackResize | ImGuiInputTextFlags_EnterReturnsTrue;
    return hint
               ? ImGui::InputTextWithHint(label, hint, buffer.data(), buffer.size(), flags, resize_text_buffer, &buffer)
               : ImGui::InputText(label, buffer.data(), buffer.size(), flags, resize_text_buffer, &buffer);
}
namespace {

char ascii_lower(char value) {
    const unsigned char character = static_cast<unsigned char>(value);
    return static_cast<char>(std::tolower(character));
}

} // namespace
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
namespace {

bool equals_case_insensitive(std::string_view left, std::string_view right) {
    return left.size() == right.size() && contains_case_insensitive(left, right);
}

void open_external_url(const char *url) {
    if (url && url[0])
        ShellExecuteA(nullptr, "open", url, nullptr, nullptr, SW_SHOWNORMAL);
}

} // namespace
bool workspace_button(const char *label, const ImVec4 &color, const ImVec4 &text_color) {
    ImGui::PushStyleColor(ImGuiCol_Button, color);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                          ImVec4(std::min(1.0f, color.x + 0.08f), std::min(1.0f, color.y + 0.08f),
                                 std::min(1.0f, color.z + 0.08f), color.w));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                          ImVec4(color.x * 0.88f, color.y * 0.88f, color.z * 0.88f, color.w));
    ImGui::PushStyleColor(ImGuiCol_Text, text_color);
    const bool pressed = ImGui::SmallButton(label);
    ImGui::PopStyleColor(4);
    return pressed;
}
bool workspace_link_button(const char *label, const char *url, const ImVec4 &color,
                           const ImVec4 &text_color) {
    const bool pressed = workspace_button(label, color, text_color);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", url);
    if (pressed)
        open_external_url(url);
    return pressed;
}
namespace {

} // namespace
bool property_label(const char *label) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::TableSetColumnIndex(1);
    ImGui::SetNextItemWidth(-1.0f);
    return true;
}
namespace {
} // namespace
std::string type_details_text(std::string_view assembly, std::string_view namespc, std::string_view class_name,
                              std::string_view full_name) {
    return "Assembly: " + std::string(assembly.empty() ? "<unavailable>" : assembly) +
           "\nNamespace: " + std::string(namespc.empty() ? "<global>" : namespc) +
           "\nClass: " + std::string(class_name.empty() ? "<unavailable>" : class_name) +
           "\nFull type: " + std::string(full_name.empty() ? "<unavailable>" : full_name);
}
namespace {
namespace {

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

} // namespace
} // namespace
CodeContext code_context(std::string_view image, std::string_view namespc, std::string_view class_name,
                         std::string_view full_name) {
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
namespace {
} // namespace
CodeContext declaring_code_context(CodeContext context, std::string_view declaring_type) {
    if (declaring_type.empty())
        return context;
    return code_context(context.image, {}, {}, declaring_type);
}
namespace {
namespace {

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
    return type == "std::string" || type.rfind("Managed_", 0) == 0 ? "URK::managed::Object*" : type;
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
    const std::string call = return_type == "void" ? invocation : "const " + return_type + " result = " + invocation;
    const std::string method_label = cpp_string_literal(context_managed_name(context) + "." + method.name);
    std::string notes = managed_type_note("Managed return type", method.return_type);
    for (std::size_t index = 0; index < method.parameter_types.size(); ++index) {
        const std::string_view name = index < method.parameter_names.size() && !method.parameter_names[index].empty()
            ? std::string_view(method.parameter_names[index]) : std::string_view{};
        notes += "// Managed parameter " + std::to_string(index + 1) + (name.empty() ? "" : " (" + std::string(name) + ")") +
                 ": " + method.parameter_types[index] + "\n";
    }

    // A void call cannot communicate failure through a return value.  Generated code must
    // therefore preserve and report the SDK error instead of encouraging callers to log
    // success unconditionally after a no-op lookup or failed invocation.
    const std::string error_check =
        "if (const char *error = URK::Unity::last_error(); error && error[0])\n"
        "    ModLog::error(\"URKit method call failed (%s): %s\", " + method_label + ", error);";
    if (method.is_static) {
        return notes + member_type_aliases(method) +
               "// Requires mod/support/mod_log.h for failure diagnostics.\n"
               "URK::Unity::clear_error();\n" + call + "\n" + error_check;
    }
    return notes + member_type_aliases(method) + target_prelude(owner) +
           "// Requires mod/support/mod_log.h for failure diagnostics.\n"
           "if (!target) {\n"
           "    const char *error = URK::Unity::last_error();\n"
           "    ModLog::error(\"URKit method target was not found (%s): %s\", " + method_label +
           ", error && error[0] ? error : \"no matching object or component\");\n"
           "} else {\n"
           "    URK::Unity::clear_error();\n"
           "    " + call + "\n"
           "    " + error_check + "\n"
           "}";
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
        parameters = "URK::managed::Object* self";
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
#if defined(URK_BACKEND_IL2CPP)
    if (!parameters.empty())
        parameters += ", ";
    parameters += "const URK::managed::Method* method";
    if (!call_arguments.empty())
        call_arguments += ", ";
    call_arguments += "method";
#endif

    const std::string fn = id + "_fn";
    const std::string original = "g_original_" + id;
    const std::string detour = "detour_" + id;
    std::string code = "// Include sdk/runtime/managed_hooks.h and sdk/runtime_api.h.\n";
    code += "// Managed signature: " + method_signature(method) + "\n";
    code += "using " + fn + " = " + return_type + "(__fastcall*) (" + parameters + ");\n";
    code += "inline " + fn + " " + original + "{};\n\n";
    code += return_type + " __fastcall " + detour + "(" + parameters + ") {\n";
    code += "    // Inspect or modify the arguments here.\n";
    if (return_type == "void") {
        code += "    if (" + original + ")\n        " + original + "(" + call_arguments + ");\n";
    } else {
        const std::string fallback = return_type == "URK::managed::Object*" ? "nullptr" : return_type + "{}";
        code += "    return " + original + " ? " + original + "(" + call_arguments + ") : " + fallback + ";\n";
    }
    code += "}\n\n";
    code += "bool install_" + id + "() {\n";
    code += "    URK::HookOptions options{};\n    options.size = sizeof(options);\n";
    code += "    options.backend = static_cast<std::uint32_t>(URK::hook_backend_detours);\n";
    const std::string parameter_literals = method_parameter_type_literals(method);
    const bool has_parameters = !method.parameter_types.empty();
    if (has_parameters)
        code += "    static constexpr const char* parameter_types[] = {" + parameter_literals + "};\n";
    code += "    return URK::managed_hooks::try_hook_managed_method(" +
            cpp_string_literal(context.image) + ", " +
            cpp_string_literal(context.namespc) + ", " +
            cpp_string_literal(context.class_name) + ", " +
            cpp_string_literal(method.name) +
            ", " + std::string(has_parameters ? "parameter_types" : "nullptr") +
            ", " + std::to_string(method.parameter_types.size()) +
            ", &" + original + ", &" + detour + ", nullptr, &options);\n}";
    return code;
}

void copy_code_menu_item(const char *label, const std::string &code, bool enabled = true) {
    if (ImGui::MenuItem(label, nullptr, false, enabled))
        ImGui::SetClipboardText(code.c_str());
}

} // namespace
} // namespace
void render_field_context_menu(const ComponentInfo::Field &field, const CodeContext &context,
                               const std::function<void()> &extra) {
    if (!ImGui::BeginPopupContextItem("##field-code-menu"))
        return;
    if (extra)
        extra();
    copy_code_menu_item("Copy signature", field_signature(field));
    ImGui::Separator();
    const bool code_supported = field.runtime_safe && !custom_value_type_requires_abi(field.type_name, field.is_value_type);
    copy_code_menu_item("Copy URKit field read", field_get_code(field, context), code_supported);
    copy_code_menu_item("Copy URKit field write", field_set_code(field, context), code_supported);
    if (!code_supported)
        ImGui::TextDisabled("%s", field.capability_reason.empty() ? "Custom value type: define its native ABI struct first." : field.capability_reason.c_str());
    ImGui::EndPopup();
}
namespace {
} // namespace
void render_property_context_menu(const ComponentInfo::Property &property, const CodeContext &context,
                                  const std::function<void()> &extra) {
    if (!ImGui::BeginPopupContextItem("##property-code-menu"))
        return;
    if (extra)
        extra();
    copy_code_menu_item("Copy signature", property_signature(property));
    ImGui::Separator();
    const bool code_supported = property.runtime_safe && !custom_value_type_requires_abi(property.type_name, property.is_value_type);
    copy_code_menu_item("Copy URKit property get", property_get_code(property, context), property.can_read && code_supported);
    copy_code_menu_item("Copy URKit property set", property_set_code(property, context), property.can_write && code_supported);
    if (!code_supported)
        ImGui::TextDisabled("%s", property.capability_reason.empty() ? "Custom value type: define its native ABI struct first." : property.capability_reason.c_str());
    ImGui::EndPopup();
}
namespace {
} // namespace
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
namespace {
} // namespace
void render_type_details(const char *label, std::string_view assembly, std::string_view namespc,
                         std::string_view class_name, std::string_view full_name, bool default_open) {
    if (!ImGui::CollapsingHeader(label, default_open ? ImGuiTreeNodeFlags_DefaultOpen : 0))
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
namespace {
namespace {

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

void send_transform_copy(const InspectorInfo& info) {
    Command command{};
    command.kind = CommandKind::CopyLocalTransform;
    command.instance_id = info.instance_id;
    RuntimeModel::instance().enqueue(std::move(command));
}

void send_transform_paste(const InspectorInfo& info, const Snapshot::TransformClipboard& clipboard) {
    Command command{};
    command.kind = CommandKind::PasteLocalTransform;
    command.instance_id = info.instance_id;
    command.vector_value = clipboard.local_position;
    command.vector_value_secondary = clipboard.local_rotation;
    command.vector_value_tertiary = clipboard.local_scale;
    RuntimeModel::instance().enqueue(std::move(command));
}

} // namespace
} // namespace
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
    const float menu_width = ImGui::CalcTextSize("...").x + ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::SetNextItemWidth(std::max(80.0f, ImGui::GetContentRegionAvail().x - menu_width - 6.0f));
    ImGui::InputText("##name", buffers.name.data(), buffers.name.size());
    if (ImGui::IsItemDeactivatedAfterEdit())
        send_text_command(CommandKind::Rename, info, buffers.name.data());
    ImGui::SameLine();
    if (ImGui::SmallButton("...##game-object-menu"))
        ImGui::OpenPopup("##game-object-menu-popup");
    if (ImGui::BeginPopup("##game-object-menu-popup")) {
        if (ImGui::MenuItem("Copy GameObject Pointer"))
            ImGui::SetClipboardText(info.pointer_text.c_str());
        if (ImGui::MenuItem("Copy Runtime Type")) {
            const std::string details = type_details_text(info.assembly_name, info.namespace_name,
                                                          info.class_name, info.type_name);
            ImGui::SetClipboardText(details.c_str());
        }
        ImGui::EndPopup();
    }

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
}
namespace {
} // namespace
void render_transform(const InspectorInfo &info, const Snapshot::TransformClipboard& clipboard) {
    const bool open = ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen);
    if (ImGui::BeginPopupContextItem("##transform-context")) {
        if (ImGui::MenuItem("Copy Component"))
            send_transform_copy(info);
        ImGui::BeginDisabled(!clipboard.valid);
        if (ImGui::MenuItem("Paste Component Values"))
            send_transform_paste(info, clipboard);
        ImGui::EndDisabled();
        ImGui::Separator();
        if (ImGui::MenuItem("Reset")) {
            const float position[3]{};
            const float rotation[3]{};
            const float scale[3]{1.0f, 1.0f, 1.0f};
            send_vector_command(CommandKind::SetLocalPosition, info, position);
            send_vector_command(CommandKind::SetLocalRotation, info, rotation);
            send_vector_command(CommandKind::SetLocalScale, info, scale);
        }
        ImGui::EndPopup();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Right-click for component actions");
    if (!open)
        return;
    if (!ImGui::BeginTable("##transform", 2, ImGuiTableFlags_SizingStretchProp))
        return;
    ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, 72.0f);
    ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);

    float position[3]{info.local_position.x, info.local_position.y, info.local_position.z};
    property_label("Position");
    if (ImGui::DragFloat3("##position", position, 0.05f, 0.0f, 0.0f, "%.3f"))
        send_vector_command(CommandKind::SetLocalPosition, info, position);

    float rotation[3]{info.local_rotation.x, info.local_rotation.y, info.local_rotation.z};
    property_label("Rotation");
    if (ImGui::DragFloat3("##rotation", rotation, 0.25f, 0.0f, 0.0f, "%.2f"))
        send_vector_command(CommandKind::SetLocalRotation, info, rotation);

    float scale[3]{info.local_scale.x, info.local_scale.y, info.local_scale.z};
    property_label("Scale");
    if (ImGui::DragFloat3("##scale", scale, 0.02f, 0.0f, 0.0f, "%.3f"))
        send_vector_command(CommandKind::SetLocalScale, info, scale);

    ImGui::EndTable();
}
namespace {
} // namespace
MemberBuffer &member_buffer(std::uint64_t key) {
    return ui_state().member_buffers.touch(key);
}
namespace {
} // namespace
std::array<char, 128> &component_filter(int component_id) {
    return ui_state().component_filters.touch(component_id);
}
namespace {
} // namespace
bool &component_show_inherited(int component_id) {
    return ui_state().component_show_inherited.touch(component_id, true);
}
namespace {
} // namespace
int &component_member_tab(int component_id) {
    return ui_state().component_member_tabs.touch(component_id);
}
namespace {
} // namespace
int &object_member_tab_for(std::uint64_t token) {
    return ui_state().object_member_tabs.touch(token);
}
namespace {
} // namespace
std::array<char, 128> &object_member_filter(std::uint64_t token) {
    return ui_state().object_member_filters.touch(token);
}
namespace {
namespace {

bool query_matches_member(std::string_view filter, std::initializer_list<std::string_view> searchable) {
	std::size_t start = 0;
	while (start < filter.size()) {
		while (start < filter.size() && std::isspace(static_cast<unsigned char>(filter[start])))
			++start;
		const std::size_t end = filter.find_first_of(" \t\r\n", start);
		const std::string_view token = filter.substr(start, end == std::string_view::npos ? filter.size() - start : end - start);
		if (!token.empty() && !std::any_of(searchable.begin(), searchable.end(), [token](std::string_view text) {
			return contains_case_insensitive(text, token);
		}))
			return false;
		if (end == std::string_view::npos)
			break;
		start = end + 1;
	}
	return true;
}

} // namespace
} // namespace
bool member_matches_filter(std::string_view name, std::string_view type, std::string_view declaring_type,
	std::string_view filter) {
	return filter.empty() || query_matches_member(filter, {name, type, declaring_type});
}
namespace {
} // namespace
bool method_matches_filter(const ComponentInfo::Method& method, std::string_view filter) {
	if (filter.empty())
		return true;
	std::size_t start = 0;
	while (start < filter.size()) {
		while (start < filter.size() && std::isspace(static_cast<unsigned char>(filter[start])))
			++start;
		const std::size_t end = filter.find_first_of(" \t\r\n", start);
		const std::string_view token = filter.substr(start, end == std::string_view::npos ? filter.size() - start : end - start);
		bool found = token.empty() || contains_case_insensitive(method.name, token) ||
			contains_case_insensitive(method.return_type, token) || contains_case_insensitive(method.declaring_type, token);
		for (std::size_t index = 0; !found && index < method.parameter_types.size(); ++index) {
			found = contains_case_insensitive(method.parameter_types[index], token) ||
				(index < method.parameter_names.size() && contains_case_insensitive(method.parameter_names[index], token));
		}
		if (!found)
			return false;
		if (end == std::string_view::npos)
			break;
		start = end + 1;
	}
	return true;
}
namespace {
namespace {

void paste_into(MemberBuffer &buffer) {
    if (const char *clipboard = ImGui::GetClipboardText())
        copy_text(buffer.text, clipboard);
}

} // namespace
} // namespace
bool editable_value(const URK::Unity::Inspect::ValueInfo &value) {
    using URK::Unity::Inspect::ValueKind;
    return value.kind == ValueKind::Boolean || value.kind == ValueKind::SignedInteger ||
           value.kind == ValueKind::UnsignedInteger || value.kind == ValueKind::FloatingPoint ||
           value.kind == ValueKind::String || value.kind == ValueKind::Enum || value.kind == ValueKind::Structured;
}
namespace {
namespace {

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
                        bool object_inspector_target, std::uint64_t member_key, bool locked, bool lockable,
                        std::uint64_t object_inspector_token) {
    if (!lockable || member_key == 0)
        return;
    ImGui::SameLine();
    if (ImGui::SmallButton(locked ? "Unlock" : "Lock")) {
        enqueue_member_value(kind, component_id, member_index, text, bool_value, object_inspector_target, member_key,
                             !locked, locked, object_inspector_token);
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(locked ? "Stop enforcing this value" : "Reapply this value every update");
}

} // namespace
} // namespace
bool& object_inspector_window_requested() {
    static bool requested = false;
    return requested;
}
namespace {
} // namespace
void enqueue_reference_inspection(std::uint64_t token, bool request_object_tab) {
    if (token == 0)
        return;
    if (request_object_tab) {
        object_inspector_window_requested() = true;
        request_object_reference_tab(token);
    }
    Command command{};
    command.kind = CommandKind::InspectReference;
    command.reference_token = token;
    RuntimeModel::instance().enqueue(std::move(command));
}
namespace {
} // namespace
void enqueue_raw_reference_inspection(std::uint64_t address) {
    if (address == 0)
        return;
    Command command{};
    command.kind = CommandKind::InspectRawReference;
    command.reference_token = address;
    RuntimeModel::instance().enqueue(std::move(command));
}
namespace {
namespace {

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

} // namespace
} // namespace
bool render_reference_context_menu(const ComponentInfo::LiveValues::Reference *reference, bool allow_assign) {
    const bool available = reference && !reference->is_null && reference->token != 0;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s\nRight-click for reference actions",
                          available ? reference->type_name.c_str() : "null reference");
    bool assign_requested = false;
    if (!ImGui::BeginPopupContextItem("##reference-actions"))
        return false;

    ImGui::TextDisabled("%s", available ? reference->type_name.c_str() : "Reference");
    ImGui::BeginDisabled(!available);
    const bool is_game_object = available &&
        (reference->type_name == "UnityEngine.GameObject" || reference->type_name == "GameObject");
    if (ImGui::MenuItem(is_game_object ? "Select GameObject" : "Inspect Reference"))
        enqueue_reference_inspection(reference->token, !is_game_object);
    if (ImGui::MenuItem("Save Reference")) {
        Command command{};
        command.kind = CommandKind::PinManagedReference;
        command.reference_token = reference->token;
        RuntimeModel::instance().enqueue(std::move(command));
    }
    if (ImGui::MenuItem("Copy Pointer"))
        ImGui::SetClipboardText(reference->pointer_text.c_str());
    ImGui::EndDisabled();
    if (allow_assign) {
        ImGui::Separator();
        assign_requested = ImGui::MenuItem("Assign Reference...");
    }
    ImGui::EndPopup();
    return assign_requested;
}
namespace {
namespace {

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
                       locked, lockable, object_inspector_token);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s - drag components; release to apply", value.type_name.c_str());
}

} // namespace
} // namespace
void render_live_value(CommandKind kind, int component_id, int member_index,
                       const URK::Unity::Inspect::ValueInfo *value, bool writable, std::uint64_t buffer_key,
                       const ComponentInfo::LiveValues::Reference *reference, bool object_inspector_target,
                       bool live_data, bool locked, bool lockable,
                       std::uint64_t object_inspector_token, bool runtime_safe,
                       std::string_view capability_reason,
                       const std::vector<ManagedReferenceInfo>* managed_references) {
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
    MemberBuffer &buffer = member_buffer(buffer_key);
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
            ImGui::TextColored(ImVec4(0.78f, 0.42f, 0.38f, 1.0f), "%s",
                               value->display.empty() ? "Unavailable" : value->display.c_str());
        }
        render_reference_context_menu(reference);
        return;
    }
    buffer.sample_requested = false;
    if (!writable) {
        ImGui::TextDisabled("%s", value->display.empty() ? "<unsupported>" : value->display.c_str());
        if (reference && !reference->is_null && reference->token != 0) {
            render_reference_context_menu(reference);
        }
        return;
    }
    const bool reference_value = value->kind == ValueKind::ObjectReference ||
                                 value->kind == ValueKind::ArrayReference || value->kind == ValueKind::Null;
    if (reference_value) {
        const char *display = value->kind == ValueKind::Null
                                  ? "null"
                                  : (!value->display.empty() ? value->display.c_str() : value->type_name.c_str());
        ImGui::TextColored(ImVec4(0.62f, 0.72f, 0.82f, 1.0f), "%s", display);
        if (render_reference_context_menu(reference, true)) {
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
            if (managed_references && ImGui::Button("Saved refs..."))
                ImGui::OpenPopup("##assign-pinned-reference");
            if (managed_references && ImGui::BeginPopup("##assign-pinned-reference")) {
                if (managed_references->empty()) {
                    ImGui::TextDisabled("No saved references are available.");
                } else {
                    for (const ManagedReferenceInfo& item : *managed_references) {
                        ImGui::PushID(static_cast<int>(item.token));
                        const std::string label = item.display + " [" + item.type_name + "]";
                        if (ImGui::Selectable(label.c_str())) {
                            copy_text(buffer.text, "@ref:" + std::to_string(item.token));
                            ImGui::CloseCurrentPopup();
                        }
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("%s\n%s", item.source.c_str(), item.pointer_text.c_str());
                        ImGui::PopID();
                    }
                }
                ImGui::EndPopup();
            }
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
                               buffer_key, locked, lockable, object_inspector_token);
            ImGui::EndPopup();
        }
        return;
    }
    if (!editable_value(*value)) {
        ImGui::TextDisabled("%s", value->display.empty() ? "<unsupported>" : value->display.c_str());
        render_reference_context_menu(reference);
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
                           buffer_key, locked, lockable, object_inspector_token);
        render_reference_context_menu(reference);
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
                       locked, lockable, object_inspector_token);
    render_reference_context_menu(reference);
}
namespace {
} // namespace
bool invokable_method(const ComponentInfo::Method &method) {
    return !method.name.empty() && method.runtime_callable;
}
namespace {
} // namespace
bool &method_boolean_argument(std::uint64_t key) {
    return ui_state().method_boolean_arguments.touch(key);
}
namespace {
} // namespace
std::uint64_t scoped_ui_key(std::uint64_t scope, std::uint64_t domain, std::size_t first, std::size_t second) {
    // Scope editor state to each Object Inspector tab.
    std::uint64_t key = scope ^ domain;
    key ^= static_cast<std::uint64_t>(first) + 0x9e3779b97f4a7c15ull + (key << 6) + (key >> 2);
    key ^= static_cast<std::uint64_t>(second) + 0x9e3779b97f4a7c15ull + (key << 6) + (key >> 2);
    return key;
}
namespace {
} // namespace
std::uint64_t method_argument_key(int component_id, std::size_t method, std::size_t parameter,
                                  std::uint64_t object_inspector_token) {
    if (object_inspector_token != 0)
        return scoped_ui_key(object_inspector_token, 0x7100000000000000ull, method, parameter);
    return 0x7000000000000000ull | (static_cast<std::uint64_t>(static_cast<std::uint32_t>(component_id)) << 24) |
           ((static_cast<std::uint64_t>(method) & 0x0fffull) << 12) |
           (static_cast<std::uint64_t>(parameter) & 0x0fffull);
}
namespace {
} // namespace
bool boolean_type(std::string_view type) {
    return equals_case_insensitive(type, "system.boolean") || equals_case_insensitive(type, "bool");
}
namespace {
namespace {

void write_structured_argument(MemberBuffer &buffer) {
    const std::string text = structured_value_text(buffer);
    copy_text(buffer.text, text);
}

} // namespace
} // namespace
void render_method_argument(int component_id, std::size_t method_index, std::size_t parameter_index,
                            std::string_view type, std::string_view name, std::uint64_t object_inspector_token,
                            const std::vector<ManagedReferenceInfo>* managed_references) {
    const std::uint64_t key = method_argument_key(component_id, method_index, parameter_index, object_inspector_token);
    if (boolean_type(type)) {
        bool &value = method_boolean_argument(key);
        ImGui::Checkbox(name.data(), &value);
        return;
    }

    MemberBuffer &buffer = member_buffer(key);
	const auto initialize_numeric_text = [&] {
		if (buffer.text.empty() || buffer.text[0] == '\0')
			copy_text(buffer.text, "0");
	};
	const std::string label{name};
	if (equals_case_insensitive(type, "system.single") || equals_case_insensitive(type, "float")) {
		initialize_numeric_text();
		float value = std::strtof(buffer.text.data(), nullptr);
		if (ImGui::InputFloat(label.c_str(), &value, 0.0f, 0.0f, "%.6g")) {
			char text[48]{};
			std::snprintf(text, sizeof(text), "%.9g", static_cast<double>(value));
			copy_text(buffer.text, text);
		}
		return;
	}
	if (equals_case_insensitive(type, "system.double") || equals_case_insensitive(type, "double")) {
		initialize_numeric_text();
		double value = std::strtod(buffer.text.data(), nullptr);
		if (ImGui::InputDouble(label.c_str(), &value, 0.0, 0.0, "%.12g")) {
			char text[64]{};
			std::snprintf(text, sizeof(text), "%.17g", value);
			copy_text(buffer.text, text);
		}
		return;
	}
	if (equals_case_insensitive(type, "system.int32") || equals_case_insensitive(type, "int")) {
		initialize_numeric_text();
		int value = static_cast<int>(std::strtol(buffer.text.data(), nullptr, 0));
		if (ImGui::InputInt(label.c_str(), &value))
			copy_text(buffer.text, std::to_string(value));
		return;
	}
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
    ImGui::SetNextItemWidth(managed_references ? -58.0f : -1.0f);
    input_text_dynamic("##argument", hint.c_str(), buffer.text);
    if (managed_references && !is_string) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Reference..."))
            ImGui::OpenPopup("##pinned-reference");
        if (ImGui::BeginPopup("##pinned-reference")) {
            ImGui::TextUnformatted("Saved runtime references");
            ImGui::TextDisabled("The runtime validates compatibility when Execute is pressed.");
            if (managed_references->empty()) {
                ImGui::TextDisabled("No saved references. Save an inspector value or the selected GameObject first.");
            } else {
                for (const ManagedReferenceInfo& reference : *managed_references) {
                    ImGui::PushID(static_cast<int>(reference.token));
                    const std::string label = reference.display + "  [" + reference.type_name + "]";
                    if (ImGui::Selectable(label.c_str())) {
                        copy_text(buffer.text, "@ref:" + std::to_string(reference.token));
                        ImGui::CloseCurrentPopup();
                    }
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("%s\n%s", reference.source.c_str(), reference.pointer_text.c_str());
                    ImGui::PopID();
                }
            }
            ImGui::EndPopup();
        }
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%.*s", static_cast<int>(type.size()), type.data());
}
namespace {
} // namespace
std::uint64_t generic_type_key(int component_id, std::size_t method, std::uint64_t object_inspector_token) {
    return object_inspector_token != 0
        ? scoped_ui_key(object_inspector_token, 0x7300000000000000ull, method)
        : 0x7200000000000000ull | (static_cast<std::uint64_t>(static_cast<std::uint32_t>(component_id)) << 24) |
            (static_cast<std::uint64_t>(method) & 0x00ffffffull);
}
namespace {
namespace {

struct GenericTypeSearchState {
    bool catalog_requested = false;
};

GenericTypeSearchState& generic_type_search_state() {
    static GenericTypeSearchState state;
    return state;
}

} // namespace
} // namespace
void render_generic_type_input(const Snapshot& snapshot, const char* id, std::vector<char>& buffer) {
    input_text_dynamic(id, "Generic type: image:Namespace.Type", buffer);
    const std::string_view query(buffer.data());
    if (query.size() < 3)
        return;

    GenericTypeSearchState& state = generic_type_search_state();
    if (!snapshot.class_browser_catalog && !state.catalog_requested) {
        RuntimeModel::instance().enqueue(Command{.kind = CommandKind::LoadClassBrowserCatalog});
        state.catalog_requested = true;
    }
    if (!snapshot.class_browser_catalog)
        return;

    const ClassBrowserCatalog& catalog = *snapshot.class_browser_catalog;
    std::size_t shown = 0;
    constexpr std::size_t kMaxGenericTypeResults = 48;
    const std::string result_id = std::string(id) + "-results";
    if (!ImGui::BeginChild(result_id.c_str(), ImVec2(0.0f, 150.0f), true))
        return;
    for (const BrowserClassInfo& entry : catalog.classes) {
        const std::string qualified = entry.image + ":" + entry.full_name;
        if (!contains_case_insensitive(qualified, query) &&
            !contains_case_insensitive(entry.class_name, query) &&
            !contains_case_insensitive(entry.namespc, query))
            continue;
        if (shown++ >= kMaxGenericTypeResults) {
            ImGui::TextDisabled("More types match; refine the search.");
            break;
        }
        const std::string label = qualified + "##generic-type-result-" + std::to_string(shown);
        if (ImGui::Selectable(label.c_str())) {
            copy_text(buffer, qualified);
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", entry.pointer_text.c_str());
    }
    if (shown == 0)
        ImGui::TextDisabled("No loaded type matches this search.");
    ImGui::EndChild();
}
namespace {
} // namespace
void enqueue_method_invoke(int component_id, int method_index, const ComponentInfo::Method &method,
                           bool object_inspector_target, std::uint64_t object_inspector_token) {
    Command command{};
    command.kind = CommandKind::InvokeMethod;
    command.instance_id = component_id;
    command.member_index = method_index;
    command.object_inspector_target = object_inspector_target;
    command.object_inspector_token = object_inspector_token;
    if (method.uses_generic_parameter) {
        const std::uint64_t key = generic_type_key(component_id, static_cast<std::size_t>(method_index), object_inspector_token);
        command.generic_type_arguments.push_back(member_buffer(key).text.data());
    }
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
namespace {
} // namespace
void enqueue_method_trace(int component_id, int method_index, bool enabled, bool object_inspector_target,
                          std::uint64_t object_inspector_token, bool capture_return) {
    Command command{};
    command.kind = CommandKind::SetMethodTrace;
    command.instance_id = component_id;
    command.member_index = method_index;
    command.bool_value = enabled;
    command.capture_return = capture_return;
    command.object_inspector_target = object_inspector_target;
    command.object_inspector_token = object_inspector_token;
    RuntimeModel::instance().enqueue(std::move(command));
}
namespace {
namespace {

// One Trace button; return capture is the deliberate second choice behind it.
// The stub that captures returns rewrites the callee return address, so it
// cannot survive a managed exception escaping the method.
} // namespace
} // namespace
void render_trace_button(const std::function<void(bool)> &start_trace) {
    // Return capture used to hide behind a right-click, so the common outcome
    // was a trace whose every row reported no return and no way to say why.
    if (ImGui::SmallButton("Trace"))
        start_trace(false);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Records arguments, caller, thread and timing for every call.\n"
                          "Return values are NOT recorded - use \"Trace + returns\" for those.");
    ImGui::SameLine();
    if (ImGui::SmallButton("Trace + returns"))
        start_trace(true);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Records everything above plus what the method returns.\n"
                          "Rewrites the return address, so avoid it on methods that throw.");
}
namespace {
} // namespace
void enqueue_method_trace_clear(MethodTracer::TraceId id) {
    Command command{};
    command.kind = CommandKind::ClearMethodTrace;
    command.reference_token = id;
    RuntimeModel::instance().enqueue(std::move(command));
}
namespace {
} // namespace
const MethodTracer::Snapshot *trace_for_method(const std::vector<MethodTracer::Snapshot> &traces,
                                               const ComponentInfo::Method &method) {
    const auto found = std::find_if(traces.begin(), traces.end(), [&method](const MethodTracer::Snapshot &trace) {
        return !method.pointer_text.empty() && trace.method_pointer_text == method.pointer_text;
    });
    return found == traces.end() ? nullptr : &*found;
}
namespace {
} // namespace
const Snapshot::FieldWatch *field_watch_for(const Snapshot &snapshot, int component_instance_id,
                                             std::size_t field_index,
                                             std::uint64_t object_inspector_token,
                                             bool property) {
    const auto found = std::find_if(snapshot.field_watches.begin(), snapshot.field_watches.end(),
                                    [=](const Snapshot::FieldWatch &watch) {
                                        return watch.component_instance_id == component_instance_id &&
                                               watch.object_inspector_token == object_inspector_token &&
                                               watch.field_index == field_index && watch.property == property;
                                    });
    return found == snapshot.field_watches.end() ? nullptr : &*found;
}
namespace {
} // namespace
void enqueue_field_watch(int component_id, int field_index, bool enabled,
                         std::uint64_t object_inspector_token, bool property) {
    Command command{};
    command.kind = CommandKind::SetFieldWatch;
    command.instance_id = component_id;
    command.member_index = field_index;
    command.bool_value = enabled;
    command.object_inspector_target = object_inspector_token != 0;
    command.object_inspector_token = object_inspector_token;
    command.member_is_property = property;
    RuntimeModel::instance().enqueue(std::move(command));
}
namespace {
} // namespace
void enqueue_field_watch_clear(std::uint64_t id) {
    Command command{};
    command.kind = CommandKind::ClearFieldWatch;
    command.reference_token = id;
    RuntimeModel::instance().enqueue(std::move(command));
}
namespace {
} // namespace
void enqueue_field_watch_close(std::uint64_t id) {
    Command command{};
    command.kind = CommandKind::CloseFieldWatch;
    command.reference_token = id;
    RuntimeModel::instance().enqueue(std::move(command));
}
namespace {
} // namespace
void enqueue_watch_alarm(std::uint64_t id, WatchAnalysis::AlarmCondition condition, float threshold) {
    Command command{};
    command.kind = CommandKind::ConfigureFieldWatch;
    command.reference_token = id;
    command.int_value = static_cast<int>(condition);
    command.float_value = threshold;
    RuntimeModel::instance().enqueue(std::move(command));
}
void render_method_result(const Snapshot &snapshot, int component_id, std::size_t method_index,
                           std::uint64_t object_inspector_token) {
    const auto found =
        std::find_if(snapshot.method_results.begin(), snapshot.method_results.end(), [=](const auto &entry) {
            const Snapshot::MethodResult &result = entry.second;
            return result.component_instance_id == component_id && result.method_index == method_index &&
                   result.object_inspector_token == object_inspector_token;
        });
    if (found == snapshot.method_results.end()) {
        return;
    }
    const Snapshot::MethodResult &result = found->second;
    ImGui::TextColored(result.succeeded ? ImVec4(0.60f, 0.68f, 0.60f, 1.0f)
        : ImVec4(0.78f, 0.42f, 0.38f, 1.0f), result.succeeded ? "Success" : "Failed");
    ImGui::SameLine();
    ImGui::TextDisabled("%.2f ms", result.elapsed_milliseconds);
    ImGui::TextWrapped("%s", result.display.c_str());
    if (!result.reference.is_null && result.reference.token != 0) {
        render_reference_context_menu(&result.reference);
    } else if (ImGui::BeginPopupContextItem("##method-result-copy")) {
        // A string/scalar result has no managed handle to inspect: its full value
        // is already the text on screen, so the only useful action left is copying it.
        if (ImGui::MenuItem("Copy value"))
            ImGui::SetClipboardText(result.display.c_str());
        ImGui::EndPopup();
    }
}
namespace {
} // namespace
void render_member_write_result(const Snapshot& snapshot, int component_id, std::size_t member_index,
                                bool property, std::uint64_t object_inspector_token) {
    const auto found = std::find_if(snapshot.member_write_results.begin(), snapshot.member_write_results.end(),
                                    [=](const auto& entry) {
        const Snapshot::MemberWriteResult& result = entry.second;
        return result.component_instance_id == component_id && result.member_index == member_index &&
               result.property == property && result.object_inspector_token == object_inspector_token;
    });
    if (found == snapshot.member_write_results.end())
        return;
    const Snapshot::MemberWriteResult& result = found->second;
    ImGui::TextColored(result.succeeded ? ImVec4(0.60f, 0.68f, 0.60f, 1.0f)
                                       : ImVec4(0.78f, 0.42f, 0.38f, 1.0f),
                       "%s", result.display.c_str());
}

} // namespace Explorer::UI

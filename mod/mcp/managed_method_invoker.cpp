// Copyright (c) 2026 Jadis0x. All rights reserved.
#include "managed_method_invoker.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <vector>

namespace Explorer::Mcp::ManagedInvoke {
namespace {

std::string normalized(std::string value) {
    if (!value.empty() && value.back() == '&')
        value.pop_back();
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

bool unsigned_type(std::string_view type) {
    return type == "system.byte" || type == "system.uint16" || type == "system.uint32" ||
        type == "system.uint64" || type == "system.uintptr" || type == "system.char";
}

bool signed_type(std::string_view type) {
    return type == "system.sbyte" || type == "system.int16" || type == "system.int32" ||
        type == "system.int64" || type == "system.intptr";
}

bool parse_argument(const URK::Unity::Inspect::MethodParamInfo& parameter,
                    const nlohmann::json& input, const ReferenceResolver& resolve_reference,
                    std::vector<URK::Unity::Inspect::ObjectHandle>& roots,
                    URK::Unity::Inspect::ValueInfo& value, std::string& error) {
    using namespace URK::Unity;
    value = {};
    value.type_name = parameter.type_name;
    const std::string type = normalized(parameter.type_name);
    const Inspect::TypeInfo described = Inspect::DescribeType(parameter.type);
    if (input.is_null()) {
        if (parameter.is_value_type || described.is_value_type || unsigned_type(type) || signed_type(type) ||
            type == "system.boolean" || type == "system.single" || type == "system.double") {
            error = "null is not valid for value-type parameter " + parameter.name;
            return false;
        }
        value.kind = Inspect::ValueKind::Null;
        value.display = "null";
        value.readable = true;
        return true;
    }
    if (described.is_enum) {
        if (!input.is_number_integer() && !input.is_number_unsigned()) {
            error = "enum parameter " + parameter.name + " requires an integer value";
            return false;
        }
        value.kind = Inspect::ValueKind::Enum;
        if (input.is_number_unsigned()) {
            const std::uint64_t raw = input.get<std::uint64_t>();
            if (raw > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
                error = "enum parameter " + parameter.name + " is outside the supported signed range";
                return false;
            }
            value.signed_value = static_cast<std::int64_t>(raw);
        } else {
            value.signed_value = input.get<std::int64_t>();
        }
        value.display = std::to_string(value.signed_value);
        value.readable = true;
        return true;
    }
    if (type == "system.boolean") {
        if (!input.is_boolean()) {
            error = "parameter " + parameter.name + " requires a boolean";
            return false;
        }
        value.kind = Inspect::ValueKind::Boolean;
        value.bool_value = input.get<bool>();
        value.display = value.bool_value ? "true" : "false";
        value.readable = true;
        return true;
    }
    if (type == "system.string") {
        if (!input.is_string()) {
            error = "parameter " + parameter.name + " requires a string";
            return false;
        }
        value.kind = Inspect::ValueKind::String;
        value.display = input.get<std::string>();
        value.readable = true;
        return true;
    }
    if (unsigned_type(type)) {
        if (!input.is_number_unsigned() && !input.is_number_integer()) {
            error = "parameter " + parameter.name + " requires an unsigned integer";
            return false;
        }
        if (input.is_number_integer() && input.get<std::int64_t>() < 0) {
            error = "parameter " + parameter.name + " cannot be negative";
            return false;
        }
        value.kind = Inspect::ValueKind::UnsignedInteger;
        value.unsigned_value = input.is_number_unsigned()
            ? input.get<std::uint64_t>() : static_cast<std::uint64_t>(input.get<std::int64_t>());
        value.display = std::to_string(value.unsigned_value);
        value.readable = true;
        return true;
    }
    if (signed_type(type)) {
        if (!input.is_number_integer() && !input.is_number_unsigned()) {
            error = "parameter " + parameter.name + " requires an integer";
            return false;
        }
        if (input.is_number_unsigned() &&
            input.get<std::uint64_t>() > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
            error = "parameter " + parameter.name + " is outside the signed integer range";
            return false;
        }
        value.kind = Inspect::ValueKind::SignedInteger;
        value.signed_value = input.is_number_unsigned()
            ? static_cast<std::int64_t>(input.get<std::uint64_t>()) : input.get<std::int64_t>();
        value.display = std::to_string(value.signed_value);
        value.readable = true;
        return true;
    }
    if (type == "system.single" || type == "system.double") {
        if (!input.is_number()) {
            error = "parameter " + parameter.name + " requires a number";
            return false;
        }
        value.kind = Inspect::ValueKind::FloatingPoint;
        value.floating_value = input.get<double>();
        value.display = std::to_string(value.floating_value);
        value.readable = true;
        return true;
    }
    const std::size_t component_count = Inspect::structured_component_count(parameter.type_name);
    if (component_count != 0) {
        if (!input.is_array() || input.size() != component_count) {
            error = "parameter " + parameter.name + " requires an array of " +
                std::to_string(component_count) + " numeric components";
            return false;
        }
        value.kind = Inspect::ValueKind::Structured;
        value.component_count = component_count;
        for (std::size_t index = 0; index < component_count; ++index) {
            if (!input[index].is_number()) {
                error = "structured parameter " + parameter.name + " contains a non-numeric component";
                return false;
            }
            value.components[index] = input[index].get<double>();
        }
        value.display = input.dump();
        value.readable = true;
        return true;
    }
    if (!input.is_object() || !input.contains("reference") || !input["reference"].is_string()) {
        error = "parameter " + parameter.name + " requires {\"reference\": \"urkref_...\"}";
        return false;
    }
    std::string reference_error;
    Inspect::ObjectHandle lifetime_root{};
    const Object object = resolve_reference(input["reference"].get_ref<const std::string&>(),
                                            reference_error, lifetime_root);
    if (!object) {
        Inspect::FreeObjectHandle(lifetime_root);
        error = reference_error.empty() ? "referenced argument is unavailable" : reference_error;
        return false;
    }
    if (parameter.type && !described.is_value_type && !Inspect::IsAssignableTo(object, parameter.type)) {
        const Inspect::TypeInfo actual = Inspect::TypeOf(object);
        Inspect::FreeObjectHandle(lifetime_root);
        error = "reference type mismatch for " + parameter.name + ": received " + actual.full_name;
        return false;
    }
    Inspect::ObjectHandle root = Inspect::PinObject(object);
    const Object stable = Inspect::ResolveObjectHandle(root);
    if (!root.handle || !stable) {
        Inspect::FreeObjectHandle(root);
        Inspect::FreeObjectHandle(lifetime_root);
        error = "referenced argument could not be rooted for invocation";
        return false;
    }
    if (lifetime_root.handle)
        roots.push_back(lifetime_root);
    roots.push_back(root);
    const Inspect::TypeInfo actual = Inspect::TypeOf(stable);
    value.kind = Inspect::type_name_looks_array(actual.full_name)
        ? Inspect::ValueKind::ArrayReference : Inspect::ValueKind::ObjectReference;
    value.object = stable.handle();
    value.display = actual.full_name;
    value.readable = true;
    return true;
}

} // namespace

PreparedValue::PreparedValue(PreparedValue&& other) noexcept
    : value(std::move(other.value)), roots(std::move(other.roots)), error(std::move(other.error)) {
    other.roots.clear();
}

PreparedValue& PreparedValue::operator=(PreparedValue&& other) noexcept {
    if (this == &other)
        return *this;
    for (auto& root : roots)
        URK::Unity::Inspect::FreeObjectHandle(root);
    value = std::move(other.value);
    roots = std::move(other.roots);
    error = std::move(other.error);
    other.roots.clear();
    return *this;
}

PreparedValue::~PreparedValue() {
    for (auto& root : roots)
        URK::Unity::Inspect::FreeObjectHandle(root);
}

PreparedValue prepare_value(std::string_view name, std::string_view type_name,
                            const void* type, bool is_value_type, bool is_enum,
                            const nlohmann::json& input,
                            const ReferenceResolver& resolve_reference) {
    URK::Unity::Inspect::MethodParamInfo parameter{};
    parameter.name = std::string(name);
    parameter.type_name = std::string(type_name);
    parameter.type = type;
    parameter.is_value_type = is_value_type;
    parameter.is_enum = is_enum;
    PreparedValue prepared;
    parse_argument(parameter, input, resolve_reference, prepared.roots, prepared.value, prepared.error);
    return prepared;
}

Result invoke(URK::Unity::Object target, const URK::Unity::Inspect::MethodInfo& method,
              const nlohmann::json& arguments, const ReferenceResolver& resolve_reference) {
    Result result{};
    if (!arguments.is_array() || arguments.size() != method.parameters.size()) {
        result.error = "Argument count does not match the method signature.";
        return result;
    }
    if (method.return_type_is_generic_parameter || std::any_of(
            method.parameters.begin(), method.parameters.end(),
            [](const URK::Unity::Inspect::MethodParamInfo& parameter) {
                return parameter.is_generic_parameter;
            })) {
        result.error = "Generic method invocation requires explicit generic type binding and is not exposed yet.";
        return result;
    }
    if (method.return_type_is_opaque || std::any_of(
            method.parameters.begin(), method.parameters.end(),
            [](const URK::Unity::Inspect::MethodParamInfo& parameter) { return parameter.is_opaque; })) {
        result.error = "The method uses a runtime-specific opaque signature.";
        return result;
    }
    std::vector<PreparedValue> prepared_values;
    std::vector<URK::Unity::Inspect::ValueInfo> parsed;
    prepared_values.reserve(method.parameters.size());
    parsed.reserve(method.parameters.size());
    for (std::size_t index = 0; index < method.parameters.size(); ++index) {
        PreparedValue prepared = prepare_value(method.parameters[index].name,
            method.parameters[index].type_name, method.parameters[index].type,
            method.parameters[index].is_value_type, method.parameters[index].is_enum,
            arguments[index], resolve_reference);
        if (!prepared.error.empty()) {
            result.error = std::move(prepared.error);
            return result;
        }
        parsed.push_back(prepared.value);
        prepared_values.push_back(std::move(prepared));
    }
    result.value = URK::Unity::Inspect::InvokeMethod(target, method, parsed);
    if (!result.value.readable) {
        const char* detail = URK::Unity::last_error();
        result.error = detail && detail[0] ? detail
            : (result.value.display.empty() ? "Managed invocation failed." : result.value.display);
    }
    return result;
}

} // namespace Explorer::Mcp::ManagedInvoke

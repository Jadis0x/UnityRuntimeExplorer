// Copyright (c) 2026 Jadis0x. All rights reserved.
#include "schema_validator.h"

#include <algorithm>
#include <limits>

namespace Explorer::Mcp {
namespace {
using Json = nlohmann::json;

bool matches_type(const Json& value, std::string_view type) {
    if (type == "object") return value.is_object();
    if (type == "array") return value.is_array();
    if (type == "string") return value.is_string();
    if (type == "boolean") return value.is_boolean();
    if (type == "integer") return value.is_number_integer() || value.is_number_unsigned();
    if (type == "number") return value.is_number();
    if (type == "null") return value.is_null();
    return true;
}

bool integer_value(const Json& value, long double& output) {
    if (value.is_number_unsigned())
        output = static_cast<long double>(value.get<std::uint64_t>());
    else if (value.is_number_integer())
        output = static_cast<long double>(value.get<std::int64_t>());
    else
        return false;
    return true;
}
} // namespace

bool validate_json_schema(const Json& value, const Json& schema, std::string& error, std::string path) {
    if (!schema.is_object()) {
        error = path + " has an invalid server schema";
        return false;
    }
    if (schema.contains("type") && schema["type"].is_string() &&
        !matches_type(value, schema["type"].get_ref<const std::string&>())) {
        error = path + " must be " + schema["type"].get<std::string>();
        return false;
    }
    if (schema.contains("enum") && schema["enum"].is_array() &&
        std::find(schema["enum"].begin(), schema["enum"].end(), value) == schema["enum"].end()) {
        error = path + " is not one of the allowed values";
        return false;
    }
    if (value.is_object()) {
        const Json properties = schema.value("properties", Json::object());
        const Json required = schema.value("required", Json::array());
        if (required.is_array())
            for (const Json& key : required)
                if (key.is_string() && !value.contains(key.get_ref<const std::string&>())) {
                    error = path + "." + key.get<std::string>() + " is required";
                    return false;
                }
        for (const auto& [key, child] : value.items()) {
            if (!properties.contains(key)) {
                if (schema.value("additionalProperties", true) == false) {
                    error = path + "." + key + " is not allowed";
                    return false;
                }
                continue;
            }
            if (!validate_json_schema(child, properties[key], error, path + "." + key))
                return false;
        }
    }
    if (value.is_array()) {
        if (schema.contains("maxItems") && value.size() > schema["maxItems"].get<std::size_t>()) {
            error = path + " contains too many items";
            return false;
        }
        if (schema.contains("minItems") && value.size() < schema["minItems"].get<std::size_t>()) {
            error = path + " contains too few items";
            return false;
        }
        if (schema.contains("items"))
            for (std::size_t index = 0; index < value.size(); ++index)
                if (!validate_json_schema(value[index], schema["items"], error,
                                          path + "[" + std::to_string(index) + "]"))
                    return false;
    }
    if (value.is_string()) {
        const std::size_t length = value.get_ref<const std::string&>().size();
        if (schema.contains("maxLength") && length > schema["maxLength"].get<std::size_t>()) {
            error = path + " is too long";
            return false;
        }
        if (schema.contains("minLength") && length < schema["minLength"].get<std::size_t>()) {
            error = path + " is too short";
            return false;
        }
    }
    long double number = 0;
    if (integer_value(value, number)) {
        if (schema.contains("minimum") && number < schema["minimum"].get<long double>()) {
            error = path + " is below the minimum";
            return false;
        }
        if (schema.contains("maximum") && number > schema["maximum"].get<long double>()) {
            error = path + " exceeds the maximum";
            return false;
        }
    }
    return true;
}

} // namespace Explorer::Mcp

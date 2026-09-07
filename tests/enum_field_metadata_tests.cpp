// Copyright (c) 2026 Jadis0x. All rights reserved.
// Covers a Mono-specific metadata gap: an enum-typed field's signature reports
// ELEMENT_TYPE_VALUETYPE (0x11), not IL2CPP's dedicated ENUM code (0x55), so
// describe_member_type() must resolve the field's class and check
// class_is_enum() itself instead of trusting the type code alone. Without
// that, an enum member field (e.g. a `const LightType Spot = ...` value) gets
// misclassified as a plain value type, and the Object Inspector falls back to
// printing the declaring class's name instead of the constant's value.
#include "sdk/mono/mono_runtime.h"
#include "sdk/unity/unity_inspect.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace {

const auto light_type_class = reinterpret_cast<const void*>(0x2100);
const auto light_type_type = reinterpret_cast<const void*>(0x2200);
const auto int_type = reinterpret_cast<const void*>(0x2300);
const auto value_field = reinterpret_cast<const void*>(0x2400);
const auto spot_field = reinterpret_cast<const void*>(0x2500);
const auto field_iterator_end = reinterpret_cast<void*>(0x1);

const void* find_class(const char* image, const char* namespc, const char* name) {
    (void)image;
    if (namespc && name && std::string(namespc) == "UnityEngine" && std::string(name) == "LightType")
        return light_type_class;
    return nullptr;
}

const void* class_get_fields(const void* klass, void** iterator) {
    if (klass != light_type_class || !iterator)
        return nullptr;
    if (*iterator == nullptr) {
        *iterator = const_cast<void*>(spot_field);
        return value_field;
    }
    if (*iterator == spot_field) {
        *iterator = field_iterator_end;
        return spot_field;
    }
    return nullptr;
}

const char* field_get_name(const void* field) {
    if (field == value_field) return "value__";
    if (field == spot_field) return "Spot";
    return nullptr;
}

const void* field_get_type(const void* field) {
    if (field == value_field) return int_type;
    if (field == spot_field) return light_type_type;
    return nullptr;
}

std::uint32_t field_get_flags(const void* field) {
    // FIELD_ATTRIBUTE_STATIC | FIELD_ATTRIBUTE_LITERAL, matching an enum
    // member's real IL flags (0x10 | 0x40).
    return field == spot_field ? 0x50u : 0u;
}

int type_get_type(const void* type) {
    if (type == int_type) return 0x08;        // ELEMENT_TYPE_I4
    if (type == light_type_type) return 0x11; // ELEMENT_TYPE_VALUETYPE
    return -1;
}

const void* type_get_class(const void* type) {
    return type == light_type_type ? light_type_class : nullptr;
}

int type_get_name(const void* type, char* output, std::size_t output_size) {
    if (!output || output_size == 0) return 0;
    if (type == int_type) { std::snprintf(output, output_size, "System.Int32"); return 1; }
    if (type == light_type_type) { std::snprintf(output, output_size, "UnityEngine.LightType"); return 1; }
    return 0;
}

const char* class_get_name(const void* klass) {
    return klass == light_type_class ? "LightType" : nullptr;
}

const char* class_get_namespace(const void* klass) {
    return klass == light_type_class ? "UnityEngine" : nullptr;
}

int class_is_valuetype(const void* klass) {
    return klass == light_type_class ? 1 : 0;
}

int class_is_enum(const void* klass) {
    return klass == light_type_class ? 1 : 0;
}

int attach_current_thread() { return 1; }
const char* last_error() { return nullptr; }

bool require(bool condition, const char* message) {
    if (condition)
        return true;
    std::fprintf(stderr, "FAILED: %s\n", message);
    return false;
}

} // namespace

int main() {
    URK_MonoApi mono{};
    mono.version = URK_MONO_API_VERSION;
    mono.size = sizeof(mono);
    mono.attach_current_thread = &attach_current_thread;
    mono.last_error = &last_error;
    mono.find_class = &find_class;
    mono.class_get_fields = &class_get_fields;
    mono.field_get_name = &field_get_name;
    mono.field_get_type = &field_get_type;
    mono.field_get_flags = &field_get_flags;
    mono.type_get_type = &type_get_type;
    mono.type_get_class = &type_get_class;
    mono.type_get_name = &type_get_name;
    mono.class_get_name = &class_get_name;
    mono.class_get_namespace = &class_get_namespace;
    mono.class_is_valuetype = &class_is_valuetype;
    mono.class_is_enum = &class_is_enum;

    URK_ModContext context{};
    context.version = URK_SDK_MIN_COMPAT_VERSION;
    context.size = sizeof(context);
    context.runtimeBackend = URK_RUNTIME_BACKEND_MONO;
    context.runtimeCapabilities = URK_RUNTIME_CAP_MONO_API;
    context.mono = &mono;

    bool ok = true;
    ok &= require(URK::mono::init(&context), "the fake Mono table must initialize");

    using namespace URK::Unity;
    using namespace URK::Unity::Inspect;

    const std::vector<FieldInfo> fields = Fields(TypeRef{"", "UnityEngine", "LightType"});
    ok &= require(!fields.empty(), "LightType must expose fields through the fake table");

    const FieldInfo* value_info = nullptr;
    const FieldInfo* spot_info = nullptr;
    for (const FieldInfo& field : fields) {
        if (field.name == "value__") value_info = &field;
        if (field.name == "Spot") spot_info = &field;
    }
    ok &= require(value_info != nullptr, "value__ must be present");
    ok &= require(spot_info != nullptr, "Spot must be present");

    if (value_info) {
        ok &= require(value_info->is_value_type, "value__ (Int32) is a value type");
        ok &= require(!value_info->is_enum, "value__ (Int32) is not itself an enum");
    }
    if (spot_info) {
        ok &= require(spot_info->is_value_type, "Spot (LightType) is a value type");
        // This is the actual bug: a VALUETYPE-coded field whose class is an enum
        // must still be recognized as an enum, the same as IL2CPP's dedicated
        // ENUM type code would be.
        ok &= require(spot_info->is_enum, "Spot (LightType) must be recognized as an enum field, "
                                           "not a generic value type");
    }

    if (!ok)
        return 1;
    std::printf("enum field metadata contract passed\n");
    return 0;
}

// Copyright (c) 2026 Jadis0x. All rights reserved.
// Covers Inspect::runtime_typed_result, which decides what an invoked method
// actually returned when the declared signature cannot say: `object`, an
// interface, a generic parameter and a runtime-specific type all describe a box
// without naming its contents.
#include "sdk/mono/mono_runtime.h"
#include "sdk/unity/unity_inspect.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

// Managed handles are only ever compared, never dereferenced by the runtime
// fakes below, so any distinct non-null addresses will do.
const auto int_class = reinterpret_cast<const void*>(0x1100);
const auto enum_class = reinterpret_cast<const void*>(0x1200);
const auto object_class = reinterpret_cast<const void*>(0x1300);
const auto int_type = reinterpret_cast<const void*>(0x1400);

void* boxed_int = nullptr;
void* boxed_enum = nullptr;
const auto plain_object = reinterpret_cast<void*>(0x2100);
const auto unowned_pointer = reinterpret_cast<void*>(0xDEAD0000);

std::int32_t boxed_int_payload = 42;
std::int32_t boxed_enum_payload = 3;

const void* object_get_class(void* object) {
    if (object == boxed_int) return int_class;
    if (object == boxed_enum) return enum_class;
    if (object == plain_object) return object_class;
    // Stands in for a result whose header the runtime cannot read.
    return nullptr;
}

void* object_unbox(void* object) {
    if (object == boxed_int) return &boxed_int_payload;
    if (object == boxed_enum) return &boxed_enum_payload;
    return nullptr;
}

const char* class_get_name(const void* klass) {
    if (klass == int_class) return "Int32";
    if (klass == enum_class) return "KeyCode";
    if (klass == object_class) return "GameObject";
    return nullptr;
}

const char* class_get_namespace(const void* klass) {
    if (klass == object_class) return "UnityEngine";
    return "System";
}

int class_is_valuetype(const void* klass) {
    return klass == int_class || klass == enum_class ? 1 : 0;
}

int class_is_enum(const void* klass) {
    return klass == enum_class ? 1 : 0;
}

// mono_class_enum_basetype is resolved from the live Mono module, which a test
// has no access to, so the underlying type has to come from the enum's
// `value__` field. That is the same fallback the decoder uses in a real
// process whenever the export is missing.
const auto value_field = reinterpret_cast<const void*>(0x1500);
const auto field_iterator_end = reinterpret_cast<void*>(0x1);

const void* class_get_fields(const void* klass, void** iterator) {
    if (klass != enum_class || !iterator || *iterator == field_iterator_end)
        return nullptr;
    *iterator = field_iterator_end;
    return value_field;
}

const char* field_get_name(const void* field) {
    return field == value_field ? "value__" : nullptr;
}

const void* field_get_type(const void* field) {
    return field == value_field ? int_type : nullptr;
}

int type_get_name(const void* type, char* output, std::size_t output_size) {
    if (type != int_type || !output || output_size == 0)
        return 0;
    std::snprintf(output, output_size, "System.Int32");
    return 1;
}

int attach_current_thread() {
    return 1;
}

const char* last_error() {
    return nullptr;
}

bool require(bool condition, const char* message) {
    if (condition)
        return true;
    std::fprintf(stderr, "FAILED: %s\n", message);
    return false;
}

} // namespace

int main() {
    // The boxes only have to be distinct readable addresses; the fake unboxes
    // them by identity rather than by walking a real object header.
    std::int32_t int_box_storage = 0;
    std::int32_t enum_box_storage = 0;
    boxed_int = &int_box_storage;
    boxed_enum = &enum_box_storage;

    URK_MonoApi mono{};
    mono.version = URK_MONO_API_VERSION;
    mono.size = sizeof(mono);
    mono.attach_current_thread = &attach_current_thread;
    mono.last_error = &last_error;
    mono.object_get_class = &object_get_class;
    mono.object_unbox = &object_unbox;
    mono.class_get_name = &class_get_name;
    mono.class_get_namespace = &class_get_namespace;
    mono.class_is_valuetype = &class_is_valuetype;
    mono.class_is_enum = &class_is_enum;
    mono.class_get_fields = &class_get_fields;
    mono.field_get_name = &field_get_name;
    mono.field_get_type = &field_get_type;
    mono.type_get_name = &type_get_name;

    URK_ModContext context{};
    context.version = URK_SDK_MIN_COMPAT_VERSION;
    context.size = sizeof(context);
    context.runtimeBackend = URK_RUNTIME_BACKEND_MONO;
    context.runtimeCapabilities = URK_RUNTIME_CAP_MONO_API;
    context.mono = &mono;

    bool ok = true;
    ok &= require(URK::mono::init(&context), "the fake Mono table must initialize");

    using namespace URK::Unity::Inspect;

    // A method declared to return `object` that hands back a boxed int must
    // report the value, not the name of the box's class.
    const ValueInfo boxed = runtime_typed_result("System.Object", boxed_int);
    ok &= require(boxed.kind == ValueKind::SignedInteger, "a boxed int result decodes as an integer");
    ok &= require(boxed.display == "42", "a boxed int result reports its value");
    ok &= require(boxed.readable, "a decoded result is readable");

    // An enum box decodes through its underlying type rather than falling back
    // to an opaque value-type placeholder.
    const ValueInfo enumerated = runtime_typed_result("System.Object", boxed_enum);
    ok &= require(enumerated.kind == ValueKind::Enum, "a boxed enum result decodes as an enum");
    ok &= require(enumerated.display == "3", "a boxed enum result reports its underlying value");
    ok &= require(enumerated.type_name == "System.KeyCode", "an enum result is named by its runtime class");

    // A reference result keeps the runtime class name, which is more specific
    // than the declared type it arrived under.
    const ValueInfo reference = runtime_typed_result("System.Object", plain_object);
    ok &= require(reference.kind == ValueKind::ObjectReference, "a reference result stays a reference");
    ok &= require(reference.object == plain_object, "a reference result keeps its pointer for inspection");
    ok &= require(reference.type_name == "UnityEngine.GameObject",
                  "a reference result is named by its runtime class, not the declaration");

    // A result whose class cannot be read must not be handed to the collector as
    // an object; it degrades to the raw pointer instead.
    const ValueInfo unknown = runtime_typed_result("System.Void*", unowned_pointer);
    ok &= require(unknown.kind == ValueKind::UnsignedInteger, "an unreadable result degrades to a raw pointer");
    ok &= require(unknown.object == nullptr, "an unreadable result is never offered for rooting");
    ok &= require(unknown.readable, "a raw pointer result is still reportable");
    ok &= require(unknown.display.find("0xDEAD0000") != std::string::npos,
                  "a raw pointer result shows the address");

    const ValueInfo empty = runtime_typed_result("System.Object", nullptr);
    ok &= require(empty.kind == ValueKind::Null, "a null result decodes as null");
    ok &= require(empty.readable, "a null result is readable");

    if (!ok)
        return 1;
    std::printf("invoke result decode contract passed\n");
    return 0;
}

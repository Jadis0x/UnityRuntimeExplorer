// Copyright (c) 2026 Jadis0x. All rights reserved.
// Covers MethodTraceValueDecoder: enum arguments resolve to their named constant
// plus the full set of constants, and reference results expand into their fields.
#include "mod/explorer/method_trace_value_decoder.h"
#include "sdk/il2cpp/il2cpp_runtime.h"
#include "sdk/unity/unity_inspect.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

namespace {

// Handles are compared, never dereferenced, so any distinct addresses do.
const auto format_enum_class = reinterpret_cast<const void*>(0x1100);
const auto wrapper_class = reinterpret_cast<const void*>(0x1200);
const auto payload_class = reinterpret_cast<const void*>(0x1300);
const auto string_class = reinterpret_cast<const void*>(0x1400);

const auto int_type = reinterpret_cast<const void*>(0x2100);
const auto string_type = reinterpret_cast<const void*>(0x2200);
const auto payload_type = reinterpret_cast<const void*>(0x2300);
const auto format_enum_type = reinterpret_cast<const void*>(0x2400);

// One field record per (class, member) pair the fakes expose.
const auto field_enum_value = reinterpret_cast<const void*>(0x3000);
const auto field_binary = reinterpret_cast<const void*>(0x3001);
const auto field_messagepack = reinterpret_cast<const void*>(0x3002);
const auto field_json = reinterpret_cast<const void*>(0x3003);
const auto field_wrapper_payload = reinterpret_cast<const void*>(0x3010);
const auto field_payload_id = reinterpret_cast<const void*>(0x3020);
const auto field_payload_label = reinterpret_cast<const void*>(0x3021);

// The managed objects the trace record points at.
const auto wrapper_object = reinterpret_cast<void*>(0x4100);
const auto payload_object = reinterpret_cast<void*>(0x4200);
const auto label_object = reinterpret_cast<void*>(0x4300);

constexpr std::uint32_t kStaticFlag = 0x0010;
constexpr std::int32_t kPayloadId = 42;

int is_available() { return 1; }
const char* last_error() { return nullptr; }

const void* object_get_class(void* object) {
    if (object == wrapper_object) return wrapper_class;
    if (object == payload_object) return payload_class;
    if (object == label_object) return string_class;
    return nullptr;
}

const char* class_get_name(const void* klass) {
    if (klass == format_enum_class) return "SerializedFormat";
    if (klass == wrapper_class) return "UnityArrayRef";
    if (klass == payload_class) return "Payload";
    if (klass == string_class) return "String";
    return nullptr;
}

const char* class_get_namespace(const void* klass) {
    if (klass == string_class) return "System";
    return "MonoGame.Core";
}

const void* class_get_parent(const void*) { return nullptr; }
int class_is_valuetype(const void* klass) { return klass == format_enum_class ? 1 : 0; }
int class_is_enum(const void* klass) { return klass == format_enum_class ? 1 : 0; }

// A field iterator that walks a fixed list per class. The iterator cookie is
// "index + 1" so that the initial null cookie means "start".
const void* class_get_fields(const void* klass, void** iterator) {
    static const void* const enum_fields[] = {field_enum_value, field_binary, field_messagepack, field_json};
    static const void* const wrapper_fields[] = {field_wrapper_payload};
    static const void* const payload_fields[] = {field_payload_id, field_payload_label};
    const void* const* fields = nullptr;
    std::size_t count = 0;
    if (klass == format_enum_class) { fields = enum_fields; count = 4; }
    else if (klass == wrapper_class) { fields = wrapper_fields; count = 1; }
    else if (klass == payload_class) { fields = payload_fields; count = 2; }
    if (!fields || !iterator)
        return nullptr;
    const std::size_t index = reinterpret_cast<std::uintptr_t>(*iterator);
    if (index >= count)
        return nullptr;
    *iterator = reinterpret_cast<void*>(static_cast<std::uintptr_t>(index + 1));
    return fields[index];
}

const char* field_get_name(const void* field) {
    if (field == field_enum_value) return "value__";
    if (field == field_binary) return "Binary";
    if (field == field_messagepack) return "MessagePack";
    if (field == field_json) return "Json";
    if (field == field_wrapper_payload) return "payload";
    if (field == field_payload_id) return "id";
    if (field == field_payload_label) return "label";
    return nullptr;
}

const void* field_get_type(const void* field) {
    if (field == field_wrapper_payload) return payload_type;
    if (field == field_payload_label) return string_type;
    if (field == field_binary || field == field_messagepack || field == field_json) return format_enum_type;
    return int_type;
}

std::uint32_t field_get_flags(const void* field) {
    return field == field_binary || field == field_messagepack || field == field_json ? kStaticFlag : 0;
}

int field_static_get_value(const void* field, void* output) {
    std::int32_t value = 0;
    if (field == field_binary) value = 0;
    else if (field == field_messagepack) value = 1;
    else if (field == field_json) value = 2;
    else return 0;
    std::memcpy(output, &value, sizeof(value));
    return 1;
}

// Inspect reads a reference field through the boxed accessor first; only a
// value-type field falls through to the raw accessor below.
void* field_get_value_object(const void* field, void* object) {
    if (field == field_wrapper_payload && object == wrapper_object) return payload_object;
    if (field == field_payload_label && object == payload_object) return label_object;
    return nullptr;
}

// Identity handles: the fakes have no collector, so rooting an object is a
// no-op that has to keep returning the same pointer.
std::uint32_t gchandle_new(void* object, int) {
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(object));
}

void* gchandle_get_target(std::uint32_t handle) {
    return reinterpret_cast<void*>(static_cast<std::uintptr_t>(handle));
}

void gchandle_free(std::uint32_t) {}

int field_get_value(void* object, const void* field, void* output) {
    if (object == wrapper_object && field == field_wrapper_payload) {
        void* value = payload_object;
        std::memcpy(output, &value, sizeof(value));
        return 1;
    }
    if (object == payload_object && field == field_payload_id) {
        std::memcpy(output, &kPayloadId, sizeof(kPayloadId));
        return 1;
    }
    if (object == payload_object && field == field_payload_label) {
        void* value = label_object;
        std::memcpy(output, &value, sizeof(value));
        return 1;
    }
    return 0;
}

int type_get_name(const void* type, char* output, std::size_t output_size) {
    const char* name = type == int_type          ? "System.Int32"
                     : type == string_type       ? "System.String"
                     : type == payload_type      ? "MonoGame.Core.Payload"
                     : type == format_enum_type  ? "MonoGame.Core.SerializedFormat"
                                                 : nullptr;
    if (!name || !output || output_size == 0)
        return 0;
    std::snprintf(output, output_size, "%s", name);
    return 1;
}

std::int32_t type_get_type(const void* type) {
    if (type == int_type) return 0x08;          // I4
    if (type == string_type) return 0x0E;       // STRING
    if (type == payload_type) return 0x12;      // CLASS
    if (type == format_enum_type) return 0x55;  // ENUM
    return -1;
}

const void* type_get_class(const void* type) {
    if (type == payload_type) return payload_class;
    if (type == format_enum_type) return format_enum_class;
    if (type == string_type) return string_class;
    return nullptr;
}

std::int32_t string_length(void* object) { return object == label_object ? 5 : -1; }

int string_to_utf8(void* object, char* output, std::size_t output_size) {
    if (object != label_object || !output || output_size == 0)
        return 0;
    std::snprintf(output, output_size, "hello");
    return 1;
}

bool require(bool condition, const char* message) {
    if (condition)
        return true;
    std::fprintf(stderr, "FAILED: %s\n", message);
    return false;
}

const Explorer::MethodTracer::ValueNode* find_child(const Explorer::MethodTracer::ValueNode& node,
                                                    std::string_view name) {
    for (const auto& child : node.children) {
        if (child.name == name)
            return &child;
    }
    return nullptr;
}

Explorer::MethodTracer::Snapshot make_trace() {
    Explorer::MethodTracer::Snapshot trace{};
    trace.id = 1;
    trace.active = true;
    trace.is_static = true;
    trace.captures_return = true;
    trace.method_name = "GetSerializedDataRef";
    trace.declaring_type = "MonoGame.ScriptFramework.DynamicMonoBehaviour";
    trace.return_type = "MonoGame.Core.UnityArrayRef";
    trace.return_is_reference = true;
    trace.start_timestamp_ticks = 1;
    trace.timestamp_frequency = 1000;
    trace.parameter_names = {"format"};
    trace.parameter_types = {"MonoGame.Core.SerializedFormat"};
    trace.parameter_type_handles = {format_enum_type};
    trace.parameter_value_classes = {format_enum_class};
    trace.parameter_value_sizes = {sizeof(std::int32_t)};
    trace.parameter_is_reference = {false};
    trace.parameter_is_value_type = {true};
    trace.parameter_is_enum = {true};
    trace.parameter_is_by_ref = {false};
    trace.parameter_enum_underlying_types = {"System.Int32"};
    trace.parameter_is_opaque = {false};
    trace.parameter_is_floating = {false};

    Explorer::MethodTracer::Record record{};
    record.sequence = 1;
    record.timestamp_ticks = 1;
    record.thread_id = 7;
    record.arguments = {1};
    record.argument_xmm_low = {0};
    record.argument_xmm_high = {0};
    record.argument_value_bytes = {{1, 0, 0, 0}};
    record.argument_byref_value_bytes = {{}};
    record.return_captured = true;
    record.return_rax = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(wrapper_object));
    trace.records.push_back(std::move(record));
    return trace;
}

} // namespace

int main() {
    URK_Il2CppApi il2cpp{};
    il2cpp.version = URK_IL2CPP_API_VERSION;
    il2cpp.size = sizeof(il2cpp);
    il2cpp.is_available = &is_available;
    il2cpp.last_error = &last_error;
    il2cpp.object_get_class = &object_get_class;
    il2cpp.class_get_name = &class_get_name;
    il2cpp.class_get_namespace = &class_get_namespace;
    il2cpp.class_get_parent = &class_get_parent;
    il2cpp.class_is_valuetype = &class_is_valuetype;
    il2cpp.class_is_enum = &class_is_enum;
    il2cpp.class_get_fields = &class_get_fields;
    il2cpp.field_get_name = &field_get_name;
    il2cpp.field_get_type = &field_get_type;
    il2cpp.field_get_flags = &field_get_flags;
    il2cpp.field_static_get_value = &field_static_get_value;
    il2cpp.field_get_value_object = &field_get_value_object;
    il2cpp.field_get_value = &field_get_value;
    il2cpp.type_get_name = &type_get_name;
    il2cpp.type_get_type = &type_get_type;
    il2cpp.type_get_class_or_element_class = &type_get_class;
    il2cpp.string_to_utf8 = &string_to_utf8;
    il2cpp.string_length = &string_length;
    il2cpp.gchandle_new = &gchandle_new;
    il2cpp.gchandle_get_target = &gchandle_get_target;
    il2cpp.gchandle_free = &gchandle_free;

    URK_ModContext context{};
    context.version = URK_SDK_MIN_COMPAT_VERSION;
    context.size = sizeof(context);
    context.runtimeBackend = URK_RUNTIME_BACKEND_IL2CPP;
    context.runtimeCapabilities = URK_RUNTIME_CAP_IL2CPP_API;
    context.il2cpp = &il2cpp;
    if (!require(URK::il2cpp::init(&context), "il2cpp runtime init"))
        return 1;

    Explorer::MethodTracer::Snapshot trace = make_trace();
    Explorer::MethodTraceValueDecoder::resolve_displays(trace);
    const Explorer::MethodTracer::Record& record = trace.records.front();

    bool ok = true;

    // The enum argument: the name, and the constants that give the name meaning.
    ok &= require(record.argument_nodes.size() == 1, "one decoded argument node");
    if (record.argument_nodes.size() == 1) {
        const Explorer::MethodTracer::ValueNode& format = record.argument_nodes.front();
        ok &= require(format.display.rfind("MessagePack", 0) == 0,
                      "enum argument resolves to its constant name");
        ok &= require(format.readable, "enum argument is marked readable");
        const auto* raw = find_child(format, "raw value");
        ok &= require(raw != nullptr && raw->display.rfind("1", 0) == 0,
                      "enum argument exposes its raw value");
        const auto* underlying = find_child(format, "underlying type");
        ok &= require(underlying != nullptr && underlying->display == "System.Int32",
                      "enum argument exposes its underlying type");
        ok &= require(find_child(format, "Binary") != nullptr && find_child(format, "Json") != nullptr,
                      "enum argument lists the other constants it could have taken");
        const auto* selected = find_child(format, "MessagePack");
        ok &= require(selected != nullptr &&
                          selected->display.find("<- this value") != std::string::npos,
                      "the matching enum constant is marked");
    }

    // The reference result: opened, and walked into.
    const Explorer::MethodTracer::ValueNode& result = record.return_node;
    ok &= require(result.inspect_address ==
                      static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(wrapper_object)),
                  "returned reference carries an inspectable address");
    ok &= require(result.type == "MonoGame.Core.UnityArrayRef", "returned reference reports its runtime type");
    const auto* payload = find_child(result, "payload");
    ok &= require(payload != nullptr, "returned reference expands into its fields");
    if (payload) {
        ok &= require(payload->inspect_address ==
                          static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(payload_object)),
                      "a reference field is itself inspectable");
        const auto* id = find_child(*payload, "id");
        ok &= require(id != nullptr && id->display == "42", "a nested field decodes its scalar value");
        const auto* label = find_child(*payload, "label");
        ok &= require(label != nullptr && label->display.find("hello") != std::string::npos,
                      "a nested string field decodes its text");
    }

    if (!ok) {
        std::fprintf(stderr, "method trace value contract failed\n");
        return 1;
    }
    std::printf("method trace value contract passed\n");
    return 0;
}

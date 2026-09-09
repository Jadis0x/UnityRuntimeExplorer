#include "method_trace_value_decoder.h"

#include "sdk/unity/unity_inspect.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace Explorer::MethodTraceValueDecoder {
namespace {

constexpr std::size_t kDecodeBudgetPerTrace = 192;
constexpr std::size_t kNewestDecodeReserve = 64;
constexpr std::size_t kMaxStructuredFields = 6;
constexpr std::size_t kMaxStructuredDepth = 2;
constexpr std::size_t kMaxCachedRecords = 16384;
// The tree view can go wider than the one-line summary; it's decoded once and cached.
constexpr std::size_t kMaxNodeFields = 48;
constexpr std::size_t kMaxNodeElements = 32;
constexpr std::size_t kMaxNodeConstants = 64;
constexpr std::size_t kMaxNodeDepth = 3;

using ValueNode = MethodTracer::ValueNode;

struct DecodedRecord {
    std::string target;
    std::string result;
    std::vector<std::string> arguments;
    std::vector<bool> argument_readable;
    bool return_readable = false;
    std::vector<ValueNode> argument_nodes;
    ValueNode return_node;
};

struct DecodeCursor {
    std::uint64_t start_timestamp_ticks = 0;
    std::uint64_t next_sequence = 0;
};

std::unordered_map<std::uint64_t, DecodedRecord>& cache() {
    static std::unordered_map<std::uint64_t, DecodedRecord> value;
    return value;
}

std::unordered_map<MethodTracer::TraceId, DecodeCursor>& decode_cursors() {
    static std::unordered_map<MethodTracer::TraceId, DecodeCursor> value;
    return value;
}

std::uint64_t cache_key(const MethodTracer::Snapshot& trace, std::uint64_t sequence) {
    std::uint64_t value = trace.id * 0x9E3779B185EBCA87ull;
    value ^= trace.start_timestamp_ticks + 0xC2B2AE3D27D4EB4Full + (value << 6u) + (value >> 2u);
    value ^= sequence + 0x165667B19E3779F9ull + (value << 6u) + (value >> 2u);
    return value;
}

bool is_string(std::string_view type) {
    return type == "System.String" || type == "String" || type == "string";
}

int native_fault_filter(unsigned long code) {
    return code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_IN_PAGE_ERROR
        ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH;
}

template <typename Read>
bool safely(Read&& read) {
#if defined(_WIN32)
    __try {
        read();
        return true;
    }
    __except (native_fault_filter(GetExceptionCode())) {
        return false;
    }
#else
    read();
    return true;
#endif
}

std::string type_display(const URK::Unity::Inspect::TypeInfo& type, std::string_view fallback) {
    return type.full_name.empty() ? std::string(fallback.empty() ? "object" : fallback) : type.full_name;
}

std::size_t scalar_storage_size(std::string_view type) {
    if (type == "System.Boolean" || type == "Boolean" || type == "bool" ||
        type == "System.SByte" || type == "SByte" || type == "sbyte" ||
        type == "System.Byte" || type == "Byte" || type == "byte")
        return 1;
    if (type == "System.Char" || type == "Char" || type == "char" ||
        type == "System.Int16" || type == "Int16" || type == "short" ||
        type == "System.UInt16" || type == "UInt16" || type == "ushort")
        return 2;
    if (type == "System.Int32" || type == "Int32" || type == "int" ||
        type == "System.UInt32" || type == "UInt32" || type == "uint" ||
        type == "System.Single" || type == "Single" || type == "float")
        return 4;
    return 8;
}

std::uint64_t scalar_bits(const void* source, std::size_t byte_count) {
    std::uint64_t value = 0;
    std::memcpy(&value, source, std::min(byte_count, sizeof(value)));
    return value;
}

using EnumConstants = std::vector<std::pair<std::string, std::uint64_t>>;

// out_constants/out_raw let the tree builder reuse this walk instead of redoing it.
std::string decode_enum(std::string_view type_name, const void* type_handle,
                        const void* value_class, std::string_view underlying_type,
                        const void* data, bool& readable, EnumConstants* out_constants = nullptr,
                        std::uint64_t* out_raw = nullptr) {
    readable = false;
    const URK::Unity::Inspect::ValueInfo raw = URK::Unity::Inspect::enum_from_pointer(
        std::string(type_name), std::string(underlying_type), const_cast<void*>(data));
    if (!raw.readable)
        return std::string(type_name) + " (enum underlying type unavailable)";

    const std::size_t bytes = scalar_storage_size(underlying_type);
    const std::uint64_t expected = scalar_bits(data, bytes);
    if (out_raw)
        *out_raw = expected;
    const auto* klass = value_class ? static_cast<const URK::managed::Class*>(value_class)
        : type_handle ? URK::managed::type_get_class_or_element_class(
              static_cast<const URK::managed::Type*>(type_handle)) : nullptr;
    if (!klass)
        return raw.display + " (" + std::string(type_name) + ")";

    EnumConstants constants;
    void* iterator = nullptr;
    while (const auto* field = URK::managed::class_get_fields(klass, &iterator)) {
        const std::uint32_t flags = URK::managed::field_get_flags(field);
        const char* name = URK::managed::field_get_name(field);
        if (!name || std::string_view(name) == "value__" ||
            (flags & URK::Unity::Inspect::kStaticMemberFlag) == 0)
            continue;

        std::uint64_t candidate = 0;
        if (URK::managed::field_static_get_value(field, &candidate))
            constants.emplace_back(name, candidate);
        // Literal constants lack normal static storage; fall back to the boxed accessor.
        void* boxed = URK::managed::field_get_value_object(field, nullptr);
        void* unboxed = boxed ? URK::managed::object_unbox(static_cast<URK::managed::Object*>(boxed)) : nullptr;
        if (unboxed) {
            candidate = scalar_bits(unboxed, bytes);
            constants.emplace_back(name, candidate);
        }
    }
    if (out_constants)
        *out_constants = constants;
    for (const auto& [name, value] : constants) {
        if (value == expected) {
            readable = true;
            return name + " (" + raw.display + ")";
        }
    }
    std::uint64_t remaining = expected;
    std::string flags;
    for (const auto& [name, value] : constants) {
        if (value == 0 || (remaining & value) != value)
            continue;
        if (!flags.empty())
            flags += " | ";
        flags += name;
        remaining &= ~value;
    }
    if (!flags.empty() && remaining == 0) {
        readable = true;
        return flags + " (" + raw.display + ")";
    }
    readable = true;
    return raw.display + " (unknown " + std::string(type_name) + " value)";
}

std::string decode_reference(std::uint64_t raw, std::string_view declared_type, bool& readable) {
    readable = false;
    if (raw == 0) {
        readable = true;
        return "null";
    }

    std::string result;
    const bool complete = safely([&] {
        const URK::Unity::Object object{reinterpret_cast<void*>(static_cast<std::uintptr_t>(raw))};
        if (is_string(declared_type)) {
            const URK::Unity::Inspect::ValueInfo value =
                URK::Unity::Inspect::string_value(std::string(declared_type), object.handle());
            result = value.display;
            readable = value.readable;
            return;
        }
        const URK::Unity::Inspect::ObjectRefInfo info = URK::Unity::Inspect::DescribeObject(object);
        if (!info.handle) {
            result = std::string(declared_type.empty() ? "object" : declared_type) + " (runtime type unavailable)";
            return;
        }
        result = type_display(info.type, declared_type);
        if (info.type.full_name.find("[]") != std::string::npos && URK::managed::has_array_length()) {
            const std::size_t length = URK::managed::array_length(
                static_cast<URK::managed::Array*>(object.handle()));
            result += " [" + std::to_string(length) + "]";
        }
        readable = true;
    });
    if (!complete)
        return std::string(declared_type.empty() ? "object" : declared_type) + " (reference unreadable)";
    return result.empty() ? std::string(declared_type.empty() ? "object" : declared_type) : result;
}

std::string decode_boxed_struct(const URK::Unity::Object& object, std::string_view type_name,
                                std::size_t depth) {
    std::vector<URK::Unity::Inspect::FieldInfo> fields = URK::Unity::Inspect::Fields(object, false);
    fields.erase(std::remove_if(fields.begin(), fields.end(), [](const auto& field) {
        return field.is_static;
    }), fields.end());

    std::string result = std::string(type_name) + " {";
    const std::size_t count = std::min(fields.size(), kMaxStructuredFields);
    for (std::size_t index = 0; index < count; ++index) {
        const auto& field_info = fields[index];
        const URK::Unity::Inspect::ValueInfo field = URK::Unity::Inspect::ReadField(object, field_info);
        std::string display = field.display.empty() ? "<unavailable>" : field.display;
        if (depth < kMaxStructuredDepth && field_info.is_value_type && !field_info.is_enum && field.object) {
            URK::Unity::Inspect::ObjectHandle root = URK::Unity::Inspect::PinObject(
                URK::Unity::Object{field.object});
            const URK::Unity::Object nested = URK::Unity::Inspect::ResolveObjectHandle(root);
            if (root.handle && nested)
                display = decode_boxed_struct(nested, field_info.type_name, depth + 1);
            URK::Unity::Inspect::FreeObjectHandle(root);
        }
        if (index != 0)
            result += ", ";
        result += field_info.name + "=" + display;
    }
    if (fields.size() > count)
        result += ", ...";
    return result + "}";
}

std::string decode_value_type(std::string_view type_name, const void* type_handle,
                              const void* value_class, const std::vector<std::uint8_t>& bytes, bool is_enum,
                              std::string_view enum_underlying_type, bool& readable) {
    readable = false;
    if (bytes.empty())
        return std::string(type_name) + " (ABI value was not captured)";

    std::string result;
    const bool complete = safely([&] {
        void* data = const_cast<std::uint8_t*>(bytes.data());
        if (is_enum) {
            result = decode_enum(type_name, type_handle, value_class, enum_underlying_type, data, readable);
            return;
        }
        URK::Unity::Inspect::ValueInfo scalar =
            URK::Unity::Inspect::scalar_from_pointer(std::string(type_name), data);
        if (scalar.readable && scalar.kind != URK::Unity::Inspect::ValueKind::ValueType) {
            result = scalar.display;
            readable = true;
            return;
        }

        const auto* klass = value_class ? static_cast<const URK::managed::Class*>(value_class)
            : type_handle ? URK::managed::type_get_class_or_element_class(
                  static_cast<const URK::managed::Type*>(type_handle)) : nullptr;
        if (!klass) {
            result = std::string(type_name) + " {" + std::to_string(bytes.size()) + " bytes}";
            return;
        }
        void* boxed = URK::managed::value_box(klass, data);
        if (!boxed) {
            result = std::string(type_name) + " (" + URK::compiled_runtime_name + " value_box failed)";
            return;
        }
        URK::Unity::Inspect::ObjectHandle root = URK::Unity::Inspect::PinObject(URK::Unity::Object{boxed});
        const URK::Unity::Object stable = URK::Unity::Inspect::ResolveObjectHandle(root);
        if (!root.handle || !stable) {
            URK::Unity::Inspect::FreeObjectHandle(root);
            result = std::string(type_name) + " (boxed value could not be retained)";
            return;
        }
        result = decode_boxed_struct(stable, type_name, 0);
        URK::Unity::Inspect::FreeObjectHandle(root);
        readable = true;
    });
    if (!complete)
        return std::string(type_name) + " (metadata decode fault)";
    return result.empty() ? std::string(type_name) + " {" + std::to_string(bytes.size()) + " bytes}" : result;
}

std::string hex_text(std::uint64_t value) {
    char text[32]{};
    std::snprintf(text, sizeof(text), "0x%llX", static_cast<unsigned long long>(value));
    return text;
}

ValueNode build_reference_node(std::uint64_t raw, std::string_view declared_type, std::size_t depth);

// Hangs one child per instance field (including inherited) off `node`.
void fill_object_children(const URK::Unity::Object& object, std::size_t depth, ValueNode& node) {
    std::vector<URK::Unity::Inspect::FieldInfo> fields = URK::Unity::Inspect::Fields(object, true);
    fields.erase(std::remove_if(fields.begin(), fields.end(), [](const auto& field) {
        return field.is_static;
    }), fields.end());

    const std::size_t count = std::min(fields.size(), kMaxNodeFields);
    node.truncated = node.truncated || fields.size() > count;
    node.children.reserve(node.children.size() + count);
    for (std::size_t index = 0; index < count; ++index) {
        const URK::Unity::Inspect::FieldInfo& field_info = fields[index];
        const URK::Unity::Inspect::ValueInfo field = URK::Unity::Inspect::ReadField(object, field_info);
        ValueNode child{};
        child.name = field_info.name;
        child.type = field_info.type_name;
        child.display = field.display.empty() ? "<unavailable>" : field.display;
        child.readable = field.readable;

        const auto address = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(field.object));
        if (!field_info.is_value_type && field.object) {
            // Expand inline so a wrapper's real data is visible without opening the Inspector.
            if (depth + 1 < kMaxNodeDepth) {
                ValueNode expanded = build_reference_node(address, field_info.type_name, depth + 1);
                if (!expanded.display.empty())
                    child.display = expanded.display;
                child.type = expanded.type;
                child.readable = expanded.readable;
                child.children = std::move(expanded.children);
                child.truncated = expanded.truncated;
            }
            child.inspect_address = address;
        } else if (field_info.is_value_type && !field_info.is_enum && field.object &&
                   depth + 1 < kMaxNodeDepth) {
            URK::Unity::Inspect::ObjectHandle root =
                URK::Unity::Inspect::PinObject(URK::Unity::Object{field.object});
            const URK::Unity::Object nested = URK::Unity::Inspect::ResolveObjectHandle(root);
            if (root.handle && nested)
                fill_object_children(nested, depth + 1, child);
            URK::Unity::Inspect::FreeObjectHandle(root);
        }
        node.children.push_back(std::move(child));
    }
}

void fill_array_children(const URK::Unity::Object& object, std::string_view type_name, std::size_t depth,
                         ValueNode& node) {
    const URK::Unity::Inspect::ValueInfo array =
        URK::Unity::Inspect::array_reference_value(std::string(type_name), object.handle());
    const std::size_t length = URK::Unity::Inspect::ArrayLength(array);
    const std::size_t count = std::min(length, kMaxNodeElements);
    node.truncated = node.truncated || length > count;
    for (std::size_t index = 0; index < count; ++index) {
        const URK::Unity::Inspect::ValueInfo element = URK::Unity::Inspect::ReadArrayElement(array, index);
        ValueNode child{};
        child.name = "[" + std::to_string(index) + "]";
        child.type = element.type_name;
        child.display = element.display.empty() ? "<unavailable>" : element.display;
        child.readable = element.readable;
        if (element.object && (element.kind == URK::Unity::Inspect::ValueKind::ObjectReference ||
                               element.kind == URK::Unity::Inspect::ValueKind::ArrayReference ||
                               element.kind == URK::Unity::Inspect::ValueKind::String)) {
            child.inspect_address = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(element.object));
            if (depth + 1 < kMaxNodeDepth) {
                ValueNode expanded = build_reference_node(child.inspect_address, element.type_name, depth + 1);
                child.children = std::move(expanded.children);
                child.truncated = expanded.truncated;
            }
        }
        node.children.push_back(std::move(child));
    }
}

ValueNode build_reference_node(std::uint64_t raw, std::string_view declared_type, std::size_t depth) {
    ValueNode node{};
    node.type = std::string(declared_type);
    bool readable = false;
    node.display = decode_reference(raw, declared_type, readable);
    node.readable = readable;
    if (raw == 0)
        return node;
    node.inspect_address = raw;
    if (depth >= kMaxNodeDepth)
        return node;

    safely([&] {
        const URK::Unity::Object object{reinterpret_cast<void*>(static_cast<std::uintptr_t>(raw))};
        const URK::Unity::Inspect::ObjectRefInfo info = URK::Unity::Inspect::DescribeObject(object);
        if (!info.handle)
            return;
        if (!info.type.full_name.empty())
            node.type = info.type.full_name;
        // A string's display already shows its value; skip its private fields.
        if (is_string(info.type.full_name) || is_string(declared_type))
            return;
        if (URK::Unity::Inspect::type_name_looks_array(info.type.full_name)) {
            fill_array_children(object, info.type.full_name, depth, node);
            return;
        }
        fill_object_children(object, depth, node);
    });
    return node;
}

ValueNode build_enum_node(std::string_view type_name, const void* type_handle, const void* value_class,
                          std::string_view underlying_type, const void* data) {
    ValueNode node{};
    node.type = std::string(type_name);
    EnumConstants constants;
    std::uint64_t raw = 0;
    bool readable = false;
    node.display = decode_enum(type_name, type_handle, value_class, underlying_type, data, readable,
                               &constants, &raw);
    node.readable = readable;

    ValueNode underlying{};
    underlying.name = "underlying type";
    underlying.display = underlying_type.empty() ? "<unavailable>" : std::string(underlying_type);
    underlying.readable = !underlying_type.empty();
    node.children.push_back(std::move(underlying));

    ValueNode numeric{};
    numeric.name = "raw value";
    numeric.type = std::string(underlying_type);
    numeric.display = std::to_string(raw) + "  (" + hex_text(raw) + ")";
    numeric.readable = true;
    node.children.push_back(std::move(numeric));

    // Lists the enum's other defined values, so e.g. "MessagePack (1)" is legible.
    const std::size_t count = std::min(constants.size(), kMaxNodeConstants);
    node.truncated = constants.size() > count;
    for (std::size_t index = 0; index < count; ++index) {
        const auto& constant_entry = constants[index];
        ValueNode constant{};
        constant.name = constant_entry.first;
        constant.display = std::to_string(constant_entry.second) + "  (" + hex_text(constant_entry.second) + ")";
        if (constant_entry.second == raw)
            constant.display += "   <- this value";
        else if (constant_entry.second != 0 && (raw & constant_entry.second) == constant_entry.second)
            constant.display += "   <- set";
        constant.readable = true;
        node.children.push_back(std::move(constant));
    }
    return node;
}

ValueNode build_value_node(std::string_view type_name, const void* type_handle, const void* value_class,
                           const std::vector<std::uint8_t>& bytes, bool is_enum,
                           std::string_view enum_underlying_type) {
    if (is_enum && !bytes.empty()) {
        ValueNode node{};
        const bool complete = safely([&] {
            node = build_enum_node(type_name, type_handle, value_class, enum_underlying_type, bytes.data());
        });
        if (complete)
            return node;
    }

    ValueNode node{};
    node.type = std::string(type_name);
    bool readable = false;
    node.display = decode_value_type(type_name, type_handle, value_class, bytes, is_enum,
                                     enum_underlying_type, readable);
    node.readable = readable;
    if (is_enum || bytes.empty())
        return node;

    safely([&] {
        const auto* klass = value_class ? static_cast<const URK::managed::Class*>(value_class)
            : type_handle ? URK::managed::type_get_class_or_element_class(
                  static_cast<const URK::managed::Type*>(type_handle)) : nullptr;
        if (!klass)
            return;
        void* boxed = URK::managed::value_box(klass, const_cast<std::uint8_t*>(bytes.data()));
        if (!boxed)
            return;
        URK::Unity::Inspect::ObjectHandle root = URK::Unity::Inspect::PinObject(URK::Unity::Object{boxed});
        const URK::Unity::Object stable = URK::Unity::Inspect::ResolveObjectHandle(root);
        if (root.handle && stable)
            fill_object_children(stable, 0, node);
        URK::Unity::Inspect::FreeObjectHandle(root);
    });
    return node;
}

std::vector<std::uint8_t> scalar_bytes(std::uint64_t value, std::size_t size) {
    std::vector<std::uint8_t> bytes(size);
    if (!bytes.empty())
        std::memcpy(bytes.data(), &value, std::min(size, sizeof(value)));
    return bytes;
}

DecodedRecord decode_record(const MethodTracer::Snapshot& trace, const MethodTracer::Record& record) {
    DecodedRecord decoded{};
    if (trace.target_is_reference) {
        bool target_readable = false;
        decoded.target = decode_reference(record.target_address, trace.declaring_type, target_readable);
        if (record.inline_site_address != 0 && record.target_address == 0)
            decoded.target = "<instance not identified in the inlined copy>";
    }

    // A hook inside an inlined copy sees the body, not a call frame: by then the
    // arguments live wherever the surrounding code put them, so reporting the
    // registers would be reporting someone else's values.
    if (record.inline_site_address != 0) {
        decoded.arguments.assign(record.arguments.size(), "<not captured at an inlined copy>");
        decoded.argument_readable.assign(record.arguments.size(), false);
        decoded.argument_nodes.resize(record.arguments.size());
        for (std::size_t index = 0; index < decoded.argument_nodes.size(); ++index) {
            decoded.argument_nodes[index].display = decoded.arguments[index];
            if (index < trace.parameter_types.size())
                decoded.argument_nodes[index].type = trace.parameter_types[index];
        }
        return decoded;
    }

    decoded.arguments.resize(record.arguments.size());
    decoded.argument_readable.resize(record.arguments.size(), false);
    decoded.argument_nodes.resize(record.arguments.size());
    for (std::size_t index = 0; index < record.arguments.size(); ++index) {
        std::string_view type = "<unknown>";
        if (index < trace.parameter_types.size())
            type = trace.parameter_types[index];
        const bool is_reference = index < trace.parameter_is_reference.size() && trace.parameter_is_reference[index];
        const bool is_enum = index < trace.parameter_is_enum.size() && trace.parameter_is_enum[index];
        const bool is_by_ref = index < trace.parameter_is_by_ref.size() && trace.parameter_is_by_ref[index];
        if (is_reference) {
            std::uint64_t value = record.arguments[index];
            if (is_by_ref && index < record.argument_byref_value_bytes.size() &&
                !record.argument_byref_value_bytes[index].empty())
                std::memcpy(&value, record.argument_byref_value_bytes[index].data(),
                            std::min(sizeof(value), record.argument_byref_value_bytes[index].size()));
            ValueNode node = build_reference_node(value, type, 0);
            decoded.arguments[index] = node.display;
            decoded.argument_readable[index] = node.readable;
            decoded.argument_nodes[index] = std::move(node);
            continue;
        }
        const std::vector<std::uint8_t>* bytes = nullptr;
        if (is_by_ref) {
            if (index < record.argument_byref_value_bytes.size() &&
                !record.argument_byref_value_bytes[index].empty())
                bytes = &record.argument_byref_value_bytes[index];
        } else if (index < record.argument_value_bytes.size() &&
                   !record.argument_value_bytes[index].empty()) {
            bytes = &record.argument_value_bytes[index];
        }
        const std::size_t value_size = index < trace.parameter_value_sizes.size()
            ? trace.parameter_value_sizes[index] : 0;
        // Win64 passes large value types by address, not by value - never box that word as data.
        const std::vector<std::uint8_t> fallback = (!is_by_ref && value_size <= sizeof(std::uint64_t))
            ? scalar_bytes(record.arguments[index], sizeof(std::uint64_t))
            : std::vector<std::uint8_t>{};
        const void* type_handle = index < trace.parameter_type_handles.size() ? trace.parameter_type_handles[index] : nullptr;
        const void* value_class = index < trace.parameter_value_classes.size()
            ? trace.parameter_value_classes[index] : nullptr;
        const std::string_view underlying = index < trace.parameter_enum_underlying_types.size()
            ? std::string_view(trace.parameter_enum_underlying_types[index]) : std::string_view{};
        ValueNode node = build_value_node(
            type, type_handle, value_class, bytes ? *bytes : fallback, is_enum, underlying);
        decoded.arguments[index] = node.display;
        decoded.argument_readable[index] = node.readable;
        decoded.argument_nodes[index] = std::move(node);
    }

    if (!record.return_captured)
        return decoded;
    if (trace.return_is_reference) {
        decoded.return_node = build_reference_node(record.return_rax, trace.return_type, 0);
        decoded.result = decoded.return_node.display;
        decoded.return_readable = decoded.return_node.readable;
        return decoded;
    }
    if (!trace.return_is_value_type && !trace.return_is_enum)
        return decoded;
    const std::vector<std::uint8_t>* bytes = !record.return_value_bytes.empty() ? &record.return_value_bytes : nullptr;
    const std::vector<std::uint8_t> fallback = scalar_bytes(
        trace.return_is_floating ? record.return_xmm_low : record.return_rax, sizeof(std::uint64_t));
    decoded.return_node = build_value_node(
        trace.return_type, trace.return_type_handle, trace.return_value_class, bytes ? *bytes : fallback,
        trace.return_is_enum, trace.return_enum_underlying_type);
    decoded.result = decoded.return_node.display;
    decoded.return_readable = decoded.return_node.readable;
    return decoded;
}

} // namespace

void resolve_displays(MethodTracer::Snapshot& trace) {
    // Decoding walks referenced fields, so bound it by time (like the hierarchy
    // census) rather than record count; the rest decodes on later frames.
    const std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(3);
    auto apply = [&](MethodTracer::Record& record) {
        const std::uint64_t key = cache_key(trace, record.sequence);
        auto found = cache().find(key);
        if (found == cache().end()) {
            if (cache().size() >= kMaxCachedRecords)
                cache().clear();
            found = cache().emplace(key, decode_record(trace, record)).first;
        }
        record.target_display = found->second.target;
        record.return_display = found->second.result;
        record.argument_displays = found->second.arguments;
        record.argument_readable = found->second.argument_readable;
        record.return_readable = found->second.return_readable;
        record.argument_nodes = found->second.argument_nodes;
        record.return_node = found->second.return_node;
    };

    // Reapply already-cached values first, cheaply, so rows stay stable while recording.
    for (MethodTracer::Record& record : trace.records) {
        const auto found = cache().find(cache_key(trace, record.sequence));
        if (found == cache().end())
            continue;
        record.target_display = found->second.target;
        record.return_display = found->second.result;
        record.argument_displays = found->second.arguments;
        record.argument_readable = found->second.argument_readable;
        record.return_readable = found->second.return_readable;
        record.argument_nodes = found->second.argument_nodes;
        record.return_node = found->second.return_node;
    }

    if (trace.records.empty())
        return;

    std::size_t budget = kDecodeBudgetPerTrace;
    const std::size_t newest_budget = std::min(kNewestDecodeReserve, budget);
    for (std::size_t offset = 0; offset < trace.records.size() && offset < newest_budget; ++offset) {
        MethodTracer::Record& record = trace.records[trace.records.size() - 1 - offset];
        if (!cache().contains(cache_key(trace, record.sequence))) {
            apply(record);
            --budget;
            if (std::chrono::steady_clock::now() >= deadline)
                return;
        }
    }

    DecodeCursor& cursor = decode_cursors()[trace.id];
    if (cursor.start_timestamp_ticks != trace.start_timestamp_ticks ||
        cursor.next_sequence < trace.records.front().sequence ||
        cursor.next_sequence > trace.records.back().sequence) {
        cursor.start_timestamp_ticks = trace.start_timestamp_ticks;
        cursor.next_sequence = trace.records.front().sequence;
    }
    const auto cursor_record = std::lower_bound(
        trace.records.begin(), trace.records.end(), cursor.next_sequence,
        [](const MethodTracer::Record& record, std::uint64_t sequence) {
            return record.sequence < sequence;
        });
    std::size_t index = cursor_record == trace.records.end()
        ? 0 : static_cast<std::size_t>(cursor_record - trace.records.begin());
    for (std::size_t visited = 0; visited < trace.records.size() && budget != 0; ++visited) {
        MethodTracer::Record& record = trace.records[index];
        if (!cache().contains(cache_key(trace, record.sequence))) {
            apply(record);
            --budget;
            if (std::chrono::steady_clock::now() >= deadline) {
                cursor.next_sequence = record.sequence;
                return;
            }
        }
        index = (index + 1) % trace.records.size();
    }
    cursor.next_sequence = trace.records[index].sequence;
}

} // namespace Explorer::MethodTraceValueDecoder

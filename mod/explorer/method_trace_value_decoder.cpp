#include "method_trace_value_decoder.h"

#include "sdk/unity/unity_inspect.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <algorithm>
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

struct DecodedRecord {
    std::string target;
    std::string result;
    std::vector<std::string> arguments;
    std::vector<bool> argument_readable;
    bool return_readable = false;
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

std::string decode_enum(std::string_view type_name, const void* type_handle,
                        const void* value_class, std::string_view underlying_type,
                        const void* data, bool& readable) {
    readable = false;
    const URK::Unity::Inspect::ValueInfo raw = URK::Unity::Inspect::enum_from_pointer(
        std::string(type_name), std::string(underlying_type), const_cast<void*>(data));
    if (!raw.readable)
        return std::string(type_name) + " (enum underlying type unavailable)";

    const std::size_t bytes = scalar_storage_size(underlying_type);
    const std::uint64_t expected = scalar_bits(data, bytes);
    const auto* klass = value_class ? static_cast<const URK::managed::Class*>(value_class)
        : type_handle ? URK::managed::type_get_class_or_element_class(
              static_cast<const URK::managed::Type*>(type_handle)) : nullptr;
    if (!klass)
        return raw.display + " (" + std::string(type_name) + ")";

    std::vector<std::pair<std::string, std::uint64_t>> constants;
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
        // Literal enum constants are not required to have normal static
        // storage. The boxed accessor is the authoritative fallback.
        void* boxed = URK::managed::field_get_value_object(field, nullptr);
        void* unboxed = boxed ? URK::managed::object_unbox(static_cast<URK::managed::Object*>(boxed)) : nullptr;
        if (unboxed) {
            candidate = scalar_bits(unboxed, bytes);
            constants.emplace_back(name, candidate);
        }
    }
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
    }

    decoded.arguments.resize(record.arguments.size());
    decoded.argument_readable.resize(record.arguments.size(), false);
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
            bool readable = false;
            decoded.arguments[index] = decode_reference(value, type, readable);
            decoded.argument_readable[index] = readable;
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
        // Win64 passes value types larger than eight bytes indirectly.  Their
        // ABI word is an address, not their value, so never box it as data.
        const std::vector<std::uint8_t> fallback = (!is_by_ref && value_size <= sizeof(std::uint64_t))
            ? scalar_bytes(record.arguments[index], sizeof(std::uint64_t))
            : std::vector<std::uint8_t>{};
        const void* type_handle = index < trace.parameter_type_handles.size() ? trace.parameter_type_handles[index] : nullptr;
        const void* value_class = index < trace.parameter_value_classes.size()
            ? trace.parameter_value_classes[index] : nullptr;
        const std::string_view underlying = index < trace.parameter_enum_underlying_types.size()
            ? std::string_view(trace.parameter_enum_underlying_types[index]) : std::string_view{};
        bool readable = false;
        decoded.arguments[index] = decode_value_type(
            type, type_handle, value_class, bytes ? *bytes : fallback, is_enum, underlying, readable);
        decoded.argument_readable[index] = readable;
    }

    if (!record.return_captured)
        return decoded;
    if (trace.return_is_reference) {
        decoded.result = decode_reference(record.return_rax, trace.return_type, decoded.return_readable);
        return decoded;
    }
    if (!trace.return_is_value_type && !trace.return_is_enum)
        return decoded;
    const std::vector<std::uint8_t>* bytes = !record.return_value_bytes.empty() ? &record.return_value_bytes : nullptr;
    const std::vector<std::uint8_t> fallback = scalar_bytes(
        trace.return_is_floating ? record.return_xmm_low : record.return_rax, sizeof(std::uint64_t));
    decoded.result = decode_value_type(
        trace.return_type, trace.return_type_handle, trace.return_value_class, bytes ? *bytes : fallback,
        trace.return_is_enum, trace.return_enum_underlying_type, decoded.return_readable);
    return decoded;
}

} // namespace

void resolve_displays(MethodTracer::Snapshot& trace) {
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
    };

    // Reapply values already resolved in a previous snapshot first. This is
    // cheap and makes decoded rows stable while the recorder is still active.
    for (MethodTracer::Record& record : trace.records) {
        const auto found = cache().find(cache_key(trace, record.sequence));
        if (found == cache().end())
            continue;
        record.target_display = found->second.target;
        record.return_display = found->second.result;
        record.argument_displays = found->second.arguments;
        record.argument_readable = found->second.argument_readable;
        record.return_readable = found->second.return_readable;
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
        }
        index = (index + 1) % trace.records.size();
    }
    cursor.next_sequence = trace.records[index].sequence;
}

} // namespace Explorer::MethodTraceValueDecoder

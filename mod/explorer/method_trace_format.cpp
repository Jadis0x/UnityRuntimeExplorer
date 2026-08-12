// Copyright (c) 2026 Jadis0x. All rights reserved.
#include "method_trace_format.h"

#include <algorithm>
#include <bit>
#include <cstdio>
#include <cstring>
#include <string_view>
#include <utility>

namespace Explorer::MethodTraceFormat {
namespace {

bool type_is(std::string_view type, std::string_view full, std::string_view short_name,
             std::string_view alias = {}) {
    return type == full || type == short_name || (!alias.empty() && type == alias);
}

std::string scalar_value(std::string_view type, std::uint64_t value) {
    char buffer[96]{};
    if (type_is(type, "System.Boolean", "Boolean", "bool"))
        return value ? "true" : "false";
    if (type_is(type, "System.Char", "Char", "char")) {
        const auto character = static_cast<std::uint16_t>(value);
        if (character >= 0x20 && character <= 0x7e) {
            std::snprintf(buffer, sizeof(buffer), "'%c' (U+%04X)", static_cast<char>(character), character);
        } else {
            std::snprintf(buffer, sizeof(buffer), "U+%04X", character);
        }
        return buffer;
    }
    if (type_is(type, "System.Single", "Single", "float")) {
        const float number = std::bit_cast<float>(static_cast<std::uint32_t>(value));
        std::snprintf(buffer, sizeof(buffer), "%.7g", static_cast<double>(number));
        return buffer;
    }
    if (type_is(type, "System.Double", "Double", "double")) {
        const double number = std::bit_cast<double>(value);
        std::snprintf(buffer, sizeof(buffer), "%.15g", number);
        return buffer;
    }
    if (type_is(type, "System.SByte", "SByte", "sbyte"))
        return std::to_string(static_cast<std::int8_t>(value));
    if (type_is(type, "System.Byte", "Byte", "byte"))
        return std::to_string(static_cast<std::uint8_t>(value));
    if (type_is(type, "System.Int16", "Int16", "short"))
        return std::to_string(static_cast<std::int16_t>(value));
    if (type_is(type, "System.UInt16", "UInt16", "ushort"))
        return std::to_string(static_cast<std::uint16_t>(value));
    if (type_is(type, "System.Int32", "Int32", "int"))
        return std::to_string(static_cast<std::int32_t>(value));
    if (type_is(type, "System.UInt32", "UInt32", "uint"))
        return std::to_string(static_cast<std::uint32_t>(value));
    if (type_is(type, "System.Int64", "Int64", "long"))
        return std::to_string(static_cast<std::int64_t>(value));
    if (type_is(type, "System.UInt64", "UInt64", "ulong"))
        return std::to_string(value);
    if (type_is(type, "System.IntPtr", "IntPtr") ||
        type_is(type, "System.UIntPtr", "UIntPtr"))
        return address(static_cast<std::uintptr_t>(value));
    return {};
}

std::string enum_value(std::string_view enum_type, std::string_view underlying_type,
                       std::uint64_t raw) {
    std::string decoded = scalar_value(underlying_type, raw);
    if (decoded.empty())
        decoded = std::to_string(raw);
    return decoded + " (" + std::string(enum_type.empty() ? "enum" : enum_type) + ")";
}

std::string raw_argument_abi(const MethodTracer::Record& record, std::size_t index) {
    std::string out = index < record.arguments.size() ? address(record.arguments[index]) : "<missing>";
    if (index < record.argument_xmm_low.size() &&
        (record.argument_xmm_low[index] != 0 ||
         (index < record.argument_xmm_high.size() && record.argument_xmm_high[index] != 0))) {
        out += " [xmm=" + address(record.argument_xmm_low[index]);
        if (index < record.argument_xmm_high.size())
            out += ":" + address(record.argument_xmm_high[index]);
        out += "]";
    }
    return out;
}

std::uint64_t byref_value(const MethodTracer::Record& record, std::size_t index) {
    if (index >= record.argument_byref_value_bytes.size())
        return 0;
    const std::vector<std::uint8_t>& bytes = record.argument_byref_value_bytes[index];
    std::uint64_t value = 0;
    if (!bytes.empty())
        std::memcpy(&value, bytes.data(), std::min(bytes.size(), sizeof(value)));
    return value;
}

ArgumentView argument_view(const MethodTracer::Snapshot& trace,
                           const MethodTracer::Record& record, std::size_t index) {
    ArgumentView argument{};
    argument.index = index;
    argument.name = index < trace.parameter_names.size() && !trace.parameter_names[index].empty()
                        ? trace.parameter_names[index]
                        : "arg" + std::to_string(index + 1);
    argument.type = index < trace.parameter_types.size() ? trace.parameter_types[index] : "<unknown>";
    argument.raw_value = address(record.arguments[index]);
    argument.raw_abi = raw_argument_abi(record, index);

    const bool opaque = index < trace.parameter_is_opaque.size() && trace.parameter_is_opaque[index];
    const bool reference =
        index < trace.parameter_is_reference.size() && trace.parameter_is_reference[index];
    const bool is_enum = index < trace.parameter_is_enum.size() && trace.parameter_is_enum[index];
    const bool by_ref = index < trace.parameter_is_by_ref.size() && trace.parameter_is_by_ref[index];
    const bool aggregate =
        index < trace.parameter_is_value_type.size() && trace.parameter_is_value_type[index];

    if (by_ref && (index >= record.argument_byref_value_bytes.size() ||
                   record.argument_byref_value_bytes[index].empty())) {
        argument.value = "<by-reference value unavailable; see Raw ABI>";
        return argument;
    }
    const std::uint64_t value = by_ref ? byref_value(record, index) : record.arguments[index];

    if (index < record.argument_displays.size() && !record.argument_displays[index].empty()) {
        argument.value = record.argument_displays[index];
        argument.readable = index < record.argument_readable.size() && record.argument_readable[index];
        argument.inspectable_reference = reference && value != 0;
        return argument;
    }

    if (opaque) {
        argument.value = "<runtime-specific value; see Raw ABI>";
    } else if (reference) {
        argument.inspectable_reference = value != 0;
        argument.value = value == 0 ? "null" : "<reference unavailable>";
    } else if (is_enum) {
        const std::string_view underlying =
            index < trace.parameter_enum_underlying_types.size()
                ? std::string_view(trace.parameter_enum_underlying_types[index])
                : std::string_view{};
        argument.value = enum_value(argument.type, underlying, value);
    } else {
        argument.value = scalar_value(argument.type, value);
        if (argument.value.empty()) {
            argument.value = aggregate ? "<value type; see Raw ABI>"
                                       : "<unsupported scalar; see Raw ABI>";
        }
    }
    argument.readable = !argument.value.empty() && argument.value.find("<") == std::string::npos;
    return argument;
}

void append_csv_value(std::string& out, std::string_view value) {
    out += '"';
    for (const char character : value) {
        if (character == '"')
            out += '"';
        out += character;
    }
    out += '"';
}

void append_json_value(std::string& out, std::string_view value) {
    out += '"';
    for (const unsigned char character : value) {
        switch (character) {
        case '\\': out += "\\\\"; break;
        case '"': out += "\\\""; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (character < 0x20) {
                char escaped[7]{};
                std::snprintf(escaped, sizeof(escaped), "\\u%04X", character);
                out += escaped;
            } else {
                out += static_cast<char>(character);
            }
            break;
        }
    }
    out += '"';
}

std::string seconds_json(double seconds) {
    char text[64]{};
    std::snprintf(text, sizeof(text), "%.9f", seconds);
    return text;
}

bool same_repeat_payload(const MethodTracer::Record& left, const MethodTracer::Record& right) {
    // Do not merge pending returns: identical inputs can still complete
    // differently. Thread and caller are part of the identity as well.
    return left.return_captured && right.return_captured &&
        left.thread_id == right.thread_id &&
        left.caller_address == right.caller_address && left.caller_display == right.caller_display &&
        left.target_address == right.target_address && left.target_display == right.target_display &&
        left.arguments == right.arguments &&
        left.argument_xmm_low == right.argument_xmm_low && left.argument_xmm_high == right.argument_xmm_high &&
        left.argument_byref_value_bytes == right.argument_byref_value_bytes &&
        left.argument_value_bytes == right.argument_value_bytes &&
        left.argument_displays == right.argument_displays &&
        left.argument_readable == right.argument_readable &&
        left.return_rax == right.return_rax && left.return_xmm_low == right.return_xmm_low &&
        left.return_xmm_high == right.return_xmm_high &&
        left.return_buffer_address == right.return_buffer_address &&
        left.return_value_bytes == right.return_value_bytes &&
        left.return_display == right.return_display && left.return_readable == right.return_readable;
}

} // namespace

double elapsed_seconds(const MethodTracer::Snapshot& trace, const MethodTracer::Record& record) {
    if (!trace.timestamp_frequency || record.timestamp_ticks < trace.start_timestamp_ticks)
        return 0.0;
    return static_cast<double>(record.timestamp_ticks - trace.start_timestamp_ticks) /
           static_cast<double>(trace.timestamp_frequency);
}

void collapse_repeated_records(MethodTracer::Snapshot& trace) {
    if (trace.records.empty())
        return;
    std::vector<MethodTracer::Record> grouped;
    grouped.reserve(trace.records.size());
    for (MethodTracer::Record& record : trace.records) {
        if (record.sequence_start == 0)
            record.sequence_start = record.sequence;
        if (record.first_timestamp_ticks == 0)
            record.first_timestamp_ticks = record.timestamp_ticks;
        if (!grouped.empty() && same_repeat_payload(grouped.back(), record)) {
            MethodTracer::Record& previous = grouped.back();
            previous.sequence = record.sequence;
            previous.timestamp_ticks = record.timestamp_ticks;
            previous.repeat_count += record.repeat_count;
            continue;
        }
        grouped.push_back(std::move(record));
    }
    trace.records = std::move(grouped);
}

std::string elapsed_text(double seconds) {
    char text[64]{};
    if (seconds < 0.001)
        std::snprintf(text, sizeof(text), "%.3f us", seconds * 1000000.0);
    else if (seconds < 1.0)
        std::snprintf(text, sizeof(text), "%.3f ms", seconds * 1000.0);
    else
        std::snprintf(text, sizeof(text), "%.6f s", seconds);
    return text;
}

std::string address(std::uintptr_t value) {
    char text[32]{};
    std::snprintf(text, sizeof(text), "0x%llX", static_cast<unsigned long long>(value));
    return text;
}

std::vector<ArgumentView> arguments(const MethodTracer::Snapshot& trace,
                                    const MethodTracer::Record& record) {
    std::vector<ArgumentView> out;
    out.reserve(record.arguments.size());
    for (std::size_t index = 0; index < record.arguments.size(); ++index)
        out.push_back(argument_view(trace, record, index));
    return out;
}

std::string argument_summary(const MethodTracer::Snapshot& trace,
                             const MethodTracer::Record& record) {
    std::string out;
    for (std::size_t index = 0; index < record.arguments.size(); ++index) {
        const ArgumentView argument = argument_view(trace, record, index);
        if (!out.empty())
            out += ", ";
        out += argument.name + " = " + argument.value;
    }
    return out;
}

std::string raw_arguments(const MethodTracer::Snapshot& trace,
                          const MethodTracer::Record& record, bool include_abi_lanes) {
    std::string out;
    for (std::size_t index = 0; index < record.arguments.size(); ++index) {
        const ArgumentView argument = argument_view(trace, record, index);
        if (!out.empty())
            out += ", ";
        out += argument.name + "=" + (include_abi_lanes ? argument.raw_abi : argument.raw_value);
    }
    return out;
}

std::string result(const MethodTracer::Snapshot& trace, const MethodTracer::Record& record) {
    if (type_is(trace.return_type, "System.Void", "Void", "void"))
        return "void";
    if (!record.return_captured)
        return "<pending return>";
    if (!record.return_display.empty())
        return record.return_display;
    if (trace.return_is_opaque)
        return "<runtime-specific return; see Raw ABI>";
    if (trace.return_is_enum) {
        return enum_value(trace.return_type, trace.return_enum_underlying_type,
                          trace.return_is_floating ? record.return_xmm_low : record.return_rax);
    }
    if (trace.return_uses_indirect_abi)
        return "<value type captured; decoding unavailable>";
    const std::uint64_t raw = trace.return_is_floating ? record.return_xmm_low : record.return_rax;
    if (trace.return_is_reference)
        return raw == 0 ? "null" : "<reference unavailable>";
    const std::string decoded = scalar_value(trace.return_type, raw);
    return decoded.empty() ? "<unsupported scalar; see Raw ABI>" : decoded;
}

std::string raw_result(const MethodTracer::Snapshot& trace, const MethodTracer::Record& record) {
    if (!record.return_captured)
        return "<pending>";
    std::string text = "rax=" + address(record.return_rax);
    if (trace.return_is_floating)
        text += " xmm0=" + address(record.return_xmm_low) + ":" + address(record.return_xmm_high);
    if (trace.return_uses_indirect_abi)
        text += " return-buffer=" + address(record.return_buffer_address);
    return text;
}

std::string csv(const MethodTracer::Snapshot& trace) {
    std::string out =
        "sequence_start,sequence,repeat_count,seconds,thread,caller,caller_address,target,target_address,arguments,result,raw_arguments,raw_abi,raw_result\n";
    for (const MethodTracer::Record& record : trace.records) {
        const std::uint64_t sequence_start = record.sequence_start == 0 ? record.sequence : record.sequence_start;
        const std::uint64_t repeat_count = std::max<std::uint64_t>(1, record.repeat_count);
        out += std::to_string(sequence_start) + "," + std::to_string(record.sequence) + "," +
               std::to_string(repeat_count) + "," + seconds_json(elapsed_seconds(trace, record)) + "," +
               std::to_string(record.thread_id) + ",";
        append_csv_value(out, record.caller_display);
        out += ",";
        append_csv_value(out, address(record.caller_address));
        out += ",";
        append_csv_value(out, record.target_display);
        out += ",";
        append_csv_value(out, address(record.target_address));
        out += ",";
        append_csv_value(out, argument_summary(trace, record));
        out += ",";
        append_csv_value(out, result(trace, record));
        out += ",";
        append_csv_value(out, raw_arguments(trace, record, false));
        out += ",";
        append_csv_value(out, raw_arguments(trace, record, true));
        out += ",";
        append_csv_value(out, raw_result(trace, record));
        out += "\n";
    }
    return out;
}

std::string json(const MethodTracer::Snapshot& trace) {
    std::string out = "{\n  \"schemaVersion\": 2,\n  \"method\": ";
    append_json_value(out, trace.declaring_type + "." + trace.method_name);
    out += ",\n  \"signature\": {\n    \"returnType\": ";
    append_json_value(out, trace.return_type);
    out += ",\n    \"parameters\": [";
    for (std::size_t index = 0; index < trace.parameter_types.size(); ++index) {
        if (index != 0)
            out += ", ";
        out += "{\"name\": ";
        append_json_value(out, index < trace.parameter_names.size() ? trace.parameter_names[index]
                                                                    : "arg" + std::to_string(index + 1));
        out += ", \"type\": ";
        append_json_value(out, trace.parameter_types[index]);
        out += "}";
    }
    out += "]\n  },\n  \"totalCalls\": " + std::to_string(trace.total_calls);
    out += ",\n  \"overwrittenRecords\": " + std::to_string(trace.overwritten_records);
    out += ",\n  \"captureFaults\": " + std::to_string(trace.native_faults) + ",\n  \"records\": [\n";
    for (std::size_t record_index = 0; record_index < trace.records.size(); ++record_index) {
        const MethodTracer::Record& record = trace.records[record_index];
        const std::uint64_t sequence_start = record.sequence_start == 0 ? record.sequence : record.sequence_start;
        const std::uint64_t repeat_count = std::max<std::uint64_t>(1, record.repeat_count);
        out += "    {\n      \"sequenceStart\": " + std::to_string(sequence_start);
        out += ",\n      \"sequence\": " + std::to_string(record.sequence);
        out += ",\n      \"repeatCount\": " + std::to_string(repeat_count);
        out += ",\n      \"elapsedSeconds\": " + seconds_json(elapsed_seconds(trace, record));
        out += ",\n      \"threadId\": " + std::to_string(record.thread_id);
        out += ",\n      \"caller\": {\"display\": ";
        append_json_value(out, record.caller_display);
        out += ", \"address\": ";
        append_json_value(out, address(record.caller_address));
        out += "},\n      \"target\": {\"display\": ";
        append_json_value(out, record.target_display);
        out += ", \"address\": ";
        append_json_value(out, address(record.target_address));
        out += "},\n      \"arguments\": [";
        const std::vector<ArgumentView> views = arguments(trace, record);
        for (std::size_t argument_index = 0; argument_index < views.size(); ++argument_index) {
            const ArgumentView& argument = views[argument_index];
            if (argument_index != 0)
                out += ",";
            out += "\n        {\"name\": ";
            append_json_value(out, argument.name);
            out += ", \"type\": ";
            append_json_value(out, argument.type);
            out += ", \"value\": ";
            append_json_value(out, argument.value);
            out += ", \"raw\": ";
            append_json_value(out, argument.raw_value);
            out += ", \"rawAbi\": ";
            append_json_value(out, argument.raw_abi);
            out += "}";
        }
        if (!views.empty())
            out += "\n      ";
        out += "],\n      \"result\": {\"type\": ";
        append_json_value(out, trace.return_type);
        out += ", \"value\": ";
        append_json_value(out, result(trace, record));
        out += ", \"rawAbi\": ";
        append_json_value(out, raw_result(trace, record));
        out += "}\n    }";
        out += record_index + 1 == trace.records.size() ? "\n" : ",\n";
    }
    return out + "  ]\n}";
}

} // namespace Explorer::MethodTraceFormat

// Copyright (c) 2026 Jadis0x. All rights reserved.
#include "mod/explorer/method_trace_format.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

void require(bool condition, const char* message) {
    if (condition)
        return;
    std::cerr << "FAILED: " << message << '\n';
    std::exit(1);
}

Explorer::MethodTracer::Snapshot base_trace() {
    Explorer::MethodTracer::Snapshot trace{};
    trace.declaring_type = "Example.Component";
    trace.method_name = "GetObject";
    trace.return_type = "System.Object";
    trace.return_is_reference = true;
    trace.timestamp_frequency = 10'000'000;
    trace.start_timestamp_ticks = 1'000'000;
    trace.parameter_names = {"key"};
    trace.parameter_types = {"System.Int32"};
    trace.parameter_is_reference = {false};
    trace.parameter_is_value_type = {true};
    trace.parameter_is_enum = {false};
    trace.parameter_enum_underlying_types = {""};
    trace.parameter_is_opaque = {false};
    trace.parameter_is_floating = {false};
    return trace;
}

Explorer::MethodTracer::Record base_record() {
    Explorer::MethodTracer::Record record{};
    record.sequence = 7;
    record.timestamp_ticks = 1'025'000;
    record.thread_id = 42;
    record.caller_display = "GameAssembly.dll+0x1234";
    record.caller_address = 0x1234;
    record.target_display = "Example.Component";
    record.target_address = 0x2000;
    record.arguments = {2};
    record.argument_xmm_low = {0};
    record.argument_xmm_high = {0};
    record.argument_displays = {""};
    record.return_captured = true;
    record.return_rax = 0x3000;
    record.return_display = "Example.RuntimeObject (declared System.Object)";
    return record;
}

} // namespace

int main() {
    using namespace Explorer;

    MethodTracer::Snapshot trace = base_trace();
    MethodTracer::Record record = base_record();

    const auto primitive = MethodTraceFormat::arguments(trace, record);
    require(primitive.size() == 1, "primitive argument count");
    require(primitive[0].value == "2", "System.Int32 must be decoded instead of shown as value ABI");
    require(MethodTraceFormat::argument_summary(trace, record) == "key = 2", "readable argument summary");
    require(MethodTraceFormat::raw_arguments(trace, record, false) == "key=0x2",
            "raw arguments remain available");
    require(MethodTraceFormat::elapsed_text(MethodTraceFormat::elapsed_seconds(trace, record)) == "2.500 ms",
            "high-resolution elapsed time");
    require(MethodTraceFormat::result(trace, record) ==
                "Example.RuntimeObject (declared System.Object)",
            "resolved reference result");

    trace.parameter_types[0] = "Example.SerializationFormat";
    trace.parameter_is_value_type[0] = false;
    trace.parameter_is_enum[0] = true;
    trace.parameter_enum_underlying_types[0] = "System.Int32";
    const auto enumeration = MethodTraceFormat::arguments(trace, record);
    require(enumeration[0].value == "2 (Example.SerializationFormat)", "enum underlying value");

    trace.parameter_is_by_ref = {true};
    record.arguments[0] = 0x12345678;
    record.argument_byref_value_bytes = {{3, 0, 0, 0}};
    const auto byref_enumeration = MethodTraceFormat::arguments(trace, record);
    require(byref_enumeration[0].value == "3 (Example.SerializationFormat)",
            "by-reference enum must use the post-call value, not its stack address");
    trace.parameter_is_by_ref = {false};
    record.argument_byref_value_bytes.clear();

    trace.parameter_types[0] = "Example.LargeStruct";
    trace.parameter_is_value_type[0] = true;
    trace.parameter_is_enum[0] = false;
    const auto aggregate = MethodTraceFormat::arguments(trace, record);
    require(aggregate[0].value == "<value type; see Raw ABI>", "aggregate value must not masquerade as a pointer");
    require(aggregate[0].value.find("0x") == std::string::npos, "raw ABI must stay out of friendly values");

    record.argument_displays = {"Example.LargeStruct {state=3, flags=Ready}"};
    record.argument_readable = {true};
    const auto decoded_aggregate = MethodTraceFormat::arguments(trace, record);
    require(decoded_aggregate[0].value == "Example.LargeStruct {state=3, flags=Ready}",
            "runtime-decoded value types must take precedence over the ABI placeholder");
    require(decoded_aggregate[0].readable, "runtime-decoded values are marked for readable trace styling");

    trace = base_trace();
    record = base_record();
    trace.records = {record};
    trace.total_calls = trace.records.size();
    const std::string json = MethodTraceFormat::json(trace);
    require(json.find("\"schemaVersion\": 2") != std::string::npos, "structured JSON schema version");
    require(json.find("\"arguments\": [") != std::string::npos, "arguments must be a JSON array");
    require(json.find("\"name\": \"key\"") != std::string::npos, "argument name in JSON");
    require(json.find("\"value\": \"2\"") != std::string::npos, "decoded argument value in JSON");
    require(json.find("\"result\": {\"type\": \"System.Object\"") != std::string::npos,
            "structured result object");

    std::cout << "method trace formatting contract passed\n";
    return 0;
}

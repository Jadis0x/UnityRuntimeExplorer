// Copyright (c) 2026 Jadis0x. All rights reserved.
#pragma once

#include "sdk/unity/unity_inspect.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Explorer::MethodTracer {

// Match the reflection limit so every valid inspected method can be traced.
constexpr std::size_t max_parameters = URK::Unity::Inspect::kMaxMethodParameters;
constexpr std::size_t max_records = 1024;
constexpr std::size_t max_sessions = 12;
using TraceId = std::uint64_t;

struct Record {
    std::uint64_t sequence = 0;
    std::uint64_t timestamp_ticks = 0;
    std::uint32_t thread_id = 0;
    std::uintptr_t caller_address = 0;
    std::uintptr_t target_address = 0;
    std::uint64_t return_rax = 0;
    std::uint64_t return_xmm_low = 0;
    std::uint64_t return_xmm_high = 0;
    bool return_captured = false;
    // Resolved on the Explorer thread; the detour never follows managed pointers.
    std::string return_display;
    std::vector<std::uint64_t> arguments;
    // Retain XMM lanes for floating-point and value-type arguments.
    std::vector<std::uint64_t> argument_xmm_low;
    std::vector<std::uint64_t> argument_xmm_high;
    // Filled on the Explorer thread; the detour does not resolve managed objects.
    std::string caller_display;
    std::string target_display;
    std::vector<std::string> argument_displays;
};

struct Snapshot {
    TraceId id = 0;
    std::string method_pointer_text;
    bool active = false;
    bool is_static = false;
    std::string method_name;
    std::string declaring_type;
    std::string return_type;
    std::vector<std::string> parameter_names;
    std::vector<std::string> parameter_types;
    std::vector<bool> parameter_is_reference;
    std::vector<bool> parameter_is_value_type;
    std::vector<bool> parameter_is_opaque;
    std::vector<bool> parameter_is_floating;
    bool target_is_reference = false;
    bool return_is_reference = false;
    bool return_is_value_type = false;
    bool return_is_opaque = false;
    // Struct returns can use a hidden Win64 output buffer; RAX is not the result.
    bool return_uses_indirect_abi = false;
    bool return_is_floating = false;
    std::uint64_t total_calls = 0;
    std::uint64_t overwritten_records = 0;
    std::uint64_t native_faults = 0;
    std::uint64_t start_timestamp_ticks = 0;
    std::uint64_t timestamp_frequency = 0;
    std::vector<Record> records;
    std::string error;
};

// A native detour captures game calls as well as Explorer invocations.
bool start(const URK::Unity::Inspect::MethodInfo &method, std::string &error);
bool stop(const URK::il2cpp::Method *method);
bool stop(TraceId id);
bool clear(TraceId id);
bool close(TraceId id);
void stop_all();
void shutdown();
bool any_active();
std::vector<Snapshot> snapshots();

} // namespace Explorer::MethodTracer

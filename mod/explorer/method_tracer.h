// Copyright (c) 2026 Jadis0x. All rights reserved.
#pragma once

#include "sdk/unity/unity_inspect.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Explorer::MethodTracer {

constexpr std::size_t max_parameters = 8;
constexpr std::size_t max_records = 256;
constexpr std::size_t max_sessions = 12;
using TraceId = std::uint64_t;

struct Record {
    std::uint64_t sequence = 0;
    std::uint64_t timestamp_ticks = 0;
    std::uint32_t thread_id = 0;
    std::uintptr_t caller_address = 0;
    std::uintptr_t target_address = 0;
    std::vector<std::uint64_t> arguments;
    // Filled by the Explorer main-thread model. The detour itself never
    // dereferences managed objects or resolves modules.
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
    std::vector<std::string> parameter_names;
    std::vector<std::string> parameter_types;
    std::vector<bool> parameter_is_reference;
    bool target_is_reference = false;
    std::uint64_t total_calls = 0;
    std::uint64_t start_timestamp_ticks = 0;
    std::uint64_t timestamp_frequency = 0;
    std::vector<Record> records;
    std::string error;
};

// Only one method is traced at a time. A native detour is deliberately used so
// calls made by the game (rather than only Explorer's reflected invocations)
// are included.
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

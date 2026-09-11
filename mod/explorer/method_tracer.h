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
// Copies of one body a trace will hook. A method inlined into more places than
// this is traced at the ones the resolver is most confident about.
constexpr std::size_t max_inline_sites = 8;
using TraceId = std::uint64_t;

// A decoded value plus, recursively, the fields/elements inside it.
struct ValueNode {
    // Empty on the root; a field, element or facet name on a child.
    std::string name;
    std::string type;
    std::string display;
    bool readable = false;
    // Non-zero for an inspectable managed reference; zero for value types.
    std::uint64_t inspect_address = 0;
    std::vector<ValueNode> children;
    // Set when the decoder stopped early (too many members to walk).
    bool truncated = false;
};

// Metadata identity of the managed method containing a captured call site.
// The detour records only a return address; RuntimeModel fills this from the
// caller index while publishing a snapshot. Keeping the identity alongside
// the display text lets the UI navigate without parsing generated method or
// nested type names out of a formatted string.
struct CallerMethod {
    std::uintptr_t class_address = 0;
    std::string image;
    std::string namespc;
    std::string class_name;
    std::string method_name;

    bool inspectable() const {
        return class_address != 0 && !class_name.empty() && !method_name.empty();
    }
};

struct Record {
    std::uint64_t sequence = 0;
    std::uint64_t sequence_start = 0;
    std::uint64_t timestamp_ticks = 0;
    std::uint64_t first_timestamp_ticks = 0;
    std::uint64_t repeat_count = 1;
    std::uint32_t thread_id = 0;
    std::uintptr_t caller_address = 0;
    std::uintptr_t target_address = 0;
    // Set when the call was seen inside an inlined copy of the body instead of
    // at the method's own entry point.
    std::uintptr_t inline_site_address = 0;
    std::uint64_t return_rax = 0;
    std::uint64_t return_xmm_low = 0;
    std::uint64_t return_xmm_high = 0;
    std::uintptr_t return_buffer_address = 0;
    bool return_captured = false;
    std::uint64_t return_reference_token = 0;
    std::vector<std::uint8_t> return_value_bytes;
    // Resolved on the Explorer thread; the detour never touches managed memory.
    std::string return_display;
    std::vector<std::uint64_t> arguments;
    std::vector<std::uint64_t> argument_xmm_low;
    std::vector<std::uint64_t> argument_xmm_high;
    // Value copied after the callee returns, for ref/out parameters.
    std::vector<std::vector<std::uint8_t>> argument_byref_value_bytes;
    // Value-type arguments copied at entry, before the callee can mutate them.
    std::vector<std::vector<std::uint8_t>> argument_value_bytes;
    std::string caller_display;
    CallerMethod caller_method;
    std::string target_display;
    std::vector<std::string> argument_displays;
    std::vector<bool> argument_readable;
    bool return_readable = false;
    // One node per argument plus the result; filled in when decoded.
    std::vector<ValueNode> argument_nodes;
    ValueNode return_node;
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
    std::vector<const void*> parameter_type_handles;
    std::vector<const void*> parameter_value_classes;
    std::vector<std::size_t> parameter_value_sizes;
    std::vector<bool> parameter_is_reference;
    std::vector<bool> parameter_is_value_type;
    std::vector<bool> parameter_is_enum;
    std::vector<bool> parameter_is_by_ref;
    std::vector<std::string> parameter_enum_underlying_types;
    std::vector<bool> parameter_is_opaque;
    std::vector<bool> parameter_is_floating;
    bool target_is_reference = false;
    bool return_is_reference = false;
    bool return_is_value_type = false;
    bool return_is_enum = false;
    std::string return_enum_underlying_type;
    const void* return_type_handle = nullptr;
    bool return_is_opaque = false;
    const void* return_value_class = nullptr;
    std::size_t return_value_size = 0;
    // Set when a struct return uses a hidden Win64 output buffer, not RAX.
    bool return_uses_indirect_abi = false;
    bool return_is_floating = false;
    // False for a mid-function entry hook, which cannot see the return value.
    bool captures_return = false;
    std::uint64_t total_calls = 0;
    // Result of scanning the owning module for references to the hooked entry
    // point. An IL2CPP method whose body was inlined into its callers keeps a
    // valid entry point that nothing branches to; direct_call_sites == 0 is
    // what that looks like, and the copies below are where its calls are
    // actually recorded.
    bool call_site_scan_done = false;
    std::size_t direct_call_sites = 0;
    std::size_t address_references = 0;
    // Where the compiler put copies of this body, and whether each copy is
    // hooked. A trace with copies records the game's own calls through them.
    struct InlineSite {
        std::uintptr_t address = 0;
        std::uintptr_t function_start = 0;
        std::size_t matched_instructions = 0;
        bool hooked = false;
        bool instance_captured = false;
        // True when the hook sits on the instruction that calls the method
        // rather than on a copy of its body.
        bool call_site = false;
        // Enclosing method and offset, resolved for display.
        std::string display;
    };
    std::vector<InlineSite> inline_sites;
    std::uint64_t overwritten_records = 0;
    std::uint64_t native_faults = 0;
    std::uint64_t start_timestamp_ticks = 0;
    std::uint64_t timestamp_frequency = 0;
    std::vector<Record> records;
    std::string error;
};

// capture_return: false hooks the entry only (no return value); true rewrites
// the return address to capture it too, but can corrupt unwinding if the
// callee throws, so it's opt-in. instance_filter restricts to one `this`
// (used by property watches, which pass user_visible = false to stay hidden
// from the Traces panel).
bool start(const URK::Unity::Inspect::MethodInfo &method, bool capture_return, const void *instance_filter,
           bool user_visible, std::string &error);

// What a watch needs from a setter trace each frame, without copying records.
struct WriteSignal {
    bool active = false;
    std::uint64_t total_calls = 0;
    std::uintptr_t last_caller = 0;
    std::uint32_t last_thread_id = 0;
    // The setter's own argument, not whatever a later poll observes.
    bool has_written_value = false;
    std::uint64_t written_raw = 0;
    std::uint64_t written_xmm_low = 0;
};
WriteSignal write_signal(TraceId id);
// Id of the trace most recently created by start().
TraceId last_started_id();
bool stop(const URK::managed::Method *method);
bool stop(TraceId id);
bool clear(TraceId id);
// Upgrades a running trace to also capture returns, keeping its identity;
// existing records are dropped since they have no return to show.
bool capture_returns(TraceId id, std::string &error);
bool close(TraceId id);
void stop_all();
void shutdown();
bool any_active();
std::vector<Snapshot> snapshots();

} // namespace Explorer::MethodTracer

// Copyright (c) 2026 Jadis0x. All rights reserved.
#include "method_tracer.h"

#include "sdk/hook_api.h"
#include "sdk/il2cpp/il2cpp_helpers.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <new>

namespace Explorer::MethodTracer {
namespace {
enum class ArgumentKind : std::uint8_t { Integer, Floating, Aggregate };

struct TraceRegisterFrame {
    std::array<std::array<std::uint8_t, 16>, 6> xmm;
    std::uint64_t r11, r10, r9, r8, rdx, rcx, rax, return_address;
    std::uint64_t stack_arguments[max_parameters];
};
static_assert(offsetof(TraceRegisterFrame, r11) == 96);
static_assert(offsetof(TraceRegisterFrame, return_address) == 152);
static_assert(offsetof(TraceRegisterFrame, stack_arguments) == 160);

struct RingRecord {
    std::atomic<std::uint64_t> published_sequence{0}, timestamp_ticks{0};
    std::atomic<std::uint32_t> thread_id{0};
    std::atomic<std::uintptr_t> caller_address{0}, target_address{0};
    std::atomic<std::uint64_t> return_rax{0}, return_xmm_low{0}, return_xmm_high{0};
    std::atomic<bool> return_published{false};
};

struct HookSession {
    TraceId id = 0;
    const URK::il2cpp::Method *method = nullptr;
    void *original = nullptr;
    void *stub = nullptr;
    bool visible = true;
    std::atomic<bool> active{false};
    std::atomic<std::uint32_t> in_flight{0};
    std::atomic<std::uint64_t> write_sequence{0};
    std::atomic<std::uint64_t> native_faults{0};
    std::array<RingRecord, max_records> records{};
    std::unique_ptr<std::atomic<std::uint64_t>[]> arguments;
    std::unique_ptr<std::atomic<std::uint64_t>[]> argument_xmm_low;
    std::unique_ptr<std::atomic<std::uint64_t>[]> argument_xmm_high;
    std::vector<ArgumentKind> argument_kinds;
    std::vector<bool> argument_is_reference;
    std::vector<bool> argument_is_value_type;
    std::vector<bool> argument_is_opaque;
    std::size_t argument_count = 0;
    bool is_static = false;
    bool target_is_reference = false;
    bool return_is_reference = false;
    bool return_is_value_type = false;
    bool return_is_opaque = false;
    bool return_uses_indirect_abi = false;
    bool return_is_floating = false;
    std::uint64_t start_timestamp_ticks = 0;
    std::string method_pointer_text, method_name, declaring_type, return_type;
    std::vector<std::string> parameter_names, parameter_types;
};

// The entry stub swaps in a post-call return address. TLS avoids allocations
// on game threads and supports recursive calls.
struct ReturnContext {
    HookSession* session = nullptr;
    std::uint64_t sequence = 0;
    std::uintptr_t original_return = 0;
};
constexpr std::size_t max_return_depth = 64;
thread_local std::array<ReturnContext, max_return_depth> g_return_contexts{};
thread_local std::size_t g_return_context_depth = 0;

struct State {
    std::mutex control_mutex;
    std::vector<std::unique_ptr<HookSession>> sessions;
    TraceId next_id = 1;
    LARGE_INTEGER frequency{};
    std::string diagnostic;
};
State g_state;

bool is_floating(std::string_view type) {
    return type == "System.Single" || type == "Single" || type == "float" || type == "System.Double" ||
           type == "Double" || type == "double";
}

bool is_register_scalar(std::string_view type) {
    return type == "System.Boolean" || type == "Boolean" || type == "bool" ||
           type == "System.Char" || type == "Char" || type == "char" ||
           type == "System.SByte" || type == "SByte" || type == "std::int8_t" ||
           type == "System.Byte" || type == "Byte" || type == "std::uint8_t" ||
           type == "System.Int16" || type == "Int16" || type == "short" ||
           type == "System.UInt16" || type == "UInt16" || type == "unsigned short" ||
           type == "System.Int32" || type == "Int32" || type == "int" ||
           type == "System.UInt32" || type == "UInt32" || type == "unsigned int" ||
           type == "System.Int64" || type == "Int64" || type == "long" ||
           type == "System.UInt64" || type == "UInt64" || type == "unsigned long" ||
           type == "System.IntPtr" || type == "IntPtr" || type == "System.UIntPtr" || type == "UIntPtr" ||
           is_floating(type);
}

std::uint64_t register_value(const TraceRegisterFrame *frame, std::size_t slot, ArgumentKind kind) {
    if (slot >= 4)
        return frame->stack_arguments[slot - 4];
    if (kind == ArgumentKind::Floating) {
        std::uint64_t value = 0;
        std::memcpy(&value, frame->xmm[slot].data(), sizeof(value));
        return value;
    }
    return slot == 0 ? frame->rcx : slot == 1 ? frame->rdx : slot == 2 ? frame->r8 : frame->r9;
}

std::uint64_t xmm_lane_value(const TraceRegisterFrame *frame, std::size_t slot, std::size_t offset) {
    if (slot >= 4 || offset > 8)
        return 0;
    std::uint64_t value = 0;
    std::memcpy(&value, frame->xmm[slot].data() + offset, sizeof(value));
    return value;
}

std::size_t argument_slot(const HookSession &session, std::uint64_t sequence, std::size_t argument_index) {
    return static_cast<std::size_t>(sequence % max_records) * session.argument_count + argument_index;
}

#if defined(_WIN32)
int trace_exception_filter(unsigned long code) {
    return code == 0xC0000005ul || code == 0xC0000006ul ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH;
}
#endif

std::uintptr_t push_return_context(HookSession* session, std::uint64_t sequence, std::uintptr_t original_return) {
    if (!session || !original_return || g_return_context_depth >= max_return_depth)
        return 0;
    g_return_contexts[g_return_context_depth++] = {session, sequence, original_return};
    return original_return;
}

extern "C" std::uintptr_t trace_record_from_stub(const TraceRegisterFrame *frame, HookSession *session) {
    if (!frame || !session)
        return 0;
    session->in_flight.fetch_add(1, std::memory_order_acq_rel);
    if (!session->active.load(std::memory_order_acquire)) {
        session->in_flight.fetch_sub(1, std::memory_order_release);
        return 0;
    }
    std::uintptr_t original_return = 0;
#if defined(_WIN32)
    __try {
#endif
    const std::uint64_t sequence = session->write_sequence.fetch_add(1, std::memory_order_relaxed);
    RingRecord &record = session->records[sequence % max_records];
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    record.timestamp_ticks.store(static_cast<std::uint64_t>(now.QuadPart), std::memory_order_relaxed);
    record.thread_id.store(GetCurrentThreadId(), std::memory_order_relaxed);
    record.caller_address.store(static_cast<std::uintptr_t>(frame->return_address), std::memory_order_relaxed);
    record.target_address.store(session->is_static ? 0 : static_cast<std::uintptr_t>(frame->rcx), std::memory_order_relaxed);
    for (std::size_t index = 0; index < session->argument_count; ++index) {
        const std::size_t slot = index + (session->is_static ? 0 : 1);
        const std::size_t storage = argument_slot(*session, sequence, index);
        session->arguments[storage].store(register_value(frame, slot, session->argument_kinds[index]),
                                          std::memory_order_relaxed);
        session->argument_xmm_low[storage].store(xmm_lane_value(frame, slot, 0), std::memory_order_relaxed);
        session->argument_xmm_high[storage].store(xmm_lane_value(frame, slot, 8), std::memory_order_relaxed);
    }
    record.return_published.store(false, std::memory_order_relaxed);
    record.published_sequence.store(sequence + 1, std::memory_order_release);
    original_return = push_return_context(session, sequence, static_cast<std::uintptr_t>(frame->return_address));
#if defined(_WIN32)
    }
    __except (trace_exception_filter(GetExceptionCode())) {
        session->native_faults.fetch_add(1, std::memory_order_relaxed);
    }
#endif
    if (!original_return)
        session->in_flight.fetch_sub(1, std::memory_order_release);
    return original_return;
}

extern "C" std::uintptr_t trace_record_return_from_stub(HookSession* session, std::uint64_t rax,
                                                           std::uint64_t xmm_low, std::uint64_t xmm_high) {
    if (!session || g_return_context_depth == 0)
        return 0;
    const ReturnContext context = g_return_contexts[--g_return_context_depth];
    if (context.session != session || !context.original_return)
        return 0;
#if defined(_WIN32)
    __try {
#endif
        RingRecord& record = session->records[context.sequence % max_records];
        if (record.published_sequence.load(std::memory_order_acquire) == context.sequence + 1) {
            record.return_rax.store(rax, std::memory_order_relaxed);
            record.return_xmm_low.store(xmm_low, std::memory_order_relaxed);
            record.return_xmm_high.store(xmm_high, std::memory_order_relaxed);
            record.return_published.store(true, std::memory_order_release);
        }
#if defined(_WIN32)
    }
    __except (trace_exception_filter(GetExceptionCode())) {
        session->native_faults.fetch_add(1, std::memory_order_relaxed);
    }
#endif
    session->in_flight.fetch_sub(1, std::memory_order_release);
    return context.original_return;
}

void emit(std::vector<std::uint8_t> &code, std::initializer_list<std::uint8_t> bytes) {
    code.insert(code.end(), bytes.begin(), bytes.end());
}
void emit_u64(std::vector<std::uint8_t> &code, std::uintptr_t value) {
    for (int byte = 0; byte < 8; ++byte) code.push_back(static_cast<std::uint8_t>(value >> (byte * 8)));
}

bool create_stub(HookSession &session, std::string &error) {
    std::vector<std::uint8_t> code;
    emit(code, {0x50, 0x51, 0x52, 0x41, 0x50, 0x41, 0x51, 0x41, 0x52, 0x41, 0x53, 0x48, 0x83, 0xEC, 0x60});
    for (std::uint8_t index = 0; index < 6; ++index)
        emit(code, {0xF3, 0x0F, 0x7F, static_cast<std::uint8_t>(0x44 + (index << 3)), 0x24, static_cast<std::uint8_t>(index * 16)});
    emit(code, {0x48, 0x89, 0xE1, 0x48, 0xBA});
    emit_u64(code, reinterpret_cast<std::uintptr_t>(&session));
    emit(code, {0x48, 0x83, 0xEC, 0x20, 0x48, 0xB8});
    emit_u64(code, reinterpret_cast<std::uintptr_t>(&trace_record_from_stub));
    emit(code, {0xFF, 0xD0, 0x48, 0x83, 0xC4, 0x20});
    // Save the original return address from RAX in the preserved R11 slot.
    emit(code, {0x48, 0x89, 0x44, 0x24, 0x60});
    for (std::uint8_t index = 0; index < 6; ++index)
        emit(code, {0xF3, 0x0F, 0x6F, static_cast<std::uint8_t>(0x44 + (index << 3)), 0x24, static_cast<std::uint8_t>(index * 16)});
    emit(code, {0x48, 0x83, 0xC4, 0x60, 0x41, 0x5B, 0x41, 0x5A, 0x41, 0x59, 0x41, 0x58, 0x5A, 0x59, 0x58});
    // test r11, r11; jump to the original tail path when it is null
    emit(code, {0x4D, 0x85, 0xDB, 0x74, 0x0E, 0x48, 0xB8});
    const std::size_t post_address_patch = code.size();
    emit_u64(code, 0);
    emit(code, {0x48, 0x89, 0x04, 0x24, 0x49, 0xBA});
    emit_u64(code, reinterpret_cast<std::uintptr_t>(&session.original));
    emit(code, {0x41, 0xFF, 0x22});

    const std::size_t post_stub_offset = code.size();
    // Preserve RAX/XMM0, record the return lanes, then continue to the caller.
    emit(code, {0x50, 0x48, 0x83, 0xEC, 0x38, 0x66, 0x0F, 0x7F, 0x44, 0x24, 0x20});
    emit(code, {0x48, 0xB9});
    emit_u64(code, reinterpret_cast<std::uintptr_t>(&session));
    emit(code, {0x48, 0x8B, 0x54, 0x24, 0x38, 0x4C, 0x8B, 0x44, 0x24, 0x20,
                0x4C, 0x8B, 0x4C, 0x24, 0x28, 0x48, 0xB8});
    emit_u64(code, reinterpret_cast<std::uintptr_t>(&trace_record_return_from_stub));
    emit(code, {0xFF, 0xD0, 0x49, 0x89, 0xC3, 0x66, 0x0F, 0x6F, 0x44, 0x24, 0x20,
                0x48, 0x83, 0xC4, 0x38, 0x58, 0x41, 0xFF, 0xE3});
    void *memory = VirtualAlloc(nullptr, code.size(), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!memory) { error = "Method tracer could not allocate its native detour stub"; return false; }
    const std::uintptr_t post_address = reinterpret_cast<std::uintptr_t>(memory) + post_stub_offset;
    std::memcpy(code.data() + post_address_patch, &post_address, sizeof(post_address));
    std::memcpy(memory, code.data(), code.size());
    DWORD old_protect = 0;
    if (!VirtualProtect(memory, code.size(), PAGE_EXECUTE_READ, &old_protect)) {
        VirtualFree(memory, 0, MEM_RELEASE); error = "Method tracer could not make its native detour stub executable"; return false;
    }
    FlushInstructionCache(GetCurrentProcess(), memory, code.size());
    session.stub = memory;
    return true;
}

void tracer_diagnostic(const char *message) { if (message && message[0]) g_state.diagnostic = message; }

void deactivate(HookSession &session) {
    session.active.store(false, std::memory_order_release);
    while (session.in_flight.load(std::memory_order_acquire) != 0) SwitchToThread();
    if (session.original && session.stub) URK::hooks::detach_ex(&session.original, session.stub);
}

void reset_records(HookSession &session) {
    for (RingRecord &record : session.records) {
        record.published_sequence.store(0, std::memory_order_relaxed);
        record.return_published.store(false, std::memory_order_relaxed);
    }
    session.write_sequence.store(0, std::memory_order_relaxed);
    session.native_faults.store(0, std::memory_order_relaxed);
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    session.start_timestamp_ticks = static_cast<std::uint64_t>(now.QuadPart);
}

Snapshot copy_snapshot(const HookSession &session) {
    Snapshot out{};
    out.id = session.id;
    out.method_pointer_text = session.method_pointer_text;
    out.active = session.active.load(std::memory_order_acquire);
    out.is_static = session.is_static;
    out.method_name = session.method_name;
    out.declaring_type = session.declaring_type;
    out.return_type = session.return_type;
    out.parameter_names = session.parameter_names;
    out.parameter_types = session.parameter_types;
    out.parameter_is_reference = session.argument_is_reference;
    out.parameter_is_value_type = session.argument_is_value_type;
    out.parameter_is_opaque = session.argument_is_opaque;
    out.parameter_is_floating.reserve(session.argument_kinds.size());
    for (const ArgumentKind kind : session.argument_kinds)
        out.parameter_is_floating.push_back(kind == ArgumentKind::Floating);
    out.target_is_reference = session.target_is_reference;
    out.return_is_reference = session.return_is_reference;
    out.return_is_value_type = session.return_is_value_type;
    out.return_is_opaque = session.return_is_opaque;
    out.return_uses_indirect_abi = session.return_uses_indirect_abi;
    out.return_is_floating = session.return_is_floating;
    out.timestamp_frequency = static_cast<std::uint64_t>(g_state.frequency.QuadPart);
    out.start_timestamp_ticks = session.start_timestamp_ticks;
    const std::uint64_t total = session.write_sequence.load(std::memory_order_acquire);
    out.total_calls = total;
    out.overwritten_records = total > max_records ? total - max_records : 0;
    out.native_faults = session.native_faults.load(std::memory_order_relaxed);
    const std::uint64_t begin = total > max_records ? total - max_records : 0;
    out.records.reserve(static_cast<std::size_t>(total - begin));
    for (std::uint64_t sequence = begin; sequence < total; ++sequence) {
        const RingRecord &source = session.records[sequence % max_records];
        if (source.published_sequence.load(std::memory_order_acquire) != sequence + 1) continue;
        Record record{};
        record.sequence = sequence + 1;
        record.timestamp_ticks = source.timestamp_ticks.load(std::memory_order_relaxed);
        record.thread_id = source.thread_id.load(std::memory_order_relaxed);
        record.caller_address = source.caller_address.load(std::memory_order_relaxed);
        record.target_address = source.target_address.load(std::memory_order_relaxed);
        record.return_captured = source.return_published.load(std::memory_order_acquire);
        if (record.return_captured) {
            record.return_rax = source.return_rax.load(std::memory_order_relaxed);
            record.return_xmm_low = source.return_xmm_low.load(std::memory_order_relaxed);
            record.return_xmm_high = source.return_xmm_high.load(std::memory_order_relaxed);
        }
        record.arguments.reserve(session.argument_count);
        record.argument_xmm_low.reserve(session.argument_count);
        record.argument_xmm_high.reserve(session.argument_count);
        for (std::size_t index = 0; index < session.argument_count; ++index) {
            const std::size_t storage = argument_slot(session, sequence, index);
            record.arguments.push_back(session.arguments[storage].load(std::memory_order_relaxed));
            record.argument_xmm_low.push_back(session.argument_xmm_low[storage].load(std::memory_order_relaxed));
            record.argument_xmm_high.push_back(session.argument_xmm_high[storage].load(std::memory_order_relaxed));
        }
        if (source.published_sequence.load(std::memory_order_acquire) == sequence + 1) out.records.push_back(std::move(record));
    }
    return out;
}
} // namespace

bool start(const URK::Unity::Inspect::MethodInfo &method, std::string &error) {
    std::lock_guard lock(g_state.control_mutex);
    if (!URK::hooks::available()) { error = "Hook API is unavailable in this runtime"; return false; }
    if (!method.handle) { error = "Method metadata handle is unavailable"; return false; }
    const auto *method_handle = static_cast<const URK::il2cpp::Method *>(method.handle);
    for (const auto &existing : g_state.sessions) {
        if (existing->method != method_handle)
            continue;
        if (existing->active.load(std::memory_order_acquire)) {
            error = "This method is already being traced";
            return false;
        }
        reset_records(*existing);
        g_state.diagnostic.clear();
        if (!URK::il2cpp::helpers::try_hook_method_pointer(method_handle, &existing->original, existing->stub,
                                                            &tracer_diagnostic, nullptr, nullptr, nullptr, nullptr,
                                                            method.name.c_str())) {
            error = g_state.diagnostic.empty() ? "The runtime refused to re-enable this method trace" : g_state.diagnostic;
            return false;
        }
        existing->active.store(true, std::memory_order_release);
        existing->visible = true;
        error.clear();
        return true;
    }
    if (g_state.sessions.size() >= max_sessions) { error = "Method tracer limit reached (" + std::to_string(max_sessions) + ")"; return false; }
    auto session = std::make_unique<HookSession>();
    session->id = g_state.next_id++;
    session->method = method_handle;
    session->method_name = method.name;
    session->declaring_type = method.declaring_type.full_name;
    char pointer_text[32]{};
    std::snprintf(pointer_text, sizeof(pointer_text), "%p", method.handle);
    session->method_pointer_text = pointer_text;
    session->argument_count = method.parameters.size();
    session->is_static = method.is_static;
    session->target_is_reference = !method.is_static && !method.declaring_type.is_value_type;
    session->return_type = method.return_type;
    session->return_is_floating = is_floating(method.return_type);
    session->return_is_value_type = method.return_is_value_type;
    session->return_is_opaque = method.return_type_is_opaque;
    session->return_uses_indirect_abi = method.return_is_value_type && !method.return_is_enum &&
                                        !method.return_type_is_opaque && !is_register_scalar(method.return_type);
    session->return_is_reference = !method.return_is_value_type && method.return_type != "System.Void" &&
                                   method.return_type != "Void" && method.return_type != "void";
    if (session->argument_count > 0) {
        if (session->argument_count > std::numeric_limits<std::size_t>::max() / max_records) {
            error = "Method tracer argument storage size overflow";
            return false;
        }
        const std::size_t storage_count = max_records * session->argument_count;
        try {
            session->arguments = std::make_unique<std::atomic<std::uint64_t>[]>(storage_count);
            session->argument_xmm_low = std::make_unique<std::atomic<std::uint64_t>[]>(storage_count);
            session->argument_xmm_high = std::make_unique<std::atomic<std::uint64_t>[]>(storage_count);
        }
        catch (const std::bad_alloc &) {
            error = "Method tracer could not allocate argument capture storage";
            return false;
        }
    }
    session->argument_kinds.reserve(method.parameters.size());
    session->argument_is_reference.reserve(method.parameters.size());
    session->argument_is_value_type.reserve(method.parameters.size());
    session->argument_is_opaque.reserve(method.parameters.size());
    for (std::size_t index = 0; index < method.parameters.size(); ++index) {
        const auto &parameter = method.parameters[index];
        session->parameter_names.push_back(parameter.name.empty() ? "arg" + std::to_string(index + 1) : parameter.name);
        session->parameter_types.push_back(parameter.type_name);
        session->argument_kinds.push_back(is_floating(parameter.type_name) ? ArgumentKind::Floating
                                          : parameter.is_value_type && !parameter.is_enum ? ArgumentKind::Aggregate
                                                                                           : ArgumentKind::Integer);
        session->argument_is_reference.push_back(!parameter.is_value_type);
        session->argument_is_value_type.push_back(parameter.is_value_type && !parameter.is_enum);
        session->argument_is_opaque.push_back(parameter.is_opaque);
    }
    reset_records(*session);
    if (!create_stub(*session, error)) return false;
    HookSession *const raw = session.get();
    g_state.diagnostic.clear();
    if (!URK::il2cpp::helpers::try_hook_method_pointer(method_handle, &raw->original, raw->stub, &tracer_diagnostic,
                                                        nullptr, nullptr, nullptr, nullptr, method.name.c_str())) {
        error = g_state.diagnostic.empty() ? "The runtime refused to hook this method" : g_state.diagnostic;
        VirtualFree(raw->stub, 0, MEM_RELEASE);
        return false;
    }
    raw->active.store(true, std::memory_order_release);
    g_state.sessions.push_back(std::move(session));
    error.clear();
    return true;
}

bool stop(const URK::il2cpp::Method *method) {
    std::lock_guard lock(g_state.control_mutex);
    for (const auto &session : g_state.sessions)
        if (session->method == method && session->active.load(std::memory_order_acquire)) { deactivate(*session); return true; }
    return false;
}

bool stop(TraceId id) {
    std::lock_guard lock(g_state.control_mutex);
    for (const auto &session : g_state.sessions)
        if (session->id == id && session->active.load(std::memory_order_acquire)) { deactivate(*session); return true; }
    return false;
}

bool clear(TraceId id) {
    std::lock_guard lock(g_state.control_mutex);
    for (const auto &session : g_state.sessions) if (session->id == id) {
        const bool resume = session->active.exchange(false, std::memory_order_acq_rel);
        while (session->in_flight.load(std::memory_order_acquire) != 0) SwitchToThread();
        reset_records(*session);
        if (resume) session->active.store(true, std::memory_order_release);
        return true;
    }
    return false;
}

bool close(TraceId id) {
    std::lock_guard lock(g_state.control_mutex);
    for (const auto &session : g_state.sessions)
        if (session->id == id && !session->active.load(std::memory_order_acquire)) {
            session->visible = false;
            return true;
        }
    return false;
}

void stop_all() {
    std::lock_guard lock(g_state.control_mutex);
    for (const auto &session : g_state.sessions) if (session->active.load(std::memory_order_acquire)) deactivate(*session);
}

void shutdown() {
    stop_all();
    std::lock_guard lock(g_state.control_mutex);
    for (const auto &session : g_state.sessions) if (session->stub) VirtualFree(session->stub, 0, MEM_RELEASE);
    g_state.sessions.clear();
}

bool any_active() {
    std::lock_guard lock(g_state.control_mutex);
    for (const auto &session : g_state.sessions) if (session->active.load(std::memory_order_acquire)) return true;
    return false;
}

std::vector<Snapshot> snapshots() {
    std::lock_guard lock(g_state.control_mutex);
    std::vector<Snapshot> out;
    out.reserve(g_state.sessions.size());
    for (const auto &session : g_state.sessions)
        if (session->visible)
            out.push_back(copy_snapshot(*session));
    return out;
}
} // namespace Explorer::MethodTracer

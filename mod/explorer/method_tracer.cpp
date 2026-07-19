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
#include <memory>
#include <mutex>

namespace Explorer::MethodTracer {
namespace {
enum class ArgumentKind : std::uint8_t { Integer, Floating };

struct TraceRegisterFrame {
    std::array<std::uint8_t, 16> xmm0, xmm1, xmm2, xmm3, xmm4, xmm5;
    std::uint64_t r11, r10, r9, r8, rdx, rcx, rax, return_address;
    std::uint64_t stack_arguments[1];
};
static_assert(offsetof(TraceRegisterFrame, r11) == 96);
static_assert(offsetof(TraceRegisterFrame, return_address) == 152);
static_assert(offsetof(TraceRegisterFrame, stack_arguments) == 160);

struct RingRecord {
    std::atomic<std::uint64_t> published_sequence{0}, timestamp_ticks{0};
    std::atomic<std::uint32_t> thread_id{0};
    std::atomic<std::uintptr_t> caller_address{0}, target_address{0};
    std::array<std::atomic<std::uint64_t>, max_parameters> arguments{};
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
    std::array<RingRecord, max_records> records{};
    std::array<ArgumentKind, max_parameters> argument_kinds{};
    std::array<bool, max_parameters> argument_is_reference{};
    std::size_t argument_count = 0;
    bool is_static = false;
    bool target_is_reference = false;
    std::uint64_t start_timestamp_ticks = 0;
    std::string method_pointer_text, method_name, declaring_type;
    std::vector<std::string> parameter_names, parameter_types;
};

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

std::uint64_t register_value(const TraceRegisterFrame *frame, std::size_t slot, ArgumentKind kind) {
    if (slot >= 4)
        return frame->stack_arguments[slot - 4];
    if (kind == ArgumentKind::Floating) {
        const std::array<std::uint8_t, 16> *xmm = &frame->xmm0;
        if (slot == 1) xmm = &frame->xmm1;
        else if (slot == 2) xmm = &frame->xmm2;
        else if (slot == 3) xmm = &frame->xmm3;
        std::uint64_t value = 0;
        std::memcpy(&value, xmm->data(), sizeof(value));
        return value;
    }
    return slot == 0 ? frame->rcx : slot == 1 ? frame->rdx : slot == 2 ? frame->r8 : frame->r9;
}

extern "C" void trace_record_from_stub(const TraceRegisterFrame *frame, HookSession *session) {
    if (!frame || !session || !session->active.load(std::memory_order_acquire))
        return;
    session->in_flight.fetch_add(1, std::memory_order_acq_rel);
    if (!session->active.load(std::memory_order_acquire)) {
        session->in_flight.fetch_sub(1, std::memory_order_release);
        return;
    }
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
        record.arguments[index].store(register_value(frame, slot, session->argument_kinds[index]), std::memory_order_relaxed);
    }
    record.published_sequence.store(sequence + 1, std::memory_order_release);
    session->in_flight.fetch_sub(1, std::memory_order_release);
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
    for (std::uint8_t index = 0; index < 6; ++index)
        emit(code, {0xF3, 0x0F, 0x6F, static_cast<std::uint8_t>(0x44 + (index << 3)), 0x24, static_cast<std::uint8_t>(index * 16)});
    emit(code, {0x48, 0x83, 0xC4, 0x60, 0x41, 0x5B, 0x41, 0x5A, 0x41, 0x59, 0x41, 0x58, 0x5A, 0x59, 0x58, 0x49, 0xBB});
    emit_u64(code, reinterpret_cast<std::uintptr_t>(&session.original));
    emit(code, {0x41, 0xFF, 0x23});
    void *memory = VirtualAlloc(nullptr, code.size(), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!memory) { error = "Method tracer could not allocate its native detour stub"; return false; }
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
    for (RingRecord &record : session.records)
        record.published_sequence.store(0, std::memory_order_relaxed);
    session.write_sequence.store(0, std::memory_order_relaxed);
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
    out.parameter_names = session.parameter_names;
    out.parameter_types = session.parameter_types;
    out.parameter_is_reference.assign(session.argument_is_reference.begin(), session.argument_is_reference.begin() + session.argument_count);
    out.target_is_reference = session.target_is_reference;
    out.timestamp_frequency = static_cast<std::uint64_t>(g_state.frequency.QuadPart);
    out.start_timestamp_ticks = session.start_timestamp_ticks;
    const std::uint64_t total = session.write_sequence.load(std::memory_order_acquire);
    out.total_calls = total;
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
        record.arguments.reserve(session.argument_count);
        for (std::size_t index = 0; index < session.argument_count; ++index)
            record.arguments.push_back(source.arguments[index].load(std::memory_order_relaxed));
        if (source.published_sequence.load(std::memory_order_acquire) == sequence + 1) out.records.push_back(std::move(record));
    }
    return out;
}
} // namespace

bool start(const URK::Unity::Inspect::MethodInfo &method, std::string &error) {
    std::lock_guard lock(g_state.control_mutex);
    if (!URK::hooks::available()) { error = "Hook API is unavailable in this runtime"; return false; }
    if (!method.handle) { error = "Method metadata handle is unavailable"; return false; }
    if (method.parameters.size() > max_parameters) { error = "Method tracer supports up to " + std::to_string(max_parameters) + " parameters"; return false; }
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
    for (std::size_t index = 0; index < method.parameters.size(); ++index) {
        const auto &parameter = method.parameters[index];
        session->parameter_names.push_back(parameter.name.empty() ? "arg" + std::to_string(index + 1) : parameter.name);
        session->parameter_types.push_back(parameter.type_name);
        session->argument_kinds[index] = is_floating(parameter.type_name) ? ArgumentKind::Floating : ArgumentKind::Integer;
        session->argument_is_reference[index] = !URK::Unity::Inspect::DescribeType(parameter.type).is_value_type;
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

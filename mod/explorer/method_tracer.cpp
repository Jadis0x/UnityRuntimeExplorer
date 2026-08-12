// Copyright (c) 2026 Jadis0x. All rights reserved.
#include "method_tracer.h"
#include "method_trace_abi.h"

#include "sdk/hook_api.h"
#include "sdk/runtime/managed_hooks.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <array>
#include <atomic>
#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <string_view>
#include <unordered_map>

namespace Explorer::MethodTracer {
namespace {
enum class ArgumentKind : std::uint8_t { Integer, Floating, Aggregate };

using MethodTraceAbi::RegisterFrame;

struct RingRecord {
    std::atomic<std::uint64_t> published_sequence{0}, timestamp_ticks{0};
    std::atomic<std::uint32_t> thread_id{0};
    std::atomic<std::uintptr_t> caller_address{0}, target_address{0};
    std::atomic<std::uint64_t> return_rax{0}, return_xmm_low{0}, return_xmm_high{0};
    std::atomic<std::uintptr_t> return_buffer_address{0};
    std::atomic<bool> return_published{false};
};

struct HookSession {
    TraceId id = 0;
    const URK::managed::Method *method = nullptr;
    void *original = nullptr;
    void *stub = nullptr;
    bool visible = true;
    std::atomic<bool> active{false};
    // The high bit closes the entry gate before a hook is detached. The
    // remaining bits count calls that already entered the detour.
    static constexpr std::uint64_t flight_accepting = 1ull << 63;
    static constexpr std::uint64_t flight_count_mask = ~flight_accepting;
    std::atomic<std::uint64_t> flight_state{flight_accepting};
    std::atomic<bool> detach_pending{false};
    std::atomic<std::uint64_t> write_sequence{0};
    std::atomic<std::uint64_t> native_faults{0};
    std::array<RingRecord, max_records> records{};
    std::unique_ptr<std::atomic<std::uint64_t>[]> arguments;
    std::unique_ptr<std::atomic<std::uint64_t>[]> argument_xmm_low;
    std::unique_ptr<std::atomic<std::uint64_t>[]> argument_xmm_high;
    std::vector<ArgumentKind> argument_kinds;
    std::vector<bool> argument_is_reference;
    std::vector<bool> argument_is_value_type;
    std::vector<bool> argument_is_enum;
    std::vector<bool> argument_is_by_ref;
    std::vector<std::string> argument_enum_underlying_types;
    std::vector<bool> argument_is_opaque;
    std::vector<const void*> argument_type_handles;
    std::vector<const void*> argument_value_classes;
    std::vector<std::size_t> argument_value_sizes;
    std::vector<std::size_t> argument_value_word_offsets;
    std::size_t argument_value_word_count = 0;
    std::unique_ptr<std::atomic<std::uint64_t>[]> argument_value_words;
    std::vector<std::size_t> argument_byref_value_sizes;
    std::vector<std::size_t> argument_byref_word_offsets;
    std::size_t argument_byref_word_count = 0;
    std::unique_ptr<std::atomic<std::uint64_t>[]> argument_byref_value_words;
    std::unique_ptr<std::atomic<std::uint64_t>[]> return_value_words;
    std::size_t argument_count = 0;
    bool is_static = false;
    bool target_is_reference = false;
    bool return_is_reference = false;
    bool return_is_value_type = false;
    bool return_is_enum = false;
    std::string return_enum_underlying_type;
    const void* return_type_handle = nullptr;
    bool return_is_opaque = false;
    const void* return_value_class = nullptr;
    std::size_t return_value_size = 0;
    std::size_t return_value_word_count = 0;
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

#if defined(_WIN32)
int trace_exception_filter(unsigned long code);
#endif

struct RuntimeTypeTraits {
    bool resolved = false;
    bool is_value_type = false;
    bool is_enum = false;
    const URK::managed::Class* klass = nullptr;
};

const URK::managed::Class* resolve_class_by_name(std::string_view name) {
    static std::unordered_map<std::string, const URK::managed::Class*> cache;
    std::string normalized(name);
    while (!normalized.empty() && (normalized.back() == '&' || normalized.back() == '*'))
        normalized.pop_back();
    if (normalized.empty() || normalized.find('[') != std::string::npos)
        return nullptr;
    if (const auto found = cache.find(normalized); found != cache.end())
        return found->second;

    constexpr std::size_t kMaxClassesToSearch = 250000;
    std::size_t visited = 0;
    for (std::size_t assembly_index = 0;
         assembly_index < URK::managed::domain_get_assembly_count() && visited < kMaxClassesToSearch;
         ++assembly_index) {
        const auto* assembly = URK::managed::domain_get_assembly(assembly_index);
        const auto* image = assembly ? URK::managed::assembly_get_image(assembly) : nullptr;
        if (!image)
            continue;
        const std::size_t class_count = URK::managed::image_get_class_count(image);
        for (std::size_t class_index = 0; class_index < class_count && visited++ < kMaxClassesToSearch;
             ++class_index) {
            const auto* klass = URK::managed::image_get_class(image, class_index);
            if (!klass)
                continue;
            const char* namespc = URK::managed::class_get_namespace(klass);
            const char* class_name = URK::managed::class_get_name(klass);
            if (!class_name)
                continue;
            const std::string full_name = !namespc || namespc[0] == '\0'
                ? std::string(class_name) : std::string(namespc) + "." + class_name;
            if (full_name == normalized) {
                cache.emplace(std::move(normalized), klass);
                return klass;
            }
        }
    }
    cache.emplace(std::move(normalized), nullptr);
    return nullptr;
}

RuntimeTypeTraits resolve_runtime_type_traits(const void* type_handle, std::string_view display_name) {
    RuntimeTypeTraits traits{};
#if defined(_WIN32)
    __try {
#endif
        const auto* klass = type_handle ? URK::managed::type_get_class_or_element_class(
            static_cast<const URK::managed::Type*>(type_handle)) : nullptr;
        std::string normalized(display_name);
        while (!normalized.empty() && (normalized.back() == '&' || normalized.back() == '*'))
            normalized.pop_back();
        if (klass) {
            const char* namespc = URK::managed::class_get_namespace(klass);
            const char* class_name = URK::managed::class_get_name(klass);
            const std::string class_display = !class_name ? std::string{}
                : !namespc || namespc[0] == '\0' ? std::string(class_name)
                : std::string(namespc) + "." + class_name;
            // Bridge type records can point at a proxy class rather than the
            // declared signature type. Do not use that proxy to choose an ABI.
            if (!normalized.empty() && class_display != normalized)
                klass = nullptr;
        }
        if (!klass)
            klass = resolve_class_by_name(display_name);
        if (!klass)
            return traits;
        traits.resolved = true;
        traits.klass = klass;
        traits.is_value_type = URK::managed::class_is_valuetype(klass);
        traits.is_enum = URK::managed::class_is_enum(klass);
        // class_value_size is an allocation/layout size and is positive for
        // reference types such as System.String on IL2CPP.  Treating it as a
        // value-type discriminator corrupts reference decoding by interpreting
        // the managed pointer as inline struct bytes. class_is_valuetype is the
        // runtime's authoritative ABI classification.
        if (!traits.is_enum && URK::managed::class_enum_basetype(klass) != nullptr)
            traits.is_enum = true;
#if defined(_WIN32)
    }
    __except (trace_exception_filter(GetExceptionCode())) {
        return {};
    }
#endif
    return traits;
}

bool resolve_value_return_layout(HookSession& session) {
    if (!session.return_is_value_type || session.return_is_enum || session.return_is_opaque ||
        !session.return_type_handle)
        return false;
#if defined(_WIN32)
    __try {
#endif
        const auto* klass = session.return_value_class
            ? static_cast<const URK::managed::Class*>(session.return_value_class)
            : URK::managed::type_get_class_or_element_class(
                  static_cast<const URK::managed::Type*>(session.return_type_handle));
        std::uint32_t alignment = 0;
        const std::int32_t size = klass ? URK::managed::class_value_size(klass, &alignment) : 0;
        if (size <= 0)
            return false;
        session.return_value_class = klass;
        session.return_value_size = static_cast<std::size_t>(size);
        session.return_uses_indirect_abi = session.return_value_size != 1 && session.return_value_size != 2 &&
            session.return_value_size != 4 && session.return_value_size != 8;
        return true;
#if defined(_WIN32)
    }
    __except (trace_exception_filter(GetExceptionCode())) {
        return false;
    }
#endif
}

std::size_t resolve_byref_value_size(const URK::Unity::Inspect::MethodParamInfo& parameter,
                                     bool is_by_ref, bool is_value_type,
                                     const URK::managed::Class* known_class) {
    if (!is_by_ref)
        return 0;
    if (!is_value_type)
        return sizeof(void*);
#if defined(_WIN32)
    __try {
#endif
        const auto* klass = known_class ? known_class : URK::managed::type_get_class_or_element_class(
            static_cast<const URK::managed::Type*>(parameter.type));
        std::uint32_t alignment = 0;
        const std::int32_t size = klass ? URK::managed::class_value_size(klass, &alignment) : 0;
        return size > 0 ? static_cast<std::size_t>(size) : 0;
#if defined(_WIN32)
    }
    __except (trace_exception_filter(GetExceptionCode())) {
        return 0;
    }
#endif
}

std::size_t resolve_value_size(const URK::Unity::Inspect::MethodParamInfo& parameter,
                               bool is_by_ref, bool is_value_type, bool is_enum,
                               const URK::managed::Class* known_class) {
    if (!is_value_type || is_enum || is_by_ref || !parameter.type)
        return 0;
#if defined(_WIN32)
    __try {
#endif
        const auto* klass = known_class ? known_class : URK::managed::type_get_class_or_element_class(
            static_cast<const URK::managed::Type*>(parameter.type));
        std::uint32_t alignment = 0;
        const std::int32_t size = klass ? URK::managed::class_value_size(klass, &alignment) : 0;
        return size > 0 ? static_cast<std::size_t>(size) : 0;
#if defined(_WIN32)
    }
    __except (trace_exception_filter(GetExceptionCode())) {
        return 0;
    }
#endif
}

std::uint64_t register_value(const RegisterFrame *frame, std::size_t slot, ArgumentKind kind) {
    if (slot >= 4)
        return MethodTraceAbi::integer_argument(*frame, slot);
    if (kind == ArgumentKind::Floating) {
        return MethodTraceAbi::xmm_argument(*frame, slot, 0);
    }
    return MethodTraceAbi::integer_argument(*frame, slot);
}

std::uint64_t xmm_lane_value(const RegisterFrame *frame, std::size_t slot, std::size_t offset) {
    return MethodTraceAbi::xmm_argument(*frame, slot, offset);
}

std::size_t argument_slot(const HookSession &session, std::uint64_t sequence, std::size_t argument_index) {
    return static_cast<std::size_t>(sequence % max_records) * session.argument_count + argument_index;
}

std::size_t byref_word_count(std::size_t bytes) {
    return (bytes + sizeof(std::uint64_t) - 1) / sizeof(std::uint64_t);
}

void copy_value_words(std::atomic<std::uint64_t>* destination, std::size_t word_count,
                      const void* source, std::size_t byte_count) {
    for (std::size_t word = 0; word < word_count; ++word) {
        const std::size_t offset = word * sizeof(std::uint64_t);
        const std::size_t bytes = std::min(sizeof(std::uint64_t), byte_count - offset);
        std::uint64_t value = 0;
        std::memcpy(&value, static_cast<const std::uint8_t*>(source) + offset, bytes);
        destination[word].store(value, std::memory_order_relaxed);
    }
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

bool enter_flight(HookSession& session);
void leave_flight(HookSession& session);

extern "C" std::uintptr_t trace_record_from_stub(const RegisterFrame *frame, HookSession *session) {
    if (!frame || !session)
        return 0;
    if (!enter_flight(*session))
        return 0;
    if (!session->active.load(std::memory_order_acquire)) {
        leave_flight(*session);
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
    const std::size_t return_slot = session->return_uses_indirect_abi ? 1 : 0;
    record.target_address.store(session->is_static ? 0 : static_cast<std::uintptr_t>(
        register_value(frame, return_slot, ArgumentKind::Integer)), std::memory_order_relaxed);
    record.return_buffer_address.store(session->return_uses_indirect_abi
        ? static_cast<std::uintptr_t>(register_value(frame, 0, ArgumentKind::Integer)) : 0, std::memory_order_relaxed);
    for (std::size_t index = 0; index < session->argument_count; ++index) {
        const std::size_t slot = index + return_slot + (session->is_static ? 0 : 1);
        const std::size_t storage = argument_slot(*session, sequence, index);
        session->arguments[storage].store(register_value(frame, slot, session->argument_kinds[index]),
                                          std::memory_order_relaxed);
        session->argument_xmm_low[storage].store(xmm_lane_value(frame, slot, 0), std::memory_order_relaxed);
        session->argument_xmm_high[storage].store(xmm_lane_value(frame, slot, 8), std::memory_order_relaxed);
    }
    if (session->argument_value_words) {
        for (std::size_t index = 0; index < session->argument_count; ++index) {
            const std::size_t value_size = session->argument_value_sizes[index];
            if (value_size == 0)
                continue;
            const std::size_t words = byref_word_count(value_size);
            const std::size_t base = (sequence % max_records) * session->argument_value_word_count +
                session->argument_value_word_offsets[index];
            for (std::size_t word = 0; word < words; ++word)
                session->argument_value_words[base + word].store(0, std::memory_order_relaxed);
            const std::uint64_t raw = session->arguments[argument_slot(*session, sequence, index)]
                .load(std::memory_order_relaxed);
            if (value_size <= sizeof(raw)) {
                copy_value_words(session->argument_value_words.get() + base, words, &raw, value_size);
            } else if (raw != 0) {
                copy_value_words(session->argument_value_words.get() + base, words,
                                 reinterpret_cast<const void*>(static_cast<std::uintptr_t>(raw)), value_size);
            }
        }
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
        leave_flight(*session);
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
            if (session->argument_byref_value_words) {
                for (std::size_t index = 0; index < session->argument_count; ++index) {
                    const std::size_t value_size = session->argument_byref_value_sizes[index];
                    if (value_size == 0)
                        continue;
                    const std::size_t words = byref_word_count(value_size);
                    const std::size_t base = (context.sequence % max_records) * session->argument_byref_word_count +
                        session->argument_byref_word_offsets[index];
                    for (std::size_t word = 0; word < words; ++word)
                        session->argument_byref_value_words[base + word].store(0, std::memory_order_relaxed);
                    const std::uintptr_t address = session->arguments[argument_slot(*session, context.sequence, index)]
                        .load(std::memory_order_relaxed);
                    if (address == 0)
                        continue;
                    for (std::size_t word = 0; word < words; ++word) {
                        const std::size_t offset = word * sizeof(std::uint64_t);
                        const std::size_t bytes = std::min(sizeof(std::uint64_t), value_size - offset);
                        std::uint64_t value = 0;
                        std::memcpy(&value, reinterpret_cast<const void*>(address + offset), bytes);
                        session->argument_byref_value_words[base + word].store(value, std::memory_order_relaxed);
                    }
                }
            }
            if (session->return_uses_indirect_abi && session->return_value_words) {
                const std::uintptr_t buffer = record.return_buffer_address.load(std::memory_order_relaxed);
                if (buffer != 0) {
                    const std::size_t base = static_cast<std::size_t>(context.sequence % max_records) * session->return_value_word_count;
                    for (std::size_t index = 0; index < session->return_value_word_count; ++index) {
                        const std::size_t offset = index * sizeof(std::uint64_t);
                        const std::size_t bytes = std::min(sizeof(std::uint64_t), session->return_value_size - offset);
                        std::uint64_t value = 0;
                        std::memcpy(&value, reinterpret_cast<const void*>(buffer + offset), bytes);
                        session->return_value_words[base + index].store(value, std::memory_order_relaxed);
                    }
                }
            }
            record.return_published.store(true, std::memory_order_release);
        }
#if defined(_WIN32)
    }
    __except (trace_exception_filter(GetExceptionCode())) {
        session->native_faults.fetch_add(1, std::memory_order_relaxed);
    }
#endif
    leave_flight(*session);
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

bool enter_flight(HookSession& session) {
    std::uint64_t state = session.flight_state.load(std::memory_order_acquire);
    for (;;) {
        if ((state & HookSession::flight_accepting) == 0 ||
            (state & HookSession::flight_count_mask) == HookSession::flight_count_mask)
            return false;
        if (session.flight_state.compare_exchange_weak(
                state, state + 1, std::memory_order_acq_rel, std::memory_order_acquire))
            return true;
    }
}

void leave_flight(HookSession& session) {
    session.flight_state.fetch_sub(1, std::memory_order_release);
}

std::uint64_t flight_count(const HookSession& session) {
    return session.flight_state.load(std::memory_order_acquire) & HookSession::flight_count_mask;
}

bool deactivate(HookSession &session) {
    session.active.store(false, std::memory_order_release);
    session.flight_state.fetch_and(~HookSession::flight_accepting, std::memory_order_acq_rel);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(50);
    while (flight_count(session) != 0 && std::chrono::steady_clock::now() < deadline)
        SwitchToThread();
    if (flight_count(session) != 0) {
        // Keep the stub mapped and attached while an already-entered call is
        // unwinding. The detour is inactive, so new calls pass through safely.
        session.detach_pending.store(true, std::memory_order_release);
        return false;
    }
    if (session.original && session.stub &&
        !URK::hooks::detach_ex(&session.original, session.stub)) {
        session.detach_pending.store(true, std::memory_order_release);
        return false;
    }
    session.detach_pending.store(false, std::memory_order_release);
    return true;
}

bool retry_pending_detach(HookSession& session) {
    if (!session.detach_pending.load(std::memory_order_acquire) || flight_count(session) != 0)
        return false;
    if (session.original && session.stub &&
        !URK::hooks::detach_ex(&session.original, session.stub))
        return false;
    session.detach_pending.store(false, std::memory_order_release);
    return true;
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
    out.parameter_type_handles = session.argument_type_handles;
    out.parameter_value_classes = session.argument_value_classes;
    out.parameter_value_sizes = session.argument_value_sizes;
    out.parameter_is_reference = session.argument_is_reference;
    out.parameter_is_value_type = session.argument_is_value_type;
    out.parameter_is_enum = session.argument_is_enum;
    out.parameter_is_by_ref = session.argument_is_by_ref;
    out.parameter_enum_underlying_types = session.argument_enum_underlying_types;
    out.parameter_is_opaque = session.argument_is_opaque;
    out.parameter_is_floating.reserve(session.argument_kinds.size());
    for (const ArgumentKind kind : session.argument_kinds)
        out.parameter_is_floating.push_back(kind == ArgumentKind::Floating);
    out.target_is_reference = session.target_is_reference;
    out.return_is_reference = session.return_is_reference;
    out.return_is_value_type = session.return_is_value_type;
    out.return_is_enum = session.return_is_enum;
    out.return_enum_underlying_type = session.return_enum_underlying_type;
    out.return_type_handle = session.return_type_handle;
    out.return_is_opaque = session.return_is_opaque;
    out.return_value_class = session.return_value_class;
    out.return_value_size = session.return_value_size;
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
        record.sequence_start = record.sequence;
        record.timestamp_ticks = source.timestamp_ticks.load(std::memory_order_relaxed);
        record.first_timestamp_ticks = record.timestamp_ticks;
        record.thread_id = source.thread_id.load(std::memory_order_relaxed);
        record.caller_address = source.caller_address.load(std::memory_order_relaxed);
        record.target_address = source.target_address.load(std::memory_order_relaxed);
        record.return_buffer_address = source.return_buffer_address.load(std::memory_order_relaxed);
        record.return_captured = source.return_published.load(std::memory_order_acquire);
        if (record.return_captured) {
            record.return_rax = source.return_rax.load(std::memory_order_relaxed);
            record.return_xmm_low = source.return_xmm_low.load(std::memory_order_relaxed);
            record.return_xmm_high = source.return_xmm_high.load(std::memory_order_relaxed);
            if (session.return_uses_indirect_abi && session.return_value_words && session.return_value_size != 0) {
                record.return_value_bytes.resize(session.return_value_size);
                const std::size_t base = static_cast<std::size_t>(sequence % max_records) * session.return_value_word_count;
                for (std::size_t index = 0; index < session.return_value_word_count; ++index) {
                    const std::size_t offset = index * sizeof(std::uint64_t);
                    const std::size_t bytes = std::min(sizeof(std::uint64_t), session.return_value_size - offset);
                    const std::uint64_t value = session.return_value_words[base + index].load(std::memory_order_relaxed);
                    std::memcpy(record.return_value_bytes.data() + offset, &value, bytes);
                }
            }
            else if (session.return_value_class && session.return_value_size != 0 && session.return_value_size <= sizeof(record.return_rax)) {
                record.return_value_bytes.resize(session.return_value_size);
                std::memcpy(record.return_value_bytes.data(), &record.return_rax, session.return_value_size);
            }
        }
        record.arguments.reserve(session.argument_count);
        record.argument_xmm_low.reserve(session.argument_count);
        record.argument_xmm_high.reserve(session.argument_count);
        record.argument_byref_value_bytes.resize(session.argument_count);
        record.argument_value_bytes.resize(session.argument_count);
        for (std::size_t index = 0; index < session.argument_count; ++index) {
            const std::size_t storage = argument_slot(session, sequence, index);
            record.arguments.push_back(session.arguments[storage].load(std::memory_order_relaxed));
            record.argument_xmm_low.push_back(session.argument_xmm_low[storage].load(std::memory_order_relaxed));
            record.argument_xmm_high.push_back(session.argument_xmm_high[storage].load(std::memory_order_relaxed));
            const std::size_t value_size = session.argument_byref_value_sizes[index];
            if (record.return_captured && value_size != 0 && session.argument_byref_value_words) {
                std::vector<std::uint8_t>& bytes = record.argument_byref_value_bytes[index];
                bytes.resize(value_size);
                const std::size_t base = (sequence % max_records) * session.argument_byref_word_count +
                    session.argument_byref_word_offsets[index];
                for (std::size_t word = 0; word < byref_word_count(value_size); ++word) {
                    const std::size_t offset = word * sizeof(std::uint64_t);
                    const std::size_t copy_size = std::min(sizeof(std::uint64_t), value_size - offset);
                    const std::uint64_t value = session.argument_byref_value_words[base + word]
                        .load(std::memory_order_relaxed);
                    std::memcpy(bytes.data() + offset, &value, copy_size);
                }
            }
            const std::size_t entry_value_size = session.argument_value_sizes[index];
            if (entry_value_size != 0 && session.argument_value_words) {
                std::vector<std::uint8_t>& bytes = record.argument_value_bytes[index];
                bytes.resize(entry_value_size);
                const std::size_t base = (sequence % max_records) * session.argument_value_word_count +
                    session.argument_value_word_offsets[index];
                for (std::size_t word = 0; word < byref_word_count(entry_value_size); ++word) {
                    const std::size_t offset = word * sizeof(std::uint64_t);
                    const std::size_t copy_size = std::min(sizeof(std::uint64_t), entry_value_size - offset);
                    const std::uint64_t value = session.argument_value_words[base + word]
                        .load(std::memory_order_relaxed);
                    std::memcpy(bytes.data() + offset, &value, copy_size);
                }
            }
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
    if (g_state.frequency.QuadPart <= 0 &&
        (!QueryPerformanceFrequency(&g_state.frequency) || g_state.frequency.QuadPart <= 0)) {
        error = "Method tracer could not initialize the high-resolution timestamp clock";
        return false;
    }
    const auto *method_handle = static_cast<const URK::managed::Method *>(method.handle);
    for (const auto &existing : g_state.sessions) {
        if (existing->method != method_handle)
            continue;
        if (existing->active.load(std::memory_order_acquire)) {
            error = "This method is already being traced";
            return false;
        }
        reset_records(*existing);
        g_state.diagnostic.clear();
        if (!URK::managed_hooks::try_hook_method_pointer(method_handle, &existing->original, existing->stub,
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
    session->return_type_handle = method.return_type_handle;
    session->return_is_floating = is_floating(method.return_type);
    const RuntimeTypeTraits return_traits = resolve_runtime_type_traits(method.return_type_handle, method.return_type);
    session->return_is_value_type = return_traits.resolved ? return_traits.is_value_type : method.return_is_value_type;
    session->return_is_enum = return_traits.resolved ? return_traits.is_enum : method.return_is_enum;
    session->return_value_class = return_traits.klass;
    if (session->return_is_enum)
        session->return_enum_underlying_type =
            URK::Unity::Inspect::enum_underlying_type_name(method.return_type_handle);
    session->return_is_opaque = method.return_type_is_opaque;
    resolve_value_return_layout(*session);
    session->return_is_reference = !session->return_is_value_type && method.return_type != "System.Void" &&
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
    if (session->return_uses_indirect_abi && session->return_value_size != 0) {
        session->return_value_word_count = (session->return_value_size + sizeof(std::uint64_t) - 1) / sizeof(std::uint64_t);
        if (session->return_value_word_count > std::numeric_limits<std::size_t>::max() / max_records) {
            error = "Method tracer return storage size overflow";
            return false;
        }
        try {
            session->return_value_words = std::make_unique<std::atomic<std::uint64_t>[]>(
                session->return_value_word_count * max_records);
        }
        catch (const std::bad_alloc&) {
            error = "Method tracer could not allocate return capture storage";
            return false;
        }
    }
    session->argument_kinds.reserve(method.parameters.size());
    session->argument_is_reference.reserve(method.parameters.size());
    session->argument_is_value_type.reserve(method.parameters.size());
    session->argument_is_enum.reserve(method.parameters.size());
    session->argument_is_by_ref.reserve(method.parameters.size());
    session->argument_enum_underlying_types.reserve(method.parameters.size());
    session->argument_is_opaque.reserve(method.parameters.size());
    session->argument_type_handles.reserve(method.parameters.size());
    session->argument_value_classes.reserve(method.parameters.size());
    session->argument_value_sizes.reserve(method.parameters.size());
    session->argument_value_word_offsets.reserve(method.parameters.size());
    for (std::size_t index = 0; index < method.parameters.size(); ++index) {
        const auto &parameter = method.parameters[index];
        const RuntimeTypeTraits traits = resolve_runtime_type_traits(parameter.type, parameter.type_name);
        const bool is_value_type = traits.resolved ? traits.is_value_type : parameter.is_value_type;
        const bool is_enum = traits.resolved ? traits.is_enum : parameter.is_enum;
        // The bridge metadata in some IL2CPP titles reports the element type
        // but omits the BYREF flag. The managed signature still carries '&',
        // which is authoritative and avoids interpreting the pointer itself
        // as a byte/int/enum value.
        const bool is_by_ref = parameter.is_by_ref || std::string_view(parameter.type_name).ends_with('&');
        session->parameter_names.push_back(parameter.name.empty() ? "arg" + std::to_string(index + 1) : parameter.name);
        session->parameter_types.push_back(parameter.type_name);
        session->argument_kinds.push_back(is_floating(parameter.type_name) ? ArgumentKind::Floating
                                          : is_value_type && !is_enum ? ArgumentKind::Aggregate
                                                                       : ArgumentKind::Integer);
        session->argument_is_reference.push_back(!is_value_type);
        session->argument_is_value_type.push_back(is_value_type && !is_enum);
        session->argument_is_enum.push_back(is_enum);
        session->argument_is_by_ref.push_back(is_by_ref);
        session->argument_enum_underlying_types.push_back(
            is_enum ? URK::Unity::Inspect::enum_underlying_type_name(parameter.type) : std::string{});
        session->argument_is_opaque.push_back(parameter.is_opaque);
        session->argument_type_handles.push_back(parameter.type);
        session->argument_value_classes.push_back(traits.klass);
        session->argument_value_word_offsets.push_back(session->argument_value_word_count);
        const std::size_t entry_value_size = resolve_value_size(
            parameter, is_by_ref, is_value_type, is_enum, traits.klass);
        session->argument_value_sizes.push_back(entry_value_size);
        if (entry_value_size != 0) {
            const std::size_t words = byref_word_count(entry_value_size);
            if (words > std::numeric_limits<std::size_t>::max() - session->argument_value_word_count) {
                error = "Method tracer value-type capture size overflow";
                return false;
            }
            session->argument_value_word_count += words;
        }
        session->argument_byref_word_offsets.push_back(session->argument_byref_word_count);
        const std::size_t value_size = resolve_byref_value_size(
            parameter, is_by_ref, is_value_type, traits.klass);
        session->argument_byref_value_sizes.push_back(value_size);
        if (value_size != 0) {
            const std::size_t words = byref_word_count(value_size);
            if (words > std::numeric_limits<std::size_t>::max() - session->argument_byref_word_count) {
                error = "Method tracer by-reference capture size overflow";
                return false;
            }
            session->argument_byref_word_count += words;
        }
    }
    if (session->argument_value_word_count != 0) {
        if (session->argument_value_word_count > std::numeric_limits<std::size_t>::max() / max_records) {
            error = "Method tracer value-type capture storage size overflow";
            return false;
        }
        try {
            session->argument_value_words = std::make_unique<std::atomic<std::uint64_t>[]>(
                session->argument_value_word_count * max_records);
        }
        catch (const std::bad_alloc&) {
            error = "Method tracer could not allocate value-type capture storage";
            return false;
        }
    }
    if (session->argument_byref_word_count != 0) {
        if (session->argument_byref_word_count > std::numeric_limits<std::size_t>::max() / max_records) {
            error = "Method tracer by-reference capture storage size overflow";
            return false;
        }
        try {
            session->argument_byref_value_words = std::make_unique<std::atomic<std::uint64_t>[]>(
                session->argument_byref_word_count * max_records);
        }
        catch (const std::bad_alloc&) {
            error = "Method tracer could not allocate by-reference capture storage";
            return false;
        }
    }
    reset_records(*session);
    if (!create_stub(*session, error)) return false;
    HookSession *const raw = session.get();
    g_state.diagnostic.clear();
    if (!URK::managed_hooks::try_hook_method_pointer(method_handle, &raw->original, raw->stub, &tracer_diagnostic,
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

bool stop(const URK::managed::Method *method) {
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
        if (session->detach_pending.load(std::memory_order_acquire))
            return false;
        const bool resume = session->active.exchange(false, std::memory_order_acq_rel);
        if (resume)
            session->flight_state.fetch_and(~HookSession::flight_accepting, std::memory_order_acq_rel);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(50);
        while (flight_count(*session) != 0 && std::chrono::steady_clock::now() < deadline)
            SwitchToThread();
        if (flight_count(*session) != 0) {
            session->detach_pending.store(true, std::memory_order_release);
            return false;
        }
        reset_records(*session);
        if (resume) {
            session->flight_state.store(HookSession::flight_accepting, std::memory_order_release);
            session->active.store(true, std::memory_order_release);
        }
        return true;
    }
    return false;
}

bool close(TraceId id) {
    std::lock_guard lock(g_state.control_mutex);
    for (const auto &session : g_state.sessions)
        if (session->id == id && !session->active.load(std::memory_order_acquire) &&
            !session->detach_pending.load(std::memory_order_acquire)) {
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
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    bool pending = true;
    while (pending && std::chrono::steady_clock::now() < deadline) {
        pending = false;
        for (const auto &session : g_state.sessions) {
            retry_pending_detach(*session);
            pending = pending || session->detach_pending.load(std::memory_order_acquire);
        }
        if (pending)
            SwitchToThread();
    }
    if (pending) {
        // Never free a stub that an in-flight call can still return through.
        // The process is shutting down, so retaining this small allocation is
        // safer than turning an incomplete detach into an execute-after-free.
        g_state.diagnostic = "Method tracer shutdown deferred a native stub detach because a call remained in flight";
        return;
    }
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
    for (const auto &session : g_state.sessions)
        retry_pending_detach(*session);
    std::vector<Snapshot> out;
    out.reserve(g_state.sessions.size());
    for (const auto &session : g_state.sessions)
        if (session->visible)
            out.push_back(copy_snapshot(*session));
    return out;
}
} // namespace Explorer::MethodTracer

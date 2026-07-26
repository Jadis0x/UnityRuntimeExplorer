// Copyright (c) 2026 Jadis0x. All rights reserved.
#pragma once

#include "../../runtime/managed_runtime.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace URK::Unity::detail {
inline std::string& error_slot() { thread_local std::string value; return value; }
inline std::mutex& cache_mutex() { static std::mutex value; return value; }
inline std::unordered_map<std::string, const void*>& class_cache() { static std::unordered_map<std::string, const void*> value; return value; }
inline std::unordered_map<std::string, void*>& type_cache() { static std::unordered_map<std::string, void*> value; return value; }
inline std::unordered_map<std::string, const void*>& method_cache() { static std::unordered_map<std::string, const void*> value; return value; }
inline std::unordered_map<std::string, const void*>& field_cache() { static std::unordered_map<std::string, const void*> value; return value; }
inline std::atomic<std::uint64_t>& quarantined_gchandle_counter() {
    static std::atomic<std::uint64_t> value{0};
    return value;
}
#if defined(_WIN32)
inline int native_access_exception_filter(unsigned long code) {
    return code == 0xC0000005ul || code == 0xC0000006ul ? 1 : 0;
}
#endif
inline void clear_metadata_caches() {
    std::lock_guard<std::mutex> lock(cache_mutex());
    class_cache().clear();
    type_cache().clear();
    method_cache().clear();
    field_cache().clear();
}
inline void clear_error() { error_slot().clear(); }
inline void set_error(std::string_view text) { error_slot() = std::string(text); }
inline const char* fallback_error() { return error_slot().empty() ? nullptr : error_slot().c_str(); }
inline std::string z(std::string_view v) { return std::string(v); }
inline std::string signature_text(std::string_view methodName, const std::vector<const char*>& parameterTypeNames) { std::string s(methodName); s += "("; for (std::size_t i=0; i<parameterTypeNames.size(); ++i) { if (i) s += ", "; s += parameterTypeNames[i] ? parameterTypeNames[i] : "<unknown>"; } s += ")"; return s; }
inline std::string type_cache_key(std::string_view image, std::string_view namespc, std::string_view name) {
    std::string key(image);
    key.push_back('|');
    key.append(namespc);
    key.push_back('|');
    key.append(name);
    return key;
}
inline std::string member_cache_key(const void* klass, std::string_view name, int argc) {
    std::string key = std::to_string(reinterpret_cast<std::uintptr_t>(klass));
    key.push_back('|');
    key.append(name);
    key.push_back('|');
    key.append(std::to_string(argc));
    return key;
}
inline std::string member_cache_key(const void* klass, std::string_view name, const std::vector<const char*>& parameterTypeNames) {
    std::string key = std::to_string(reinterpret_cast<std::uintptr_t>(klass));
    key.push_back('|');
    key.append(name);
    key.push_back('(');
    for (std::size_t i = 0; i < parameterTypeNames.size(); ++i) {
        if (i) key.push_back(',');
        key.append(parameterTypeNames[i] ? parameterTypeNames[i] : "<unknown>");
    }
    key.push_back(')');
    return key;
}
inline std::string normalized_type_name(std::string_view type) {
    std::string out(type);
    if (out.rfind("class ", 0) == 0) out.erase(0, 6);
    if (out.rfind("struct ", 0) == 0) out.erase(0, 7);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
    if (out == "bool" || out == "boolean" || out == "system.boolean") return "system.boolean";
    if (out == "int" || out == "int32" || out == "system.int32") return "system.int32";
    if (out == "uint" || out == "uint32" || out == "system.uint32") return "system.uint32";
    if (out == "short" || out == "int16" || out == "system.int16") return "system.int16";
    if (out == "ushort" || out == "uint16" || out == "system.uint16") return "system.uint16";
    if (out == "long" || out == "int64" || out == "system.int64") return "system.int64";
    if (out == "ulong" || out == "uint64" || out == "system.uint64") return "system.uint64";
    if (out == "float" || out == "single" || out == "system.single") return "system.single";
    if (out == "double" || out == "system.double") return "system.double";
    if (out == "byte" || out == "system.byte") return "system.byte";
    if (out == "sbyte" || out == "system.sbyte") return "system.sbyte";
    if (out == "char" || out == "system.char") return "system.char";
    if (out == "string" || out == "system.string") return "system.string";
    if (out == "object" || out == "system.object") return "system.object";
    if (out == "type" || out == "system.type") return "system.type";
    if (out == "void" || out == "system.void") return "system.void";
    return out;
}
inline bool type_name_matches(std::string_view actual, const char* requested) { return requested && normalized_type_name(actual) == normalized_type_name(requested); }
struct Backend {
    static bool available() { return URK::managed::available(); }
    static const char* backend_last_error() { return URK::managed::last_error(); }
    static const char* last_error() {
        // Backend diagnostics are copied into the local slot at the failure
        // site. Returning that slot prevents a stale backend error from
        // turning a successful false/null Unity result into a failure.
        thread_local std::string snapshot;
        snapshot = fallback_error() ? fallback_error() : "";
        return snapshot.empty() ? nullptr : snapshot.c_str();
    }
    static const void* find_class(std::string_view image, std::string_view ns, std::string_view name) { auto i=z(image), n=z(ns), c=z(name); return URK::managed::find_class(i.c_str(), n.c_str(), c.c_str()); }
    static const void* object_get_class(void* object) { return object ? URK::managed::object_get_class(static_cast<URK::managed::Object*>(object)) : nullptr; }
    static const char* class_get_name(const void* klass) { return klass ? URK::managed::class_get_name(static_cast<const URK::managed::Class*>(klass)) : nullptr; }
    static const char* class_get_namespace(const void* klass) { return klass ? URK::managed::class_get_namespace(static_cast<const URK::managed::Class*>(klass)) : nullptr; }
    static const void* find_method(const void* klass, std::string_view name, int argc) {
        if (!klass) { set_error("Unity method lookup failed: class is null"); return nullptr; }
        const std::string cacheKey = member_cache_key(klass, name, argc);
        { std::lock_guard<std::mutex> lock(cache_mutex()); const auto found = method_cache().find(cacheKey); if (found != method_cache().end()) return found->second; }
        auto n=z(name); const void* current=klass; std::unordered_set<const void*> parents; std::size_t depth=0;
        while (current) {
            if (depth++ >= 128 || !parents.insert(current).second) { set_error("Unity method lookup rejected a cyclic or excessive inheritance chain"); return nullptr; }
            void* it=nullptr; const void* match=nullptr; int matches=0; std::unordered_set<const void*> members; std::size_t member_count=0;
            while (const auto* m = URK::managed::class_get_methods(static_cast<const URK::managed::Class*>(current), &it)) {
                if (++member_count > 32768 || !members.insert(m).second) { set_error("Unity method lookup rejected a cyclic or excessive method iterator"); return nullptr; }
                const char* mn = URK::managed::method_get_name(m); if (!mn || n != mn) continue;
                if (argc < 0 || static_cast<int>(URK::managed::method_get_param_count(m)) == argc) { match=m; ++matches; }
            }
            if (matches > 1) { set_error(std::string("Unity method lookup failed: ambiguous overload by name/argc: ")+std::string(name)); return nullptr; }
            if (match) { std::lock_guard<std::mutex> lock(cache_mutex()); method_cache()[cacheKey] = match; return match; }
            current = URK::managed::class_get_parent(static_cast<const URK::managed::Class*>(current));
        }
        return nullptr;
    }
    static const void* find_method_exact(const void* klass, std::string_view name, const std::vector<const char*>& parameterTypeNames) {
        if (!klass) { set_error("Unity exact method lookup failed: class is null"); return nullptr; }
        const std::string cacheKey = member_cache_key(klass, name, parameterTypeNames);
        { std::lock_guard<std::mutex> lock(cache_mutex()); const auto found = method_cache().find(cacheKey); if (found != method_cache().end()) return found->second; }
        auto n=z(name); int same_arity=0; std::string first_mismatch; const void* current=klass; std::unordered_set<const void*> parents; std::size_t depth=0;
        while (current) {
            if (depth++ >= 128 || !parents.insert(current).second) { set_error("Unity exact method lookup rejected a cyclic or excessive inheritance chain"); return nullptr; }
            void* it=nullptr; const void* match=nullptr; int matches=0; std::unordered_set<const void*> members; std::size_t member_count=0;
            while (const auto* m = URK::managed::class_get_methods(static_cast<const URK::managed::Class*>(current), &it)) {
                if (++member_count > 32768 || !members.insert(m).second) { set_error("Unity exact method lookup rejected a cyclic or excessive method iterator"); return nullptr; }
                const char* mn = URK::managed::method_get_name(m); if (!mn || n != mn || URK::managed::method_get_param_count(m) != parameterTypeNames.size()) continue; ++same_arity;
                bool ok=true; for (std::uint32_t i=0; i<parameterTypeNames.size(); ++i) { char buf[256]{}; const auto* pt=URK::managed::method_get_param(m,i); const char* want=parameterTypeNames[i]; const bool named=URK::managed::type_get_name(pt, buf, sizeof(buf)); std::string_view got(named ? buf : ""); if (!want || !named || (!type_name_matches(got, want))) { if (first_mismatch.empty()) first_mismatch=std::string("; first mismatch param=")+std::to_string(i)+" requested="+(want?want:"<null>")+" actual="+(got.empty()?"<unavailable>":std::string(got)); ok=false; break; } }
                if (ok) { match=m; ++matches; }
            }
            if (matches > 1) { set_error(std::string("Unity exact method lookup failed: ambiguous exact overload: ")+signature_text(name, parameterTypeNames)); return nullptr; }
            if (match) { std::lock_guard<std::mutex> lock(cache_mutex()); method_cache()[cacheKey] = match; return match; }
            current = URK::managed::class_get_parent(static_cast<const URK::managed::Class*>(current));
        }
        set_error(std::string("Unity exact method lookup failed: no overload matched ")+signature_text(name, parameterTypeNames)+"; same-arity candidates="+std::to_string(same_arity)+first_mismatch);
        return nullptr;
    }
    static int runtime_invoke(const void* method, void* object, void** params, void** result, void** exception) { return URK::managed::runtime_invoke(static_cast<const URK::managed::Method*>(method), static_cast<URK::managed::Object*>(object), params, result, exception); }
    static void* object_unbox(void* object) { return object ? URK::managed::object_unbox(static_cast<URK::managed::Object*>(object)) : nullptr; }
    static void* object_new(const void* klass) { return URK::managed::object_new(static_cast<const URK::managed::Class*>(klass)); }
    static void runtime_object_init(void* object) { URK::managed::runtime_object_init(static_cast<URK::managed::Object*>(object)); }
    static void* new_string(std::string_view text) {
        if (text.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
            set_error("Unity managed string exceeds the runtime API uint32 length range");
            return nullptr;
        }
        return URK::managed::string_new_len(text.data(), static_cast<std::uint32_t>(text.size()));
    }
    static bool string_to_utf8(void* string, char* output, std::size_t outputSize) { return URK::managed::string_to_utf8(static_cast<URK::managed::String*>(string), output, outputSize); }
    static const void* find_field(const void* klass, std::string_view name) {
        const std::string cacheKey = member_cache_key(klass, name, -1);
        { std::lock_guard<std::mutex> lock(cache_mutex()); const auto found = field_cache().find(cacheKey); if (found != field_cache().end()) return found->second; }
        auto n=z(name); const void* current=klass; std::unordered_set<const void*> parents; std::size_t depth=0;
        while (current) {
            if (depth++ >= 128 || !parents.insert(current).second) { set_error("Unity field lookup rejected a cyclic or excessive inheritance chain"); return nullptr; }
            void* it=nullptr; std::unordered_set<const void*> members; std::size_t member_count=0;
            while (const auto* f = URK::managed::class_get_fields(static_cast<const URK::managed::Class*>(current), &it)) { if (++member_count > 32768 || !members.insert(f).second) { set_error("Unity field lookup rejected a cyclic or excessive field iterator"); return nullptr; } const char* fn = URK::managed::field_get_name(f); if (fn && n == fn) { std::lock_guard<std::mutex> lock(cache_mutex()); field_cache()[cacheKey] = f; return f; } }
            current = URK::managed::class_get_parent(static_cast<const URK::managed::Class*>(current));
        }
        return nullptr;
    }
    static bool field_get_value(void* object, const void* field, void* output) { return URK::managed::field_get_value(static_cast<URK::managed::Object*>(object), static_cast<const URK::managed::Field*>(field), output); }
    static void* field_reference_write_pointer(void*& reference) { return reference; }
    static bool field_set_value(void* object, const void* field, void* value) { return URK::managed::field_set_value(static_cast<URK::managed::Object*>(object), static_cast<const URK::managed::Field*>(field), value); }
    static void* field_get_value_object(const void* field, void* object) { return URK::managed::field_get_value_object(field, object); }
    static bool field_static_get_value(const void* klass, const void* field, void* output) { (void)klass; return URK::managed::field_static_get_value(static_cast<const URK::managed::Field*>(field), output); }
    static bool field_static_set_value(const void* klass, const void* field, void* value) { (void)klass; return URK::managed::field_static_set_value(static_cast<const URK::managed::Field*>(field), value); }
    static std::size_t array_length(void* array) { return URK::managed::array_length(static_cast<URK::managed::Array*>(array)); }
    static bool has_array_length() { return URK::managed::has_array_length(); }
    static bool has_array_ref_at() { return URK::managed::has_array_ref_at(); }
    static void* array_ref_at(void* array, std::size_t index) { return URK::managed::array_ref_at(static_cast<URK::managed::Array*>(array), index); }
    static void* array_addr_with_size(void* array, int elementSize, std::size_t index) { return URK::managed::array_addr_with_size(static_cast<URK::managed::Array*>(array), elementSize, index); }
    static std::int32_t class_value_size(const void* klass, std::uint32_t* align) { return URK::managed::class_value_size(klass, align); }
    static bool has_array_set_ref() { return URK::managed::has_array_set_ref(); }
    static bool array_set_ref(void* array, std::size_t index, void* value) { return URK::managed::array_set_ref(static_cast<URK::managed::Array*>(array), index, value); }
    static URK::managed::GCHandle gchandle_new(
        void* object,
        int pinned) {
        return URK::managed::gchandle_new(object, pinned);
    }

    static URK::managed::GCHandle gchandle_new_weakref(
        void* object,
        int trackResurrection) {
        return URK::managed::gchandle_new_weakref(
            object,
            trackResurrection);
    }

    static void* gchandle_get_target(
        URK::managed::GCHandle handle) {
        return URK::managed::gchandle_get_target(handle);
    }

    static void gchandle_free(
        URK::managed::GCHandle handle) {
        URK::managed::gchandle_free(handle);
    }
    static void* type_object_for_class(std::string_view image, std::string_view ns, std::string_view name) { const void* k=find_class(image, ns, name); if (!k) return nullptr; auto* t = URK::managed::class_get_type(static_cast<const URK::managed::Class*>(k)); return t ? URK::managed::type_get_object(t) : nullptr; }
        static void* method_get_object(const void* method, const void* refClass) { return URK::managed::method_get_object(static_cast<const URK::managed::Method*>(method), static_cast<const URK::managed::Class*>(refClass)); }
    static void* value_box(const void* klass, void* data) { return URK::managed::value_box(static_cast<const URK::managed::Class*>(klass), data); }

    static std::int64_t string_length(void* string) { return static_cast<std::int64_t>(URK::managed::string_length(static_cast<URK::managed::String*>(string))); }
};

} // namespace URK::Unity::detail

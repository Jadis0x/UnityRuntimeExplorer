#pragma once

#include "managed_runtime.h"
#include "../hook_api.h"

#if defined(URK_BACKEND_IL2CPP)
#include "../il2cpp/il2cpp_helpers.h"
#endif

namespace URK::managed_hooks {

using DiagnosticSink = void (*)(const char*);

inline void emit(DiagnosticSink sink, const char* message) {
    if (sink)
        sink(message ? message : "");
}

inline bool try_hook_method_pointer(
    const URK::managed::Method* method, void** original, void* detour,
    DiagnosticSink sink = nullptr, const URK_HookOptions* options = nullptr,
    const char* image = nullptr, const char* namespc = nullptr,
    const char* klass = nullptr, const char* method_name = nullptr) {
#if defined(URK_BACKEND_IL2CPP)
    return URK::il2cpp::helpers::try_hook_method_pointer(
        method, original, detour, sink, options, image, namespc, klass, method_name);
#else
    (void)image;
    (void)namespc;
    (void)klass;
    (void)method_name;
    if (!method || !original || !detour) {
        emit(sink, "[URK Mono runtime] hook method/original/detour is null");
        return false;
    }
    void* target = URK::managed::method_pointer(method);
    if (!target) {
        emit(sink, "[URK Mono runtime] method could not be compiled to a native entry point");
        if (const char* error = URK::managed::last_error(); error && error[0])
            emit(sink, error);
        return false;
    }
    *original = target;
    if (!URK::hooks::attach_ex(original, detour, options)) {
        emit(sink, "[URK Mono runtime] native hook attach failed");
        return false;
    }
    return true;
#endif
}

inline bool try_hook_managed_method(
    const char* image, const char* namespc, const char* klass,
    const char* method_name, const char* const* parameter_types,
    int parameter_count, void** original, void* detour,
    DiagnosticSink sink = nullptr, const URK_HookOptions* options = nullptr) {
#if defined(URK_BACKEND_IL2CPP)
    const URK::il2cpp::helpers::ManagedHookSpec spec{
        image, namespc, klass, method_name, parameter_types, parameter_count};
    return URK::il2cpp::helpers::attach_managed(
        spec, original, detour, sink, options);
#else
    const auto* type = URK::managed::find_class(image, namespc, klass);
    if (!type) {
        emit(sink, "[URK managed runtime] hook class lookup failed");
        return false;
    }
    const auto* method = URK::managed::resolve_method_exact(
        type, method_name, parameter_types, parameter_count);
    if (!method) {
        emit(sink, "[URK managed runtime] exact hook method lookup failed");
        return false;
    }
    return try_hook_method_pointer(method, original, detour, sink, options,
                                   image, namespc, klass, method_name);
#endif
}

template <class Fn>
inline bool try_hook_managed_method(
    const char* image, const char* namespc, const char* klass,
    const char* method_name, const char* const* parameter_types,
    int parameter_count, Fn* original, Fn detour,
    DiagnosticSink sink = nullptr, const URK_HookOptions* options = nullptr) {
    return try_hook_managed_method(
        image, namespc, klass, method_name, parameter_types, parameter_count,
        reinterpret_cast<void**>(original), reinterpret_cast<void*>(detour),
        sink, options);
}

} // namespace URK::managed_hooks

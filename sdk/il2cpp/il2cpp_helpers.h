#pragma once

#include "il2cpp_runtime.h"
#include "../hook_api.h"

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
namespace URK::il2cpp::helpers {
using DiagnosticSink = void (*)(const char *);
struct ManagedHookSpec {
  const char *image = nullptr;
  const char *namespc = nullptr;
  const char *klass = nullptr;
  const char *method = nullptr;
  const char *const *parameter_types = nullptr;
  int parameter_count = 0;
};
inline void emit(DiagnosticSink sink, const char *message) {
  if (sink && message)
    sink(message);
}
inline void emit_last_error(DiagnosticSink sink) {
  if (const char *e = URK::il2cpp::last_error(); e && e[0])
    emit(sink, e);
}
inline std::string to_utf8(URK::il2cpp::String *value,
                           std::string_view fallback) {
  if (!value)
    return std::string(fallback);
  const std::int32_t length = URK::il2cpp::string_length(value);
  if (length < 0)
    return std::string(fallback);
  const std::size_t capacity = static_cast<std::size_t>(length) * 4u + 1u;
  std::vector<char> buffer(capacity ? capacity : 1u, '\0');
  if (!URK::il2cpp::string_to_utf8(value, buffer.data(), buffer.size()))
    return std::string(fallback);
  return std::string(buffer.data());
}
inline std::string to_utf8(URK::il2cpp::String *value) {
  return to_utf8(value, std::string_view{});
}
inline ManagedHookSpec managed_hook(const char *image, const char *namespc,
                                    const char *klass, const char *method) {
  return ManagedHookSpec{image, namespc, klass, method, nullptr, 0};
}
template <std::size_t N>
inline ManagedHookSpec managed_hook(const char *image, const char *namespc,
                                    const char *klass, const char *method,
                                    const char *const (&parameter_types)[N]) {
  return ManagedHookSpec{image, namespc, klass, method, parameter_types,
                         static_cast<int>(N)};
}
inline bool ensure_thread_attached(DiagnosticSink sink = nullptr) {
  if (!URK::il2cpp::available()) {
    emit(sink, "[URK IL2CPP runtime] IL2CPP API unavailable; operation "
               "is not requested");
    return false;
  }
  if (URK::il2cpp::thread_current())
    return true;
  if (URK::il2cpp::thread_attach())
    return true;
  emit(sink, "[URK IL2CPP runtime] thread_attach failed; operation is not "
             "requested");
  emit_last_error(sink);
  return false;
}
inline bool is_executable_protection(unsigned long protect) {
  protect &= 0xffu;
  return protect == PAGE_EXECUTE || protect == PAGE_EXECUTE_READ ||
         protect == PAGE_EXECUTE_READWRITE || protect == PAGE_EXECUTE_WRITECOPY;
}
inline bool address_in_module(void *address, const char *module_name) {
  HMODULE module = ::GetModuleHandleA(module_name);
  if (!module || !address)
    return false;
  const auto *base = reinterpret_cast<const unsigned char *>(module);
  const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(base);
  if (dos->e_magic != IMAGE_DOS_SIGNATURE)
    return false;
  const auto *nt =
      reinterpret_cast<const IMAGE_NT_HEADERS *>(base + dos->e_lfanew);
  if (nt->Signature != IMAGE_NT_SIGNATURE)
    return false;
  const auto *ptr = reinterpret_cast<const unsigned char *>(address);
  return ptr >= base && ptr < (base + nt->OptionalHeader.SizeOfImage);
}
inline bool is_valid_icall_target(void *target) {
  if (!target)
    return false;
  MEMORY_BASIC_INFORMATION mbi{};
  if (::VirtualQuery(target, &mbi, sizeof(mbi)) != sizeof(mbi))
    return false;
  if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) ||
      !is_executable_protection(mbi.Protect))
    return false;
  return address_in_module(target, "GameAssembly.dll") ||
         address_in_module(target, "UnityPlayer.dll");
}
inline std::unordered_map<std::string, void *> &icall_cache() {
  static std::unordered_map<std::string, void *> cache;
  return cache;
}
inline std::mutex &icall_cache_mutex() {
  static std::mutex m;
  return m;
}
inline void *try_resolve_icall(const char *name,
                               DiagnosticSink sink = nullptr) {
  if (!name || !name[0]) {
    emit(sink, "[URK IL2CPP runtime] icall name is null/empty; resolve_icall "
               "not requested");
    return nullptr;
  }
  std::lock_guard<std::mutex> lock(icall_cache_mutex());
  auto &cache = icall_cache();
  const auto found = cache.find(name);
  if (found != cache.end())
    return found->second;
  void *target = URK::il2cpp::resolve_icall(name);
  if (!target) {
    char msg[768]{};
    std::snprintf(msg, sizeof(msg),
                  "[URK IL2CPP runtime] resolve_icall failed for: %s", name);
    emit(sink, msg);
    emit_last_error(sink);
    return nullptr;
  }
  if (!is_valid_icall_target(target)) {
    char msg[896]{};
    std::snprintf(msg, sizeof(msg),
                  "[URK IL2CPP runtime] resolve_icall returned an "
                  "invalid/non-executable/non-Unity target for: %s",
                  name);
    emit(sink, msg);
    return nullptr;
  }
  cache.emplace(name, target);
  return target;
}
inline bool try_hook_icall(const char *name, void **original, void *detour,
                           DiagnosticSink sink = nullptr,
                           const URK_HookOptions *options = nullptr) {
  if (!original || !detour) {
    emit(sink, "[URK IL2CPP runtime] icall hook original/detour is null; hook "
               "is not installed");
    return false;
  }
  void *target = try_resolve_icall(name, sink);
  if (!target)
    return false;
  *original = target;
  if (!URK::hooks::attach_ex(original, detour, options)) {
    emit(sink, "[URK IL2CPP runtime] hook backend unavailable or attach "
               "failed; icall hook is not installed");
    return false;
  }
  return true;
}
inline void emit_method_pointer_unavailable(DiagnosticSink sink,
                                            const char *image = nullptr,
                                            const char *namespc = nullptr,
                                            const char *klass = nullptr,
                                            const char *method = nullptr) {
  char msg[768]{};
  std::snprintf(msg, sizeof(msg),
                "[URK IL2CPP runtime] method_pointer unavailable or validation "
                "failed for %s:%s.%s.%s; native method hook is not installed",
                image ? image : "", namespc ? namespc : "", klass ? klass : "",
                method ? method : "");
  emit(sink, msg);
  emit_last_error(sink);
}
inline void *try_method_pointer(const URK::il2cpp::Method *method,
                                DiagnosticSink sink = nullptr,
                                const char *image = nullptr,
                                const char *namespc = nullptr,
                                const char *klass = nullptr,
                                const char *method_name = nullptr) {
  if (!method) {
    emit(sink, "[URK IL2CPP runtime] method handle is null; method_pointer not "
               "requested");
    emit_last_error(sink);
    return nullptr;
  }
  void *target = URK::il2cpp::method_pointer(method);
  if (!target)
    emit_method_pointer_unavailable(sink, image, namespc, klass, method_name);
  return target;
}
inline bool try_method_pointer(const URK::il2cpp::Method *method, void *&out,
                               DiagnosticSink sink = nullptr,
                               const char *image = nullptr,
                               const char *namespc = nullptr,
                               const char *klass = nullptr,
                               const char *method_name = nullptr) {
  out = try_method_pointer(method, sink, image, namespc, klass, method_name);
  return out != nullptr;
}
inline void *require_method_pointer(const URK::il2cpp::Method *method,
                                    DiagnosticSink sink = nullptr,
                                    const char *image = nullptr,
                                    const char *namespc = nullptr,
                                    const char *klass = nullptr,
                                    const char *method_name = nullptr) {
  return try_method_pointer(method, sink, image, namespc, klass, method_name);
}
// try_hook_method_pointer seeds *original with the resolved method_pointer
// target before attaching. On success, the hook backend may replace *original
// with a trampoline. On failure, no hook is installed, but *original may still
// contain the unhooked target pointer.
inline bool try_hook_method_pointer(
    const URK::il2cpp::Method *method, void **original, void *detour,
    DiagnosticSink sink = nullptr, const URK_HookOptions *options = nullptr,
    const char *image = nullptr, const char *namespc = nullptr,
    const char *klass = nullptr, const char *method_name = nullptr) {
  void *target =
      try_method_pointer(method, sink, image, namespc, klass, method_name);
  if (!target)
    return false;
  if (!original || !detour) {
    emit(sink, "[URK IL2CPP runtime] hook original/detour is null; native "
               "method hook is not installed");
    return false;
  }
  *original = target;
  if (!URK::hooks::attach_ex(original, detour, options)) {
    emit(sink, "[URK IL2CPP runtime] hook backend unavailable or attach "
               "failed; native method hook is not installed");
    return false;
  }
  return true;
}
inline bool try_hook_managed_method(
    const URK_Il2CppManagedMethodDesc &desc, void **original, void *detour,
    DiagnosticSink sink = nullptr, const URK_HookOptions *options = nullptr,
    URK_Il2CppManagedHookResult *result = nullptr) {
  URK_Il2CppManagedHookResult local = URK::il2cpp::managed_hook_result();
  URK_Il2CppManagedHookResult *out = result ? result : &local;
  out->size = sizeof(URK_Il2CppManagedHookResult);
  if (!original || !detour) {
    emit(sink, "[URK IL2CPP runtime] managed-method hook original/detour is "
               "null; hook is not installed");
    return false;
  }
  if (URK::il2cpp::attach_managed_method_hook(&desc, original, detour, options,
                                              out) != 0)
    return true;
  emit(sink, out->diagnostic && out->diagnostic[0]
                 ? out->diagnostic
                 : "[URK IL2CPP runtime] managed-method hook was refused or "
                   "methodPointer validation failed; hook is not installed");
  emit_last_error(sink);
  return false;
}
inline bool try_hook_managed_method(
    const char *image, const char *namespc, const char *klass,
    const char *method, const char *const *parameter_types, int parameter_count,
    void **original, void *detour, DiagnosticSink sink = nullptr,
    const URK_HookOptions *options = nullptr,
    URK_Il2CppManagedHookResult *result = nullptr) {
  const URK_Il2CppManagedMethodDesc desc = URK::il2cpp::managed_method_desc(
      image, namespc, klass, method, parameter_types, parameter_count);
  return try_hook_managed_method(desc, original, detour, sink, options, result);
}
inline bool attach_managed(const ManagedHookSpec &spec, void **original,
                           void *detour, DiagnosticSink sink = nullptr,
                           const URK_HookOptions *options = nullptr,
                           URK_Il2CppManagedHookResult *result = nullptr) {
  if (!ensure_thread_attached(sink))
    return false;
  return try_hook_managed_method(spec.image, spec.namespc, spec.klass,
                                 spec.method, spec.parameter_types,
                                 spec.parameter_count, original, detour, sink,
                                 options, result);
}
template <class Fn>
inline bool attach_managed(const ManagedHookSpec &spec, Fn *original, Fn detour,
                           DiagnosticSink sink = nullptr,
                           const URK_HookOptions *options = nullptr,
                           URK_Il2CppManagedHookResult *result = nullptr) {
  return attach_managed(spec, reinterpret_cast<void **>(original),
                        reinterpret_cast<void *>(detour), sink, options,
                        result);
}
inline bool attach_managed(
    const char *image, const char *namespc, const char *klass,
    const char *method, std::initializer_list<const char *> parameter_types,
    void **original, void *detour, DiagnosticSink sink = nullptr,
    const URK_HookOptions *options = nullptr,
    URK_Il2CppManagedHookResult *result = nullptr) {
  std::vector<const char *> params(parameter_types);
  const ManagedHookSpec spec{image, namespc, klass, method, params.data(),
                             static_cast<int>(params.size())};
  return attach_managed(spec, original, detour, sink, options, result);
}
template <class Fn>
inline bool attach_managed(
    const char *image, const char *namespc, const char *klass,
    const char *method, std::initializer_list<const char *> parameter_types,
    Fn *original, Fn detour, DiagnosticSink sink = nullptr,
    const URK_HookOptions *options = nullptr,
    URK_Il2CppManagedHookResult *result = nullptr) {
  return attach_managed(image, namespc, klass, method, parameter_types,
                        reinterpret_cast<void **>(original),
                        reinterpret_cast<void *>(detour), sink, options,
                        result);
}
inline bool require_class(const char *image, const char *namespc,
                          const char *name, const URK::il2cpp::Class *&out,
                          DiagnosticSink sink = nullptr) {
  out = URK::il2cpp::find_class(image, namespc, name);
  if (out)
    return true;
  char msg[640]{};
  std::snprintf(msg, sizeof(msg),
                "[URK IL2CPP runtime] missing class: %s:%s.%s",
                image ? image : "", namespc ? namespc : "", name ? name : "");
  emit(sink, msg);
  emit_last_error(sink);
  return false;
}
inline bool require_method_exact(const char *image, const char *namespc,
                                 const char *klass, const char *name,
                                 const char *const *parameter_types,
                                 int parameter_count, const char *return_type,
                                 const URK::il2cpp::Method *&out,
                                 DiagnosticSink sink = nullptr) {
  out = URK::il2cpp::resolve_method_exact(image, namespc, klass, name,
                                          parameter_types, parameter_count);
  if (out)
    return true;
  char params[384]{};
  std::size_t used = 0;
  for (int i = 0; i < parameter_count && used < sizeof(params); ++i) {
    int w = std::snprintf(
        params + used, sizeof(params) - used, "%s%s", i ? ", " : "",
        parameter_types && parameter_types[i] ? parameter_types[i]
                                              : "<unknown>");
    if (w < 0)
      break;
    used += static_cast<std::size_t>(w);
  }
  char msg[1024]{};
  std::snprintf(
      msg, sizeof(msg),
      "[URK IL2CPP runtime] missing/changed method: %s:%s.%s.%s(%s) -> %s",
      image ? image : "", namespc ? namespc : "", klass ? klass : "",
      name ? name : "", params, return_type ? return_type : "<unknown>");
  emit(sink, msg);
  emit_last_error(sink);
  return false;
}
inline bool require_field(const char *image, const char *namespc,
                          const char *klass, const char *name,
                          const URK::il2cpp::Field *&out,
                          DiagnosticSink sink = nullptr) {
  out = URK::il2cpp::resolve_field(image, namespc, klass, name);
  if (out)
    return true;
  char msg[640]{};
  std::snprintf(msg, sizeof(msg),
                "[URK IL2CPP runtime] missing/changed field: %s:%s.%s.%s",
                image ? image : "", namespc ? namespc : "", klass ? klass : "",
                name ? name : "");
  emit(sink, msg);
  emit_last_error(sink);
  return false;
}
inline bool require_method(const URK::il2cpp::Class *klass, const char *name,
                           int argc, const URK::il2cpp::Method *&out,
                           DiagnosticSink sink = nullptr) {
  out = URK::il2cpp::resolve_method(klass, name, argc);
  if (out)
    return true;
  char msg[512]{};
  std::snprintf(msg, sizeof(msg), "[URK IL2CPP runtime] missing method: %s/%d",
                name ? name : "", argc);
  emit(sink, msg);
  emit_last_error(sink);
  return false;
}
inline bool require_property(const URK::il2cpp::Class *klass, const char *name,
                             const URK::il2cpp::Property *&out,
                             DiagnosticSink sink = nullptr) {
  void *it = nullptr;
  while ((out = URK::il2cpp::class_get_properties(klass, &it)) != nullptr) {
    const char *n = URK::il2cpp::property_get_name(out);
    if (n && name && std::strcmp(n, name) == 0)
      return true;
  }
  char msg[512]{};
  std::snprintf(msg, sizeof(msg), "[URK IL2CPP runtime] missing property: %s",
                name ? name : "");
  emit(sink, msg);
  emit_last_error(sink);
  return false;
}
} // namespace URK::il2cpp::helpers

namespace Il2CppHook {
using DiagnosticSink = URK::il2cpp::helpers::DiagnosticSink;
using ManagedSpec = URK::il2cpp::helpers::ManagedHookSpec;

inline ManagedSpec managed(const char *image, const char *namespc,
                           const char *klass, const char *method) {
  return URK::il2cpp::helpers::managed_hook(image, namespc, klass, method);
}

template <std::size_t N>
inline ManagedSpec managed(const char *image, const char *namespc,
                           const char *klass, const char *method,
                           const char *const (&parameter_types)[N]) {
  return URK::il2cpp::helpers::managed_hook(image, namespc, klass, method,
                                            parameter_types);
}

inline bool attach(const ManagedSpec &spec, void **original, void *detour,
                   DiagnosticSink sink = nullptr,
                   const URK_HookOptions *options = nullptr,
                   URK_Il2CppManagedHookResult *result = nullptr) {
  return URK::il2cpp::helpers::attach_managed(spec, original, detour, sink,
                                              options, result);
}

template <class Fn>
inline bool attach(const ManagedSpec &spec, Fn *original, Fn detour,
                   DiagnosticSink sink = nullptr,
                   const URK_HookOptions *options = nullptr,
                   URK_Il2CppManagedHookResult *result = nullptr) {
  return URK::il2cpp::helpers::attach_managed(spec, original, detour, sink,
                                              options, result);
}

inline bool attach(const char *image, const char *namespc, const char *klass,
                   const char *method,
                   std::initializer_list<const char *> parameter_types,
                   void **original, void *detour,
                   DiagnosticSink sink = nullptr,
                   const URK_HookOptions *options = nullptr,
                   URK_Il2CppManagedHookResult *result = nullptr) {
  return URK::il2cpp::helpers::attach_managed(
      image, namespc, klass, method, parameter_types, original, detour, sink,
      options, result);
}

template <class Fn>
inline bool attach(const char *image, const char *namespc, const char *klass,
                   const char *method,
                   std::initializer_list<const char *> parameter_types,
                   Fn *original, Fn detour, DiagnosticSink sink = nullptr,
                   const URK_HookOptions *options = nullptr,
                   URK_Il2CppManagedHookResult *result = nullptr) {
  return URK::il2cpp::helpers::attach_managed(
      image, namespc, klass, method, parameter_types, original, detour, sink,
      options, result);
}
} // namespace Il2CppHook
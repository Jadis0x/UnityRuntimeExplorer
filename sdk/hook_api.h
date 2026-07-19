#pragma once

#include "runtime_api.h"

#include <cstddef>
#include <cstdint>
#include <utility>

namespace URK::hooks {
inline bool available() {
  const URK::ModContext *ctx = URK::context();
  return URK::has_runtime_capability(URK::runtime_cap_hooks) && ctx &&
         (ctx->HookAttach || ctx->HookAttachEx);
}
inline bool backend_available(URK_HookBackend backend) {
  const URK::ModContext *ctx = URK::context();
  if (!available())
    return false;
  if (backend == URK::hook_backend_auto)
    return true;
  return ctx->HookBackendAvailable
             ? ctx->HookBackendAvailable(static_cast<std::uint32_t>(backend)) !=
                   0
             : false;
}
inline bool attach_ex(void **original, void *detour,
                      const URK_HookOptions *options) {
  const URK::ModContext *ctx = URK::context();
  if (!original || !detour || !available())
    return false;
  const bool has_options = options != nullptr;
  if (has_options && options->size < sizeof(URK_HookOptions))
    return false;
  const auto backend = has_options
                           ? static_cast<URK_HookBackend>(options->backend)
                           : URK::hook_backend_auto;
  if (!backend_available(backend))
    return false;
  if (ctx->HookAttachEx)
    return ctx->HookAttachEx(original, detour, options) != 0;
  return backend == URK::hook_backend_auto && ctx->HookAttach
             ? ctx->HookAttach(original, detour) != 0
             : false;
}
inline bool attach_ex(void **original, void *detour, URK_HookBackend backend) {
  URK_HookOptions options{};
  options.size = sizeof(options);
  options.backend = static_cast<std::uint32_t>(backend);
  return attach_ex(original, detour, &options);
}
inline bool attach(void **original, void *detour) {
  return attach_ex(original, detour,
                   static_cast<const URK_HookOptions *>(nullptr));
}
inline bool detach_ex(void **original, void *detour) {
  const URK::ModContext *ctx = URK::context();
  if (!original || !detour ||
      !URK::has_runtime_capability(URK::runtime_cap_hooks) || !ctx)
    return false;
  if (ctx->HookDetachEx)
    return ctx->HookDetachEx(original, detour) != 0;
  return ctx->HookDetach ? ctx->HookDetach(original, detour) != 0 : false;
}
inline bool detach(void **original, void *detour) {
  return detach_ex(original, detour);
}
template <class T> inline T as(void *value) {
  return reinterpret_cast<T>(value);
}

class HookSet {
public:
  static constexpr std::size_t max_entries = 64;
  template <class T> bool add(T *original, T detour) {
    return add_raw(reinterpret_cast<void **>(original),
                   reinterpret_cast<void *>(detour));
  }
  template <class T> bool add(T *original, T detour, URK_HookBackend backend) {
    return add_raw(reinterpret_cast<void **>(original),
                   reinterpret_cast<void *>(detour), backend);
  }
  bool add_raw(void **original, void *detour) {
    if (full() || !original || !detour || !URK::hooks::attach(original, detour))
      return false;
    entries_[count_++] = {original, detour};
    return true;
  }
  bool add_raw(void **original, void *detour, const URK_HookOptions *options) {
    if (full() || !original || !detour ||
        !URK::hooks::attach_ex(original, detour, options))
      return false;
    entries_[count_++] = {original, detour};
    return true;
  }
  bool add_raw(void **original, void *detour, URK_HookBackend backend) {
    if (full() || !original || !detour ||
        !URK::hooks::attach_ex(original, detour, backend))
      return false;
    entries_[count_++] = {original, detour};
    return true;
  }
  void detach_all() {
    while (count_ > 0) {
      auto entry = entries_[--count_];
      detach(entry.original, entry.detour);
    }
  }
  std::size_t size() const { return count_; }
  constexpr std::size_t capacity() const { return max_entries; }
  bool full() const { return count_ >= max_entries; }

private:
  struct Entry {
    void **original;
    void *detour;
  };
  Entry entries_[max_entries]{};
  std::size_t count_ = 0;
};
} // namespace URK::hooks
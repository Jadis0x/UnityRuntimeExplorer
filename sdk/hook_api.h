#pragma once

#include "runtime_api.h"

#include <cstddef>
#include <cstdint>
#include <utility>

namespace URK::hooks {
inline bool available() {
    const URK::ModContext *ctx = URK::context();
    return URK::has_runtime_capability(URK::runtime_cap_hooks) && ctx && (ctx->HookAttach || ctx->HookAttachEx);
}
inline bool backend_available(URK_HookBackend backend) {
    const URK::ModContext *ctx = URK::context();
    if (!available())
        return false;
    if (backend == URK::hook_backend_auto)
        return true;
    return ctx->HookBackendAvailable ? ctx->HookBackendAvailable(static_cast<std::uint32_t>(backend)) != 0 : false;
}
inline bool attach_ex(void **original, void *detour, const URK_HookOptions *options) {
    const URK::ModContext *ctx = URK::context();
    if (!original || !detour || !available())
        return false;
    const bool has_options = options != nullptr;
    if (has_options && options->size < sizeof(URK_HookOptions))
        return false;
    const auto backend = has_options ? static_cast<URK_HookBackend>(options->backend) : URK::hook_backend_auto;
    if (!backend_available(backend))
        return false;
    if (ctx->HookAttachEx)
        return ctx->HookAttachEx(original, detour, options) != 0;
    return backend == URK::hook_backend_auto && ctx->HookAttach ? ctx->HookAttach(original, detour) != 0 : false;
}
inline bool attach_ex(void **original, void *detour, URK_HookBackend backend) {
    URK_HookOptions options{};
    options.size = sizeof(options);
    options.backend = static_cast<std::uint32_t>(backend);
    return attach_ex(original, detour, &options);
}
inline bool attach(void **original, void *detour) {
    return attach_ex(original, detour, static_cast<const URK_HookOptions *>(nullptr));
}
inline bool detach_ex(void **original, void *detour) {
    const URK::ModContext *ctx = URK::context();
    if (!original || !detour || !URK::has_runtime_capability(URK::runtime_cap_hooks) || !ctx)
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

inline bool mid_available() {
    const URK::ModContext *ctx = URK::context();
    return ctx && URK::has_runtime_capability(URK::runtime_cap_mid_hooks) &&
           ctx->size >= offsetof(URK::ModContext, hooks) + sizeof(ctx->hooks) && ctx->hooks &&
           ctx->hooks->size >= offsetof(URK::HookApi, mid_attach) + sizeof(ctx->hooks->mid_attach) &&
           ctx->hooks->mid_attach;
}
inline URK::MidHookHandle *mid_attach(void *target, URK::MidHookCallbackFn callback, void *user_data = nullptr) {
    if (!target || !callback || !mid_available())
        return nullptr;
    URK_MidHookOptions options{};
    options.size = sizeof(options);
    options.flags = 0;
    options.userData = user_data;
    return URK::context()->hooks->mid_attach(target, callback, &options);
}
inline bool mid_detach(URK::MidHookHandle *hook) {
    const URK::ModContext *ctx = URK::context();
    if (!hook || !mid_available() ||
        ctx->hooks->size < offsetof(URK::HookApi, mid_detach) + sizeof(ctx->hooks->mid_detach) ||
        !ctx->hooks->mid_detach)
        return false;
    return ctx->hooks->mid_detach(hook) != 0;
}
inline bool mid_set_enabled(URK::MidHookHandle *hook, bool enabled) {
    const URK::ModContext *ctx = URK::context();
    if (!hook || !mid_available() ||
        ctx->hooks->size < offsetof(URK::HookApi, mid_set_enabled) + sizeof(ctx->hooks->mid_set_enabled) ||
        !ctx->hooks->mid_set_enabled)
        return false;
    return ctx->hooks->mid_set_enabled(hook, enabled ? 1 : 0) != 0;
}

class MidHook {
  public:
    MidHook() = default;
    MidHook(const MidHook &) = delete;
    MidHook &operator=(const MidHook &) = delete;
    MidHook(MidHook &&other) noexcept : handle_(other.handle_) {
        other.handle_ = nullptr;
    }
    MidHook &operator=(MidHook &&other) noexcept {
        if (this != &other) {
            reset();
            handle_ = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }
    ~MidHook() {
        reset();
    }
    bool attach(void *target, URK::MidHookCallbackFn callback, void *user_data = nullptr) {
        reset();
        handle_ = URK::hooks::mid_attach(target, callback, user_data);
        return handle_ != nullptr;
    }
    bool set_enabled(bool enabled) {
        return handle_ && URK::hooks::mid_set_enabled(handle_, enabled);
    }
    void reset() {
        if (handle_) {
            URK::hooks::mid_detach(handle_);
            handle_ = nullptr;
        }
    }
    bool valid() const {
        return handle_ != nullptr;
    }
    explicit operator bool() const {
        return valid();
    }

  private:
    URK::MidHookHandle *handle_ = nullptr;
};

class HookSet {
  public:
    static constexpr std::size_t max_entries = 64;
    template <class T> bool add(T *original, T detour) {
        return add_raw(reinterpret_cast<void **>(original), reinterpret_cast<void *>(detour));
    }
    template <class T> bool add(T *original, T detour, URK_HookBackend backend) {
        return add_raw(reinterpret_cast<void **>(original), reinterpret_cast<void *>(detour), backend);
    }
    bool add_raw(void **original, void *detour) {
        if (full() || !original || !detour || !URK::hooks::attach(original, detour))
            return false;
        entries_[count_++] = {original, detour};
        return true;
    }
    bool add_raw(void **original, void *detour, const URK_HookOptions *options) {
        if (full() || !original || !detour || !URK::hooks::attach_ex(original, detour, options))
            return false;
        entries_[count_++] = {original, detour};
        return true;
    }
    bool add_raw(void **original, void *detour, URK_HookBackend backend) {
        if (full() || !original || !detour || !URK::hooks::attach_ex(original, detour, backend))
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
    std::size_t size() const {
        return count_;
    }
    constexpr std::size_t capacity() const {
        return max_entries;
    }
    bool full() const {
        return count_ >= max_entries;
    }

  private:
    struct Entry {
        void **original;
        void *detour;
    };
    Entry entries_[max_entries]{};
    std::size_t count_ = 0;
};
} // namespace URK::hooks

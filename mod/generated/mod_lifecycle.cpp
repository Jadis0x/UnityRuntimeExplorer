// Copyright (c) 2026 Jadis0x. All rights reserved.
#include "mod_lifecycle.h"

#include "lifecycle/mod_runtime.h"
#include "hooks/mod_hooks.h"
#include "support/mod_log.h"

#include "lifecycle/mod_network.h"

#include "sdk/mod_async.h"

#include <cstddef>
#include <utility>

#include "sdk/runtime_api.h"
#include "sdk/il2cpp/il2cpp_runtime.h"

namespace {
const URK_ModContext* g_ctx = nullptr;
bool g_update_registered = false;
bool g_network_initialized = false;
bool g_runtime_started = false;
bool g_initialized = false;

URK::coroutines::FlowState g_coroutines{};

constexpr std::size_t field_end(std::size_t offset, std::size_t size) {
  return offset + size;
}

bool context_has_field(const URK_ModContext* ctx, std::size_t end) {
  return ctx && ctx->size >= end;
}

void runtime_update() {
  URK::coroutines::tick(g_coroutines);
  ModRuntime::update();
}

bool validate_required_backend(const URK_ModContext* ctx) {
  if (!ctx) return false;
  if (ctx->version < URK_SDK_VERSION) {
    ModLog::error("mod context SDK version is too old: got=%d required>=%d", ctx->version, URK_SDK_VERSION);
    return false;
  }
  if (ctx->size < sizeof(URK_ModContext)) {
    ModLog::error("mod context table is too small: got=%u required>=%zu", ctx->size, sizeof(URK_ModContext));
    return false;
  }
  if (!context_has_field(ctx, field_end(offsetof(URK_ModContext, runtimeBackend), sizeof(ctx->runtimeBackend)))) {
    ModLog::error("mod context is too small for runtimeBackend; refusing to initialize");
    return false;
  }
  if (ctx->runtimeBackend != URK::runtime_backend_il2cpp) {
    ModLog::error("required backend mismatch: generated for IL2CPP but loader runtimeBackend=%u; refusing to initialize", ctx->runtimeBackend);
    return false;
  }
  if (!context_has_field(ctx, field_end(offsetof(URK_ModContext, runtimeCapabilities), sizeof(ctx->runtimeCapabilities)))) {
    ModLog::error("mod context is too small for runtimeCapabilities; refusing to initialize");
    return false;
  }
  if ((ctx->runtimeCapabilities & URK::runtime_cap_il2cpp_api) == 0) {
    ModLog::error("required IL2CPP API capability is missing; refusing to initialize");
    return false;
  }
  if (!context_has_field(ctx, field_end(offsetof(URK_ModContext, il2cpp), sizeof(ctx->il2cpp)))) {
    ModLog::error("mod context is too small for IL2CPP API table; refusing to initialize");
    return false;
  }
  if (!ctx->il2cpp) {
    ModLog::error("required IL2CPP API table is missing; refusing to initialize");
    return false;
  }
  if (ctx->il2cpp->version < URK_IL2CPP_API_VERSION || ctx->il2cpp->size < sizeof(*ctx->il2cpp)) {
    ModLog::error("required IL2CPP API table is incompatible: version=%d size=%u", ctx->il2cpp->version, ctx->il2cpp->size);
    return false;
  }
  return true;
}
} // namespace

namespace ModAsync {
URK::coroutines::FlowState& flow() { return g_coroutines; }
void spawn(URK::coroutines::Task task) {
  URK::coroutines::spawn(g_coroutines, std::move(task));
}
void cancel_all() { URK::coroutines::cancel_all(g_coroutines); }
} // namespace ModAsync

namespace ModLifecycle {
bool initialize(const URK_ModContext* ctx) {
  if (!ctx) return false;
  if (g_initialized) {
    ModLog::error("ModInitEx was invoked more than once; refusing duplicate initialization");
    return false;
  }
  g_ctx = ctx;
  ModLog::initialize(ctx);
  if (!validate_required_backend(ctx)) { g_ctx = nullptr; ModLog::shutdown(); return false; }

  URK::set_context(ctx);
  if (!ModNetwork::init(ctx)) { URK::set_context(nullptr); g_ctx = nullptr; ModLog::shutdown(); return false; }
  g_network_initialized = true;
  if (!ModRuntime::start(ctx)) { ModNetwork::shutdown(); g_network_initialized = false; URK::set_context(nullptr); g_ctx = nullptr; ModLog::shutdown(); return false; }
  g_runtime_started = true;
  if (!ModHooks::install(ctx)) { ModRuntime::stop(); g_runtime_started = false; ModNetwork::shutdown(); g_network_initialized = false; URK::set_context(nullptr); g_ctx = nullptr; ModLog::shutdown(); return false; }

  const bool can_register_update = context_has_field(ctx, field_end(offsetof(URK_ModContext, MainThreadRegister), sizeof(ctx->MainThreadRegister))) && ctx->MainThreadRegister;
  if (can_register_update) {
    g_update_registered = ctx->MainThreadRegister(&runtime_update) != 0;
    if (!g_update_registered)
      ModLog::warn("main-thread update registration failed");
  } else {
    ModLog::warn("main-thread dispatcher unavailable; update() will not be called automatically");
  }

  g_initialized = true;
  ModLog::info("mod initialized");
  return true;
}

void on_scene_loaded(const URK_SceneInfo* scene) {
  ModRuntime::on_scene_loaded(scene);
}

void on_scene_changed(const URK_SceneInfo* previousScene,
                      const URK_SceneInfo* currentScene) {
  ModRuntime::on_scene_changed(previousScene, currentScene);
}

void on_object_destroy_requested(const URK_ObjectDestroyRequest* request) {
  ModRuntime::on_object_destroy_requested(request);
}

void shutdown() {
  if (!g_initialized && !g_runtime_started && !g_network_initialized) return;
  if (g_update_registered && g_ctx && context_has_field(g_ctx, field_end(offsetof(URK_ModContext, MainThreadUnregister), sizeof(g_ctx->MainThreadUnregister))) && g_ctx->MainThreadUnregister) {
    g_ctx->MainThreadUnregister(&runtime_update);
  }
  g_update_registered = false;

  ModAsync::cancel_all();
  ModHooks::uninstall();
  if (g_runtime_started) ModRuntime::stop();
  if (g_network_initialized) ModNetwork::shutdown();
  g_runtime_started = false;
  g_network_initialized = false;
  g_initialized = false;
  ModLog::info("mod shutdown");
  ModLog::shutdown();
  URK::set_context(nullptr);
  g_ctx = nullptr;
}
} // namespace ModLifecycle

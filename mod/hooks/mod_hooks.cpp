// Copyright (c) 2026 Jadis0x. All rights reserved.
#include "mod_hooks.h"

#include "config/mod_config.h"
#include "explorer/method_tracer.h"
#include "support/mod_log.h"

#include "sdk/runtime_api.h"
#include "sdk/hook_api.h"
#include "render_imgui_hook.h"
#include "unity_log_hook.h"
#include "sdk/il2cpp/il2cpp_runtime.h"
#include "sdk/il2cpp/il2cpp_helpers.h"

namespace {
URK::hooks::HookSet g_hooks;
} // namespace

namespace ModHooks {
bool install(const URK_ModContext* ctx) {
  URK::set_context(ctx);
  URK::il2cpp::init(ctx);

  if (!URK::hooks::available()) {
    ModLog::warn("hook API is unavailable; no hooks were installed");
    return true;
  }

  // Register validated targets with g_hooks so uninstall() can detach them.
  if (ModConfig::enable_unity_log_hook && !ModUnityLogHook::install(ctx))
    ModLog::warn("Unity log hooks were requested but not installed");
  if (!ModRenderHook::install(ctx))
    ModLog::warn("ImGui render hook could not be installed; continuing without menu");

  ModLog::info("hook registry ready; ImGui render hook initialization requested");
  return true;
}

void uninstall() {
  Explorer::MethodTracer::shutdown();
  ModUnityLogHook::uninstall();
  if (!ModRenderHook::uninstall())
    ModLog::error("ImGui render hook shutdown was incomplete; see hook diagnostics");
  g_hooks.detach_all();
}
} // namespace ModHooks

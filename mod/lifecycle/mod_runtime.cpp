// Copyright (c) 2026 Jadis0x. All rights reserved.
#include "mod_runtime.h"

#include "explorer/explorer_model.h"
#include "support/mod_log.h"

#include "sdk/runtime_api.h"
#include "sdk/runtime_bootstrap.h"
#include "sdk/unity/unity.h"

#include <utility>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#endif

namespace ModRuntime {
#if defined(_WIN32)
namespace {
struct RuntimeFaultRecord {
  std::uint32_t code = 0;
  std::uintptr_t address = 0;
  std::uintptr_t instruction = 0;
};
thread_local RuntimeFaultRecord g_runtime_fault{};

int capture_runtime_fault(void* raw_info) {
  auto* info = static_cast<EXCEPTION_POINTERS*>(raw_info);
  if (!info || !info->ExceptionRecord) return 0;
  const DWORD code = info->ExceptionRecord->ExceptionCode;
  if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_IN_PAGE_ERROR) return 0;
  g_runtime_fault.code = code;
  g_runtime_fault.instruction = reinterpret_cast<std::uintptr_t>(info->ExceptionRecord->ExceptionAddress);
  g_runtime_fault.address = info->ExceptionRecord->NumberParameters >= 2
                                ? static_cast<std::uintptr_t>(info->ExceptionRecord->ExceptionInformation[1])
                                : 0;
  return 1;
}
} // namespace
#endif

bool start(const URK_ModContext* ctx) {
  URK::set_context(ctx);
  if (!URK::initialize_backend(ctx)) {
    ModLog::error("IL2CPP runtime API initialization failed");
    return false;
  }

  Explorer::RuntimeModel::instance().start();
  ModLog::info("runtime ready: backend=IL2CPP main_thread=%s scene_events=%s", URK::has_main_thread() ? "yes" : "no", URK::has_scene_events() ? "yes" : "no");
  return true;
}

void update() {
#if defined(_WIN32)
  // URKit unregisters a main-thread callback when an exception escapes it.
  // Keep a malformed third-party IL2CPP metadata record isolated to this
  // update; RuntimeModel will discard stale reflection state next frame.
  __try {
    Explorer::RuntimeModel::instance().tick();
  } __except (capture_runtime_fault(_exception_info())) {
    Explorer::RuntimeModel::instance().notify_native_fault(
        g_runtime_fault.code, g_runtime_fault.address, g_runtime_fault.instruction);
    ModLog::error("Explorer update caught a native access fault; recovery scheduled: code=0x%08X address=%p instruction=%p",
                  g_runtime_fault.code, reinterpret_cast<void*>(g_runtime_fault.address),
                  reinterpret_cast<void*>(g_runtime_fault.instruction));
  }
#else
  Explorer::RuntimeModel::instance().tick();
#endif
}

void on_scene_loaded(const URK_SceneInfo* scene) {
  if (!scene || scene->size < sizeof(URK_SceneInfo)) return;
  ModLog::info("scene loaded: name=%s buildIndex=%d handle=%d", scene->name, scene->buildIndex, scene->handle);
  Explorer::Command command{};
  command.kind = Explorer::CommandKind::SceneHint;
  command.int_value = scene->handle;
  command.text = scene->name;
  Explorer::RuntimeModel::instance().enqueue(std::move(command));
}

void on_scene_changed(const URK_SceneInfo* previousScene, const URK_SceneInfo* currentScene) {
  if (!previousScene || !currentScene || previousScene->size < sizeof(URK_SceneInfo) || currentScene->size < sizeof(URK_SceneInfo)) return;
  ModLog::info("scene changed: %s -> %s", previousScene->name, currentScene->name);
  Explorer::Command command{};
  command.kind = Explorer::CommandKind::SceneHint;
  command.int_value = currentScene->handle;
  command.text = currentScene->name;
  Explorer::RuntimeModel::instance().enqueue(std::move(command));
}

void on_object_destroy_requested(const URK_ObjectDestroyRequest* request) {
  if (!request || request->size < sizeof(URK_ObjectDestroyRequest)) return;
  // A map unload can emit thousands of these. The model coalesces them into a
  // single refresh after the destruction burst has settled.
  Explorer::Command command{};
  command.kind = Explorer::CommandKind::ObjectDestroyRequested;
  command.instance_id = request->instanceId;
  Explorer::RuntimeModel::instance().enqueue(std::move(command));
}

void stop() {
  Explorer::RuntimeModel::instance().stop();
  ModLog::info("runtime stopped");
}
} // namespace ModRuntime

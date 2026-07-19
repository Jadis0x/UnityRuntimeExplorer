// Copyright (c) 2026 Jadis0x. All rights reserved.
#include "mod_runtime.h"

#include "explorer/explorer_model.h"
#include "support/mod_log.h"

#include "sdk/runtime_api.h"
#include "sdk/runtime_bootstrap.h"
#include "sdk/unity/unity.h"

#include <utility>

namespace ModRuntime {
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
  } __except (1) {
    Explorer::RuntimeModel::instance().notify_native_fault();
    ModLog::error("Explorer update caught a native access violation; recovery scheduled");
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

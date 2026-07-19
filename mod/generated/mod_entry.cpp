// Copyright (c) 2026 Jadis0x. All rights reserved.
#include "mod_lifecycle.h"

#include "config/mod_config.h"

#if defined(_WIN32)
#define URK_EXPORT __declspec(dllexport)
#else
#define URK_EXPORT __attribute__((visibility("default")))
#endif

extern "C" URK_EXPORT const URK_ModInfo* URKGetModInfo() {
  static const URK_ModInfo info{
      ModConfig::project_name,
      ModConfig::display_name,
      ModConfig::author,
      ModConfig::version,
      ModConfig::url,
      ModConfig::description,
  };
  return &info;
}

extern "C" URK_EXPORT int ModInitEx(const URK_ModContext* ctx) {
  return ModLifecycle::initialize(ctx) ? 1 : 0;
}

extern "C" URK_EXPORT void OnSceneLoaded(const URK_SceneInfo* scene) {
  ModLifecycle::on_scene_loaded(scene);
}

extern "C" URK_EXPORT void OnSceneChanged(const URK_SceneInfo* previousScene,
                                          const URK_SceneInfo* currentScene) {
  ModLifecycle::on_scene_changed(previousScene, currentScene);
}

extern "C" URK_EXPORT void OnObjectDestroyRequested(
    const URK_ObjectDestroyRequest* request) {
  ModLifecycle::on_object_destroy_requested(request);
}

extern "C" URK_EXPORT void ModShutdown() {
  ModLifecycle::shutdown();
}

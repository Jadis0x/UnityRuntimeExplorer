// Copyright (c) 2026 Jadis0x. All rights reserved.
#pragma once

#include "sdk/mod_sdk.h"

namespace ModLifecycle {
bool initialize(const URK_ModContext* ctx);
void on_scene_loaded(const URK_SceneInfo* scene);
void on_scene_changed(const URK_SceneInfo* previousScene,
                      const URK_SceneInfo* currentScene);
void on_object_destroy_requested(const URK_ObjectDestroyRequest* request);
void shutdown();
} // namespace ModLifecycle

// Copyright (c) 2026 Jadis0x. All rights reserved.
#include "explorer_model.h"
#include "model_shared.h"
#include "support/mod_log.h"

#include <chrono>
#include <string>
#include <string_view>

using namespace URK::Unity;

namespace Explorer {

	void RuntimeModel::load_scene(int build_index, std::string_view scene_key) {
		clear_error();
		ModLog::info("load scene requested: build_index=%d key=\"%.*s\" routes kept=[%s]", build_index,
			static_cast<int>(scene_key.size()), scene_key.data(),
			SceneManager::AvailableLoadSceneRoutes().c_str());
		const Scene before = SceneManager::GetActiveScene();
		const int previous_build_index = before && before.IsValid() ? before.buildIndex() : -1;
		const std::string previous_name = before && before.IsValid() ? before.name() : std::string{};
		if (const char* error = last_error(); error && error[0]) {
			ModLog::error("load scene failed before dispatch: could not query the active scene: %s", error);
			set_status(std::string("Load scene failed before dispatch: could not query the active scene: ") + error);
			return;
		}
		if (!scene_key.empty()) {
			if (!SceneManager::HasLoadSceneRoute(true)) {
				const std::string kept = SceneManager::AvailableLoadSceneRoutes();
				ModLog::error("load scene failed: no by-name overload survived stripping; kept=[%s]", kept.c_str());
				set_status(kept.empty()
					? "Load scene failed: this build kept no SceneManager overload that loads a scene"
					: "Load scene failed: no overload takes a scene name here. This build kept: " + kept);
				return;
			}
			clear_error();
			if (!SceneManager::LoadSceneUsingAnyRoute(true, scene_key, -1)) {
				const char* error = last_error();
				ModLog::error("load scene by name failed: %s", error && error[0] ? error : "the runtime refused the call");
				set_status(std::string("Load scene failed: ") + (error && error[0] ? error : "the runtime refused the call"));
				return;
			}
			ModLog::info("load scene dispatched by name: \"%.*s\"", static_cast<int>(scene_key.size()), scene_key.data());
			pending_scene_load_ = { true, -1, previous_build_index, std::string(scene_key), previous_name, Clock::now() };
			set_status("Load request dispatched for " + std::string(scene_key) + "; waiting for scene activation");
			request_refresh();
			return;
		}

		const int scene_count = SceneManager::sceneCountInBuildSettings();
		if (build_index < 0 || build_index >= scene_count) {
			ModLog::error("load scene failed: build index %d is outside the %d scene(s) in build settings",
					build_index, scene_count);
			set_status("Load scene failed: enter a scene path or name, or select a current build scene");
			return;
		}
		if (!SceneManager::HasLoadSceneRoute(false)) {
			ModLog::error("load scene failed: no by-index overload survived stripping; kept=[%s]",
					SceneManager::AvailableLoadSceneRoutes().c_str());
			set_status("No overload takes a build index here. Enter the scene path or name in the manual loader.");
			return;
		}
		clear_error();
		if (!SceneManager::LoadSceneUsingAnyRoute(false, {}, build_index)) {
			const char* error = last_error();
			ModLog::error("load scene by build index failed: %s",
				error && error[0] ? error : "the runtime refused the call");
			set_status(std::string("Load scene failed: ") + (error && error[0] ? error : "the runtime refused the call"));
			return;
		}
		ModLog::info("load scene dispatched by build index: %d", build_index);
		pending_scene_load_ = { true, build_index, previous_build_index, {}, previous_name, Clock::now() };
		set_status("Load request dispatched for build scene " + std::to_string(build_index) + "; waiting for scene activation");
		request_refresh();
	}

	void RuntimeModel::update_pending_scene_load() {
		if (!pending_scene_load_.active)
			return;
		clear_error();
		const Scene active = SceneManager::GetActiveScene();
		const char* error = last_error();
		if (active && active.IsValid()) {
			const int active_index = active.buildIndex();
			const std::string active_name = active.name();
			const bool index_match = pending_scene_load_.build_index >= 0 && active_index == pending_scene_load_.build_index;
			const bool name_match = !pending_scene_load_.key.empty() &&
				(active_name == pending_scene_load_.key ||
				 active_name == scene_display_name(pending_scene_load_.key, -1));
			if (index_match || name_match) {
				const bool unchanged = active_index == pending_scene_load_.previous_build_index &&
					active_name == pending_scene_load_.previous_name;
				set_status(unchanged ? "Scene load request targeted the already active scene: " + active_name
					: "Scene activated: " + active_name + " (build index " + std::to_string(active_index) + ")");
				pending_scene_load_ = {};
				request_refresh();
				return;
			}
		}
		if (Clock::now() - pending_scene_load_.requested < std::chrono::seconds(5))
			return;
		const std::string requested = pending_scene_load_.build_index >= 0
			? "build scene " + std::to_string(pending_scene_load_.build_index)
			: pending_scene_load_.key;
		const std::string active_name = active && active.IsValid() ? active.name() : "<unavailable>";
		// The dispatch succeeded and the scene still did not change, which is
		// what a game that drives its own scene flow does with an outside
		// LoadScene call. Worth a log line: the status bar is one corner of one
		// frame, and this is the failure people report as "nothing happened".
		ModLog::warn("load scene timed out: %s was not activated within 5s; active scene is %s%s%s",
			requested.c_str(), active_name.c_str(), error && error[0] ? "; " : "", error && error[0] ? error : "");
		set_status("Load scene failed: " + requested + " was not activated within 5 seconds; active scene is " +
			active_name + (error && error[0] ? std::string("; ") + error : std::string{}));
		pending_scene_load_ = {};
	}

} // namespace Explorer

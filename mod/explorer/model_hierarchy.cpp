// Copyright (c) 2026 Jadis0x. All rights reserved.
#include "explorer_model.h"
#include "model_shared.h"

#include "support/mod_log.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace URK::Unity;

namespace Explorer {
	namespace {

		constexpr int kMaxSceneCount = 128;
		constexpr int kMaxBuildSceneCount = 4096;
		constexpr int kMaxHierarchyDepth = 256;
		constexpr std::size_t kMaxCensusCandidates = 250000;
		constexpr std::size_t kMinCensusObjectsPerSlice = 8;
		constexpr int kHideAndDontSaveMask = 1 | 4 | 8 | 16 | 32;
		constexpr TypeRef kSceneType{ "", "UnityEngine.SceneManagement", "Scene" };

		// FlatObject and RuntimeModel::HierarchyCensus are defined in model_shared.h:
		// the shell (explorer_model.cpp) also needs the complete census type to
		// reset/inspect the unique_ptr<HierarchyCensus> member.

		void capture_discovery_signature(GameObject object, FlatObject& record) {
			clear_error();
			const auto components = object.GetComponentsRooted<Object>();
			if (const char* error = last_error(); error && error[0]) {
				record.discovery_signature_complete = false;
				clear_error();
				return;
			}
			if (components.empty())
				record.discovery_signature_complete = false;

			for (const Object& component : components) {
				if (!component) {
					record.discovery_signature_complete = false;
					continue;
				}
				const auto* klass = static_cast<const URK::managed::Class*>(
					URK::managed::object_get_class(static_cast<URK::managed::Object*>(component.handle())));
				if (!klass) {
					record.discovery_signature_complete = false;
					continue;
				}
				const Inspect::TypeInfo type = Inspect::DescribeClass(klass);
				std::string type_name = type.full_name;
				if (type_name.empty())
					type_name = type.name;
				if (type_name.empty()) {
					record.discovery_signature_complete = false;
					continue;
				}
				record.component_types.push_back(type_name);

				const std::string normalized = normalized_type(type_name);
				if (normalized.find("dynamicmonobehaviour") == std::string::npos &&
					normalized.find("dynamicbehaviour") == std::string::npos &&
					normalized.find("dynamicbehavior") == std::string::npos)
					continue;

				// DynamicMonoBehaviour exposes this serialized type discriminator as
				// a field. Reading it does not invoke game code and avoids enumerating
				// complete reflection metadata for every bridge instance in the census.
				clear_error();
				const std::string behaviour_type = component.GetField<std::string>("BehaviourType");
				if (last_error() && last_error()[0])
					record.discovery_signature_complete = false;
				else if (!behaviour_type.empty())
					record.dynamic_behaviour_types.push_back(behaviour_type);
				clear_error();
			}

			std::sort(record.component_types.begin(), record.component_types.end());
			record.component_types.erase(
				std::unique(record.component_types.begin(), record.component_types.end()),
				record.component_types.end());
			std::sort(record.dynamic_behaviour_types.begin(), record.dynamic_behaviour_types.end());
			record.dynamic_behaviour_types.erase(
				std::unique(record.dynamic_behaviour_types.begin(), record.dynamic_behaviour_types.end()),
				record.dynamic_behaviour_types.end());
		}

		std::size_t count_nodes(const HierarchyNode& node) {
			std::size_t count = node.instance_id != 0 ? 1 : 0;
			for (const HierarchyNode& child : node.children)
				count += count_nodes(child);
			return count;
		}

		int boxed_scene_handle(Scene scene) {
			if (!scene || !scene.handle())
				return 0;

			// Read m_Handle directly; Scene methods require an unboxed value.
			clear_error();
			const int handle = Object{ scene.handle() }.GetField<int>("m_Handle");
			return last_error() ? 0 : handle;
		}

		std::string scene_name_from_handle(int handle) {
			if (handle == 0)
				return {};
			clear_error();
			return detail::InvokeStatic<std::string>(kSceneType, "GetNameInternal", handle);
		}

		bool is_hide_and_dont_save(GameObject object) {
			const int flags = object.hideFlags();
			return (flags & kHideAndDontSaveMask) == kHideAndDontSaveMask;
		}

	} // namespace

	bool RuntimeModel::refresh_hierarchy() {
		const bool starting = !hierarchy_census_;
		if (starting)
			hierarchy_census_ = std::make_unique<HierarchyCensus>();
		HierarchyCensus& state = *hierarchy_census_;
		HierarchyInfo& next = state.next;

		if (starting) {
			state.started = Clock::now();
			state.scene_generation = scene_generation_;
			next.revision = next_hierarchy_revision_++;
			next.scene_generation = scene_generation_;
			next.source = "Event-driven Resources census / native adjacency / frame-budgeted";
			hierarchy_instance_ids_.clear();

			const Scene active_scene = SceneManager::GetActiveScene();
			int active_handle = boxed_scene_handle(active_scene);
			if (active_handle == 0)
				active_handle = active_scene_handle_hint_;

			const int build_scene_count =
				std::clamp(SceneManager::sceneCountInBuildSettings(), 0, kMaxBuildSceneCount);
			const bool scene_utility_available = SceneUtility::available();
			clear_error();
			next.available_scenes.reserve(static_cast<std::size_t>(build_scene_count));
			for (int build_index = 0; build_index < build_scene_count; ++build_index) {
				SceneLoadInfo scene{};
				scene.build_index = build_index;
				if (scene_utility_available)
					scene.path = SceneUtility::GetScenePathByBuildIndex(build_index);
				clear_error();
				scene.name = scene_display_name(scene.path, build_index);
				next.available_scenes.push_back(std::move(scene));
			}

			const int scene_count = std::clamp(SceneManager::sceneCount(), 0, kMaxSceneCount);
			for (int index = 0; index < scene_count; ++index) {
				const Scene scene = SceneManager::GetSceneAt(index);
				const int build_index = scene.buildIndex();
				if (build_index >= 0 && static_cast<std::size_t>(build_index) < next.available_scenes.size()) {
					next.available_scenes[static_cast<std::size_t>(build_index)].loaded = scene.isLoaded();
					next.available_scenes[static_cast<std::size_t>(build_index)].active = scene.handle_value() == active_handle;
					const std::string loaded_name = scene.name();
					if (!loaded_name.empty()) {
						next.available_scenes[static_cast<std::size_t>(build_index)].name = loaded_name;
						if (next.available_scenes[static_cast<std::size_t>(build_index)].path.empty())
							next.available_scenes[static_cast<std::size_t>(build_index)].path = loaded_name;
					}
				}
				const int handle = boxed_scene_handle(scene);
				if (handle == 0 || state.loaded_scene_indices.contains(handle))
					continue;
				SceneNode node{};
				node.handle = handle;
				node.name = scene_name_from_handle(handle);
				if (node.name.empty() && handle == active_scene_handle_hint_)
					node.name = active_scene_name_hint_;
				if (node.name.empty())
					node.name = "Scene " + std::to_string(handle);
				node.active = handle == active_handle;
				state.loaded_scene_indices.emplace(handle, next.scenes.size());
				next.scenes.push_back(std::move(node));
			}

			if (active_handle != 0 && !state.loaded_scene_indices.contains(active_handle)) {
				SceneNode node{};
				node.handle = active_handle;
				node.name = scene_name_from_handle(active_handle);
				if (node.name.empty())
					node.name = active_scene_name_hint_;
				if (node.name.empty())
					node.name = "Active Scene";
				node.active = true;
				next.scenes.insert(next.scenes.begin(), std::move(node));
				state.loaded_scene_indices.clear();
				for (std::size_t index = 0; index < next.scenes.size(); ++index)
					state.loaded_scene_indices[next.scenes[index].handle] = index;
			}

			state.ddol_index = next.scenes.size();
			SceneNode ddol{};
			ddol.name = "DontDestroyOnLoad";
			ddol.dont_destroy_on_load = true;
			next.scenes.push_back(std::move(ddol));

			state.hidden_index = next.scenes.size();
			SceneNode hidden{};
			hidden.name = "HideAndDontSave";
			hidden.hide_and_dont_save = true;
			next.scenes.push_back(std::move(hidden));

			state.candidates = Object::FindObjectsOfTypeAllRooted<GameObject>();
			state.candidate_count = std::min(state.candidates.size(), kMaxCensusCandidates);
			ModLog::info("Hierarchy census started: generation=%llu candidates=%zu budget_ms=2",
				static_cast<unsigned long long>(state.scene_generation), state.candidate_count);
			if (state.candidates.size() > kMaxCensusCandidates)
				next.source += " (candidate cap reached)";
			state.flat.reserve(state.candidate_count);
			state.flat_indices.reserve(state.candidate_count);
		}

		if (state.scene_generation != scene_generation_) {
			hierarchy_census_.reset();
			request_refresh();
			return false;
		}

		auto& flat = state.flat;
		auto& flat_indices = state.flat_indices;
		const Clock::time_point slice_started = Clock::now();
		const Clock::time_point deadline = slice_started + std::chrono::milliseconds(2);
		std::size_t processed_this_slice = 0;

		// Build parent-child relations natively after one managed census.
		for (; state.candidate_index < state.candidate_count; ++state.candidate_index) {
			if (processed_this_slice >= kMinCensusObjectsPerSlice && Clock::now() >= deadline) {
				state.max_slice = std::max(state.max_slice,
					std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - slice_started));
				return false;
			}
			++processed_this_slice;
			const GameObject& object = state.candidates[state.candidate_index];
			if (!object)
				continue;
			const int instance_id = object.GetInstanceID();
			if (instance_id == 0 || flat_indices.contains(instance_id))
				continue;
			const Transform transform = object.transform();
			if (!transform)
				continue;
			const Transform parent = transform.parent();
			int parent_id = 0;
			if (parent) {
				const GameObject parent_object = parent.gameObject();
				if (parent_object)
					parent_id = parent_object.GetInstanceID();
			}

			FlatObject record{};
			record.object = object;
			record.instance_id = instance_id;
			record.parent_id = parent_id;
			record.name = object.name();
			if (record.name.empty())
				record.name = "<unnamed>";
			record.tag = object.tag();
			record.active = object.activeSelf();
			capture_discovery_signature(object, record);
			flat_indices.emplace(instance_id, flat.size());
			flat.push_back(std::move(record));
		}
		state.max_slice = std::max(state.max_slice,
			std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - slice_started));

		// Wait for the complete, budgeted census before rebuilding the hierarchy.
		// Rebuilding partial results duplicates roots in busy scenes.
		if (state.candidate_index < state.candidate_count)
			return false;

		const std::size_t ddol_index = state.ddol_index;
		const std::size_t hidden_index = state.hidden_index;
		auto& loaded_scene_indices = state.loaded_scene_indices;

		std::vector<std::vector<std::size_t>> children(flat.size());
		std::vector<std::size_t> root_indices;
		root_indices.reserve(flat.size());
		for (std::size_t index = 0; index < flat.size(); ++index) {
			const auto parent = flat_indices.find(flat[index].parent_id);
			if (flat[index].parent_id != 0 && parent != flat_indices.end())
				children[parent->second].push_back(index);
			else
				root_indices.push_back(index);
		}

		std::unordered_set<int> visited;
		visited.reserve(flat.size());
		std::function<HierarchyNode(std::size_t, int)> build_node = [&](std::size_t index, int depth) -> HierarchyNode {
			if (index >= flat.size() || depth > kMaxHierarchyDepth)
				return {};
			const FlatObject& source = flat[index];
			if (!visited.insert(source.instance_id).second)
				return {};
			hierarchy_instance_ids_.insert(source.instance_id);
			HierarchyNode node{};
			node.instance_id = source.instance_id;
			node.object_address = reinterpret_cast<std::uintptr_t>(source.object.handle());
			node.name = source.name;
			node.tag = source.tag;
			node.pointer_text = pointer_text(source.object.handle());
			node.active = source.active;
			node.component_types = source.component_types;
			node.dynamic_behaviour_types = source.dynamic_behaviour_types;
			node.discovery_signature_complete = source.discovery_signature_complete;
			if (!source.discovery_signature_complete)
				++next.discovery_signature_failures;
			node.children.reserve(children[index].size());
			for (const std::size_t child_index : children[index]) {
				HierarchyNode child = build_node(child_index, depth + 1);
				if (child.instance_id != 0)
					node.children.push_back(std::move(child));
			}
			return node;
			};

		for (const std::size_t root_index : root_indices) {
			const FlatObject& root = flat[root_index];
			std::size_t group_index = ddol_index;
			if (is_hide_and_dont_save(root.object)) {
				group_index = hidden_index;
			}
			else {
				const int scene_handle = boxed_scene_handle(root.object.scene());
				if (scene_handle == 0)
					continue; // prefab/asset GameObject, not a live hierarchy object
				if (const auto loaded = loaded_scene_indices.find(scene_handle); loaded != loaded_scene_indices.end()) {
					group_index = loaded->second;
				}
			}

			HierarchyNode node = build_node(root_index, 0);
			if (node.instance_id != 0)
				next.scenes[group_index].roots.push_back(std::move(node));
		}

		for (const SceneNode& scene : next.scenes) {
			next.roots += scene.roots.size();
			for (const HierarchyNode& root : scene.roots)
				next.objects += count_nodes(root);
		}

		if (next.objects == 0) {
			const char* error = last_error();
			set_status(error && error[0] ? std::string("Hierarchy discovery failed: ") + error
				: "No live scene GameObjects were discovered");
		}

		const std::string signature = next.source + "|" + std::to_string(next.scenes.size()) + "|" +
			std::to_string(next.roots) + "|" + std::to_string(next.objects) + "|" +
			std::to_string(next.discovery_signature_failures);
		if (signature != logged_hierarchy_signature_) {
			logged_hierarchy_signature_ = signature;
			ModLog::info("hierarchy refreshed: source=%s scenes=%zu roots=%zu objects=%zu signatures_incomplete=%zu",
				next.source.c_str(), next.scenes.size(), next.roots, next.objects,
				next.discovery_signature_failures);
		}

		const auto census_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - state.started);
		ModLog::info("hierarchy census timing: candidates=%zu total_ms=%lld max_slice_us=%lld",
			state.candidate_count, static_cast<long long>(census_elapsed.count()),
			static_cast<long long>(state.max_slice.count()));
		hierarchy_ = std::make_shared<const HierarchyInfo>(std::move(next));
		working_.hierarchy = hierarchy_;

		if (working_.selected_instance_id != 0) {
			// Keep the rooted selection; census wrappers expire with their array.
			if (!hierarchy_instance_ids_.contains(working_.selected_instance_id) || !resolve_selected_object())
				clear_selection();
		}

		hierarchy_census_.reset();
		return true;
	}

} // namespace Explorer

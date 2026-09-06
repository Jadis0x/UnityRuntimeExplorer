// Copyright (c) 2026 Jadis0x. All rights reserved.
#include "explorer_model.h"
#include "model_shared.h"

#include "support/mod_log.h"
#include "ui/highlight.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

using namespace URK::Unity;

namespace Explorer {
	namespace {

		constexpr std::size_t kArrayPageSize = 128;
		// Bound native byte snapshots before decoding.
		constexpr std::size_t kMaxByteArrayInspectionBytes = 256 * 1024;
		constexpr std::size_t kLiveByteArrayRefreshLimit = 4096;
		constexpr std::size_t kMaxHighlightRenderers = 48;

		bool is_transform_component(std::string_view name) {
			return name == "UnityEngine.Transform" || name == "UnityEngine.RectTransform" || name == "Transform" ||
				name == "RectTransform";
		}

		std::string lowercase_ascii(std::string_view text) {
			std::string result(text);
			std::transform(result.begin(), result.end(), result.begin(), [](unsigned char value) {
				return static_cast<char>(std::tolower(value));
			});
			return result;
		}

		std::string assembly_name(const Inspect::TypeInfo& type) {
			if (!type.handle)
				return {};
			const char* name = URK::managed::class_get_assemblyname(type.handle);
			return name ? name : "";
		}

	} // namespace

	void RuntimeModel::refresh_inspector(bool include_components) {
		selected_ = resolve_selected_object();
		if (!safe_object_alive(selected_)) {
			if (working_.selected_instance_id != 0)
				clear_selection();
			return;
		}

		InspectorInfo info = working_.inspector;
		const int instance_id = selected_.GetInstanceID();
		if (!info.valid || info.instance_id != instance_id)
			include_components = true;

		info.valid = true;
		info.instance_id = instance_id;
		info.pointer_text = pointer_text(selected_.handle());
		info.active = selected_.activeSelf();

		if (include_components) {
			info.name = selected_.name();
			info.tag = selected_.tag();
			info.layer = selected_.GetProperty<int>("layer");
			info.is_static = selected_.GetProperty<bool>("isStatic");
			const Inspect::TypeInfo object_type = safe_type_of(Object{ selected_.handle() });
			info.type_name = object_type.full_name.empty() ? "UnityEngine.GameObject" : object_type.full_name;
			info.assembly_name = assembly_name(object_type);
			info.namespace_name = object_type.namespc;
			info.class_name = object_type.name;
			info.components.clear();
			info.component_query_error.clear();
			clear_component_cache();
			const auto components = selected_.GetComponentsRooted<Component>();
			if (const char* error = last_error(); error && error[0]) {
				info.component_query_error = error;
				const std::string signature = std::to_string(instance_id) + "|" + info.component_query_error;
				if (signature != logged_component_query_error_) {
					logged_component_query_error_ = signature;
					ModLog::warn("component query failed: gameObject=%s id=%d error=%s", info.name.c_str(),
						instance_id, info.component_query_error.c_str());
				}
				clear_error();
			}
			else {
				logged_component_query_error_.clear();
			}
			for (const Component& candidate : components) {
				if (!candidate)
					continue;
				Inspect::ObjectHandle handle = Inspect::PinObject(Object{ candidate.handle() });
				const Object rooted = Inspect::ResolveObjectHandle(handle);
				const Component component{ rooted.handle() };
				if (!handle.handle || !safe_object_alive(component)) {
					Inspect::FreeObjectHandle(handle);
					continue;
				}
				ComponentInfo component_info{};
				component_info.instance_id = component.GetInstanceID();
				if (component_info.instance_id == 0) {
					Inspect::FreeObjectHandle(handle);
					continue;
				}
				const Inspect::TypeInfo component_type = safe_type_of(Object{ component.handle() });
				component_info.type_name = component_type.full_name;
				component_info.assembly_name = assembly_name(component_type);
				component_info.namespace_name = component_type.namespc;
				component_info.class_name = component_type.name;
				component_info.pointer_text = pointer_text(component.handle());
				if (component_info.type_name.empty())
					component_info.type_name = safe_runtime_class_name(component);
				if (component_info.type_name.empty())
					component_info.type_name = "<missing component>";
				if (!is_transform_component(component_info.type_name)) {
					clear_error();
					bool enabled = false;
#if defined(_WIN32)
					__try {
						enabled = component.GetProperty<bool>("enabled");
					}
					__except (capture_native_fault(_exception_info())) {
						// Record native faults from malformed third-party metadata.
						clear_error();
						ModLog::warn("Component enabled probe blocked a native access violation: id=%d type=%s",
							component_info.instance_id, component_info.type_name.c_str());
					}
#else
					enabled = component.GetProperty<bool>("enabled");
#endif
					if (!last_error()) {
						component_info.enabled_supported = true;
						component_info.enabled = enabled;
					}
					clear_error();
					component_handles_[component_info.instance_id] = handle;
					info.components.push_back(std::move(component_info));
				}
				else {
					Inspect::FreeObjectHandle(handle);
				}
			}
			// Prefer renderers nearest to the selected transform.
			struct HighlightRendererCandidate {
				Renderer renderer;
				float distance_squared = 0.0f;
			};
			std::vector<HighlightRendererCandidate> candidates;
			candidates.reserve(kMaxHighlightRenderers);
			const Transform highlight_transform = selected_.transform();
			const Vector3 highlight_position = highlight_transform ? highlight_transform.position() : Vector3{};
			const auto renderers = selected_.GetComponentsInChildrenRooted<Renderer>(true);
			for (const Renderer& renderer : renderers) {
				if (!safe_object_alive(renderer))
					continue;
				const Transform renderer_transform = renderer.transform();
				const Vector3 renderer_position = renderer_transform ? renderer_transform.position() : highlight_position;
				const float dx = renderer_position.x - highlight_position.x;
				const float dy = renderer_position.y - highlight_position.y;
				const float dz = renderer_position.z - highlight_position.z;
				const HighlightRendererCandidate candidate{ renderer, dx * dx + dy * dy + dz * dz };
				if (candidates.size() < kMaxHighlightRenderers) {
					candidates.push_back(candidate);
					continue;
				}
				const auto farthest = std::max_element(candidates.begin(), candidates.end(),
					[](const HighlightRendererCandidate& left, const HighlightRendererCandidate& right) {
						return left.distance_squared < right.distance_squared;
					});
				if (candidate.distance_squared < farthest->distance_squared)
					*farthest = candidate;
			}
			std::sort(candidates.begin(), candidates.end(),
				[](const HighlightRendererCandidate& left, const HighlightRendererCandidate& right) {
					return left.distance_squared < right.distance_squared;
				});
			clear_highlight_renderer_cache();
			highlight_renderers_.reserve(candidates.size());
			for (const HighlightRendererCandidate& candidate : candidates) {
				Inspect::ObjectHandle handle = Inspect::WeakObject(Object{ candidate.renderer.handle() });
				if (handle.handle)
					highlight_renderers_.push_back(handle);
			}
		}

		const Transform transform = selected_.transform();
		info.camera_distance_valid = false;
		if (transform) {
			info.local_position = transform.localPosition();
			info.local_rotation = transform.GetProperty<Vector3>("localEulerAngles");
			info.local_scale = transform.localScale();
			Camera distance_camera = Camera::main();
			if (!safe_object_alive(distance_camera) || !distance_camera.enabled())
				distance_camera = Camera::current();
			const Transform camera_transform = distance_camera ? distance_camera.transform() : Transform{};
			if (safe_object_alive(camera_transform)) {
				const float distance = Vector3::distance(transform.position(), camera_transform.position());
				if (std::isfinite(distance)) {
					info.camera_distance = distance;
					info.camera_distance_valid = true;
				}
			}
		}
		working_.inspector = std::move(info);
	}

	void RuntimeModel::clear_component_cache() {
		for (auto& [_, handle] : component_handles_)
			if (handle.handle) {
				Inspect::FreeObjectHandle(handle);
			}
		component_handles_.clear();
		for (auto it = reference_handles_.begin(); it != reference_handles_.end();) {
			if ((it->first & 0xF000000000000000ull) == 0xb000000000000000ull) {
				++it;
				continue;
			}
			Inspect::FreeObjectHandle(it->second);
			it = reference_handles_.erase(it);
		}
		working_.method_results.clear();
		working_.member_write_results.clear();
		component_reflection_.clear();
		sampled_component_members_.clear();
	}

	void RuntimeModel::clear_object_inspector() {
		if (object_inspector_handle_.handle) {
			Inspect::FreeObjectHandle(object_inspector_handle_);
		}
		object_inspector_handle_ = {};
		for (auto& [_, handle] : object_inspector_history_)
			Inspect::FreeObjectHandle(handle);
		object_inspector_history_.clear();
		object_inspector_reflection_ = {};
		sampled_object_fields_.clear();
		sampled_object_properties_.clear();
		for (auto it = reference_handles_.begin(); it != reference_handles_.end();) {
			const std::uint64_t scope = it->first & 0xF000000000000000ull;
			if (scope != 0x3000000000000000ull && scope != 0x4000000000000000ull && scope != 0x5000000000000000ull) {
				++it;
				continue;
			}
			if (it->second.handle)
				Inspect::FreeObjectHandle(it->second);
			it = reference_handles_.erase(it);
		}
		for (auto it = working_.method_results.begin(); it != working_.method_results.end();) {
			if (it->second.object_inspector_token == 0) {
				++it;
				continue;
			}
			if (it->second.reference.token != 0)
				release_reference_handle(it->second.reference.token);
			it = working_.method_results.erase(it);
		}
		clear_locked_members(true);
		working_.object_inspector = {};
	}

	void RuntimeModel::close_object_inspector_tab(std::uint64_t token) {
		if (token == 0)
			return;

		if (const auto history = object_inspector_history_.find(token); history != object_inspector_history_.end()) {
			if (history->second.handle)
				Inspect::FreeObjectHandle(history->second);
			object_inspector_history_.erase(history);
		}

		for (auto it = working_.method_results.begin(); it != working_.method_results.end();) {
			if (it->second.object_inspector_token != token) {
				++it;
				continue;
			}
			if (it->second.reference.token != 0)
				release_reference_handle(it->second.reference.token);
			it = working_.method_results.erase(it);
		}

		if (!working_.object_inspector.valid || working_.object_inspector.token != token)
			return;

		if (object_inspector_handle_.handle)
			Inspect::FreeObjectHandle(object_inspector_handle_);
		object_inspector_handle_ = {};
		object_inspector_reflection_ = {};
		sampled_object_fields_.clear();
		sampled_object_properties_.clear();
		for (auto it = reference_handles_.begin(); it != reference_handles_.end();) {
			const std::uint64_t scope = it->first & 0xF000000000000000ull;
			if (scope != 0x3000000000000000ull && scope != 0x4000000000000000ull && scope != 0x5000000000000000ull) {
				++it;
				continue;
			}
			if (it->second.handle)
				Inspect::FreeObjectHandle(it->second);
			it = reference_handles_.erase(it);
		}
		clear_locked_members(true);
		working_.object_inspector = {};
		set_status("Object Inspector tab closed");
	}

	void RuntimeModel::release_reference_handle(std::uint64_t token) {
		const auto found = reference_handles_.find(token);
		if (found == reference_handles_.end())
			return;
		if (found->second.handle) {
			Inspect::FreeObjectHandle(found->second);
		}
		reference_handles_.erase(found);
	}

	void RuntimeModel::load_component_metadata(int component_instance_id) {
		const Object target = resolve_component(component_instance_id);
		if (!safe_object_alive(target) || target.GetInstanceID() != component_instance_id) {
			set_status("Component is no longer available");
			refresh_inspector(true);
			return;
		}

		auto component = std::find_if(
			working_.inspector.components.begin(), working_.inspector.components.end(),
			[component_instance_id](const ComponentInfo& info) { return info.instance_id == component_instance_id; });
		if (component == working_.inspector.components.end() ||
			(component->metadata && !component->metadata_unavailable))
			return;
		record_flight("TARGET", "Load component metadata", component->type_name + " #" + std::to_string(component_instance_id));
		const auto fail_metadata = [&](std::string message) {
			component_reflection_.erase(component_instance_id);
			component->metadata = std::make_shared<ComponentInfo::Metadata>();
			component->metadata_unavailable = true;
			component->metadata_error = std::move(message);
			set_status("Component metadata load failed for " + component->type_name + ": " + component->metadata_error);
			};

		const auto* klass = static_cast<const URK::managed::Class*>(
			URK::managed::object_get_class(static_cast<URK::managed::Object*>(target.handle())));
		if (!klass) {
			fail_metadata("runtime class is unavailable");
			return;
		}
		const Inspect::TypeInfo current_type = Inspect::DescribeClass(klass);
		if (!current_type.handle || current_type.full_name.empty()) {
			fail_metadata("runtime class identity is unavailable");
			return;
		}
		if (!component->type_name.empty() && component->type_name != current_type.full_name) {
			set_status("Component changed while metadata was loading; inspector refreshed");
			refresh_inspector(true);
			return;
		}

		auto metadata = std::make_shared<ComponentInfo::Metadata>();
		ComponentReflection reflection{};
		std::vector<std::string> metadata_warnings;
		const auto enumerate = [&](std::string_view section, auto&& action) {
			active_metadata_stage_ = std::string(section);
			clear_error();
			action();
			if (const char* error = last_error(); error && error[0])
				metadata_warnings.push_back(std::string(section) + ": " + error);
			};
		// Capture one class identity for the complete component snapshot.
		enumerate("Fields", [&] { reflection.fields = Inspect::fields_from_class(klass, true); });
		enumerate("Properties", [&] { reflection.properties = Inspect::properties_from_class(klass, true); });
		enumerate("Methods", [&] { reflection.methods = Inspect::methods_from_class(klass, true); });
		active_metadata_stage_.clear();

		component->dynamic_bridge = {};
		const std::string bridge_type = normalized_type(component->type_name);
		if (bridge_type.find("dynamicmonobehaviour") != std::string::npos ||
			bridge_type.find("dynamicbehaviour") != std::string::npos ||
			bridge_type.find("dynamicbehavior") != std::string::npos)
			component->dynamic_bridge.detected = true;
		for (std::size_t method_index = 0; method_index < reflection.methods.size(); ++method_index) {
			const Inspect::MethodInfo& method = reflection.methods[method_index];
			const std::string method_name = lowercase_ascii(method.name);
			const bool parameterless_instance = !method.is_static && method.parameters.empty();
			if (parameterless_instance && normalized_type(method.return_type) == "system.string" &&
				(method_name == "getbehaviourtype" || method_name == "getbehaviortype" ||
					method_name == "getscripttype" ||
					(method_name.find("behaviour") != std::string::npos && method_name.find("type") != std::string::npos) ||
					(method_name.find("behavior") != std::string::npos && method_name.find("type") != std::string::npos))) {
				component->dynamic_bridge.detected = true;
				component->dynamic_bridge.type_getter = method.name + "()";
				component->dynamic_bridge.type_getter_method_index = static_cast<int>(method_index);
			}
			const bool serialized_data_method = method_name.find("serialized") != std::string::npos &&
				(method_name.find("data") != std::string::npos || method_name.find("payload") != std::string::npos);
			if (serialized_data_method) {
				component->dynamic_bridge.detected = true;
				component->dynamic_bridge.serialized_data_method_indices.push_back(static_cast<int>(method_index));
			}
			const bool object_reference_method = !method.is_static &&
				(method_name == "getobject" || method_name == "getobjectreference" ||
					method_name == "getreferencedobject" || method_name == "getgameobject");
			if (object_reference_method) {
				component->dynamic_bridge.detected = true;
				component->dynamic_bridge.object_reference_method_indices.push_back(static_cast<int>(method_index));
			}
		}

		metadata->fields.reserve(reflection.fields.size());
		for (const Inspect::FieldInfo& field : reflection.fields)
			metadata->fields.push_back(field_metadata(field));

		metadata->properties.reserve(reflection.properties.size());
		for (const Inspect::PropertyInfo& property : reflection.properties) {
			metadata->properties.push_back({ property.name, property.type_name, property.declaring_type.full_name,
									 property.can_read, property.can_write, {}, property.is_value_type, property.is_enum,
									 !property.type_is_opaque,
									 property.type_is_opaque ? "Runtime-specific type; metadata is available but generic read/write is unsafe." : "" });
		}

		metadata->methods.reserve(reflection.methods.size());
		for (const Inspect::MethodInfo& method : reflection.methods) {
			// IL2CPP supplies stable native method pointers for this optional
			// caller index. Mono deliberately skips this passive step because JIT
			// compilation is not safe for every metadata method.
			remember_managed_method(method);
			ComponentInfo::Method member{};
			member.name = method.name;
			member.return_type = method.return_type;
			member.declaring_type = method.declaring_type.full_name;
			member.is_static = method.is_static;
			member.return_is_value_type = method.return_is_value_type;
			member.return_is_enum = method.return_is_enum;
			member.uses_generic_parameter = method.return_type_is_generic_parameter ||
				std::any_of(method.parameters.begin(), method.parameters.end(), [](const Inspect::MethodParamInfo& parameter) {
					return parameter.is_generic_parameter;
				});
			member.runtime_callable = (!method.return_type_is_opaque || method.return_type_is_generic_parameter) &&
				std::none_of(method.parameters.begin(), method.parameters.end(), [](const Inspect::MethodParamInfo& parameter) {
					return parameter.is_opaque;
					});
			if (!member.runtime_callable)
				member.capability_reason = "Runtime-specific by-ref/internal signature; generic invocation is disabled.";
			member.parameter_types.reserve(method.parameters.size());
			member.parameter_names.reserve(method.parameters.size());
			for (const Inspect::MethodParamInfo& parameter : method.parameters) {
				member.parameter_types.push_back(parameter.type_name);
				member.parameter_names.push_back(parameter.name);
				member.parameter_is_value_types.push_back(parameter.is_value_type);
				member.parameter_is_enums.push_back(parameter.is_enum);
			}
			metadata->methods.push_back(std::move(member));
		}

		// The UI consumes immutable data captured on the game thread.
		component_reflection_[component_instance_id] = std::move(reflection);
		component->metadata = std::move(metadata);
		component->metadata_unavailable = false;
		component->metadata_error.clear();
		if (!metadata_warnings.empty()) {
			for (std::size_t index = 0; index < metadata_warnings.size(); ++index) {
				if (index)
					component->metadata_error += " | ";
				component->metadata_error += metadata_warnings[index];
			}
		}
		refresh_live_member_values();
		if (component->metadata_error.empty())
			set_status("Loaded all metadata for " + component->type_name);
		else
			set_status("Loaded metadata for " + component->type_name + " with explicit member diagnostics");
	}

	void RuntimeModel::write_back_value_type_object_inspector() {
		const ObjectInspectorInfo& inspector = working_.object_inspector;
		const Object parent = resolve_component(inspector.value_origin_component_id);
		const Object boxed = Inspect::ResolveObjectHandle(object_inspector_handle_);
		const auto reflection = component_reflection_.find(inspector.value_origin_component_id);
		if (!parent || !boxed || reflection == component_reflection_.end() || inspector.value_origin_member_index < 0) {
			set_status("Value-type owner is no longer available");
			return;
		}
		bool written = false;
#if defined(_WIN32)
		__try {
#endif
			if (inspector.value_origin_property) {
				const std::size_t index = static_cast<std::size_t>(inspector.value_origin_member_index);
				if (index < reflection->second.properties.size())
					written = Inspect::SetPropertyFromBox(parent, reflection->second.properties[index], boxed);
			}
			else {
				const std::size_t index = static_cast<std::size_t>(inspector.value_origin_member_index);
				if (index < reflection->second.fields.size())
					written = Inspect::SetFieldFromBox(parent, reflection->second.fields[index], boxed);
			}
#if defined(_WIN32)
		}
		__except (capture_native_fault(_exception_info())) {
			clear_error();
			set_status("Value-type write-back blocked an invalid native access");
			return;
		}
#endif
		if (written)
			set_status("Applied value-type change to owner");
		else
			capture_last_error("Apply value-type change");
	}

	void RuntimeModel::inspect_raw_reference(std::uint64_t address) {
		if (address == 0) {
			set_status("Cannot inspect a null reference");
			return;
		}
		Inspect::ValueInfo value{};
		value.kind = Inspect::ValueKind::ObjectReference;
		value.object = reinterpret_cast<void*>(static_cast<std::uintptr_t>(address));
		const ComponentInfo::LiveValues::Reference reference = watch_reference_for(value);
		if (reference.is_null || reference.token == 0) {
			set_status("That native address is not a live managed object");
			return;
		}
		inspect_reference(reference.token);
	}

	void RuntimeModel::inspect_reference(std::uint64_t token) {
		const auto retained = object_inspector_history_.find(token);
		auto reference = reference_handles_.find(token);
		const auto browser_reference = class_browser_handles_.find(token);
		const auto static_reference = class_browser_static_handles_.find(token);
		const auto graph_reference = reference_graph_handles_.find(token);
		if (retained == object_inspector_history_.end() && reference == reference_handles_.end() &&
			browser_reference == class_browser_handles_.end() && static_reference == class_browser_static_handles_.end() &&
			graph_reference == reference_graph_handles_.end()) {
			std::string error;
			const Object saved = managed_references_.object(token, error);
			if (!saved) {
				set_status(error.empty() ? "Referenced object is no longer available" : error);
				return;
			}
			Inspect::ObjectHandle tracked = Inspect::WeakObject(saved);
			if (!tracked.handle) {
				set_status("Saved reference could not be opened in the Object Inspector");
				return;
			}
			reference_handles_[token] = tracked;
			reference = reference_handles_.find(token);
		}
		const Inspect::ObjectHandle& source = retained != object_inspector_history_.end() ? retained->second
			: reference != reference_handles_.end() ? reference->second
			: browser_reference != class_browser_handles_.end()
			? browser_reference->second
			: static_reference != class_browser_static_handles_.end() ? static_reference->second
			: graph_reference->second;
		const Object object = Inspect::ResolveObjectHandle(source);
		if (!object) {
			set_status("Referenced object was released");
			return;
		}
		const Inspect::TypeInfo type = safe_type_of(object);
		if (type.full_name == "UnityEngine.GameObject") {
			// Route GameObjects through the hierarchy-aware Inspector.
			Inspect::ObjectHandle selection_root = Inspect::PinObject(object);
			if (!selection_root.handle) {
				set_status("Could not root referenced GameObject");
				return;
			}
			const GameObject game_object{ object.handle() };
			select_object(game_object, selection_root);
			if (selected_.handle() == object.handle()) {
				refresh_inspector(true);
				update_highlight();
				set_status("Selected referenced GameObject");
			}
			return;
		}
		Inspect::ObjectHandle next_handle = type.is_value_type ? Inspect::PinObject(object) : Inspect::WeakObject(object);
		if (!next_handle.handle) {
			set_status("Could not track referenced object");
			return;
		}
		if (retained == object_inspector_history_.end()) {
			Inspect::ObjectHandle history_handle =
				type.is_value_type ? Inspect::PinObject(object) : Inspect::WeakObject(object);
			if (!history_handle.handle) {
				Inspect::FreeObjectHandle(next_handle);
				set_status("Could not retain object for an inspector tab");
				return;
			}
			object_inspector_history_[token] = history_handle;
		}
		Inspect::FreeObjectHandle(object_inspector_handle_);
		object_inspector_handle_ = next_handle;
		sampled_object_fields_.clear();
		sampled_object_properties_.clear();
		const Object rooted = Inspect::ResolveObjectHandle(object_inspector_handle_);
		if (!rooted) {
			set_status("Referenced object could not be resolved");
			return;
		}

		ObjectInspectorInfo inspector{};
		inspector.valid = true;
		inspector.token = token;
		inspector.type_name = type.full_name.empty() ? "<object>" : type.full_name;
		inspector.assembly_name = assembly_name(type);
		inspector.namespace_name = type.namespc;
		inspector.class_name = type.name;
		inspector.pointer_text = pointer_text(rooted.handle());
		inspector.instance_id = rooted.GetInstanceID();
		inspector.is_value_type = type.is_value_type;
		inspector.is_array = Inspect::type_name_looks_array(inspector.type_name);
		if (inspector.is_array)
			inspector.array_element_type = Inspect::array_element_type_name(inspector.type_name);
		const int origin_component_id = static_cast<int>(static_cast<std::uint32_t>(token >> 32));
		const bool origin_property = (token & 0x80000000ull) != 0;
		const int origin_member_index = static_cast<int>(token & 0x0fffffffull);
		if (origin_component_id != 0) {
			const auto origin_reflection = component_reflection_.find(origin_component_id);
			if (origin_reflection != component_reflection_.end()) {
				const bool value_origin =
					origin_property
					? origin_member_index >= 0 &&
					static_cast<std::size_t>(origin_member_index) < origin_reflection->second.properties.size() &&
					origin_reflection->second.properties[origin_member_index].is_value_type
					: origin_member_index >= 0 &&
					static_cast<std::size_t>(origin_member_index) < origin_reflection->second.fields.size() &&
					origin_reflection->second.fields[origin_member_index].is_value_type;
				if (value_origin) {
					inspector.value_origin_component_id = origin_component_id;
					inspector.value_origin_member_index = origin_member_index;
					inspector.value_origin_property = origin_property;
				}
			}
		}
		inspector.component.instance_id = 0;
		inspector.component.type_name = inspector.type_name;
		inspector.component.assembly_name = inspector.assembly_name;
		inspector.component.namespace_name = inspector.namespace_name;
		inspector.component.class_name = inspector.class_name;

		if (inspector.is_array) {
			working_.object_inspector = std::move(inspector);
			refresh_object_inspector_values();
			set_status("Inspecting array " + working_.object_inspector.type_name);
			return;
		}

		ComponentReflection reflection{};
		reflection.fields = Inspect::Fields(rooted, true);
		reflection.properties = Inspect::Properties(rooted, true);
		reflection.methods = Inspect::Methods(rooted, true);
		auto metadata = std::make_shared<ComponentInfo::Metadata>();
		metadata->fields.reserve(reflection.fields.size());
		for (const Inspect::FieldInfo& field : reflection.fields)
			metadata->fields.push_back(field_metadata(field));
		metadata->properties.reserve(reflection.properties.size());
		for (const Inspect::PropertyInfo& property : reflection.properties) {
			metadata->properties.push_back({ property.name, property.type_name, property.declaring_type.full_name,
									 property.can_read, property.can_write, {}, property.is_value_type, property.is_enum,
									 !property.type_is_opaque,
									 property.type_is_opaque ? "Runtime-specific type; metadata is available but generic read/write is unsafe." : "" });
		}
		metadata->methods.reserve(reflection.methods.size());
		for (const Inspect::MethodInfo& method : reflection.methods) {
			ComponentInfo::Method member{};
			member.name = method.name;
			member.return_type = method.return_type;
			member.declaring_type = method.declaring_type.full_name;
			member.is_static = method.is_static;
			member.return_is_value_type = method.return_is_value_type;
			member.return_is_enum = method.return_is_enum;
			member.uses_generic_parameter = method.return_type_is_generic_parameter ||
				std::any_of(method.parameters.begin(), method.parameters.end(), [](const Inspect::MethodParamInfo& parameter) {
					return parameter.is_generic_parameter;
				});
			member.runtime_callable = (!method.return_type_is_opaque || method.return_type_is_generic_parameter) &&
				std::none_of(method.parameters.begin(), method.parameters.end(), [](const Inspect::MethodParamInfo& parameter) {
					return parameter.is_opaque;
					});
			if (!member.runtime_callable)
				member.capability_reason = "Runtime-specific by-ref/internal signature; generic invocation is disabled.";
			for (const Inspect::MethodParamInfo& parameter : method.parameters) {
				member.parameter_types.push_back(parameter.type_name);
				member.parameter_names.push_back(parameter.name);
				member.parameter_is_value_types.push_back(parameter.is_value_type);
				member.parameter_is_enums.push_back(parameter.is_enum);
			}
			metadata->methods.push_back(std::move(member));
		}
		inspector.component.metadata = std::move(metadata);
		object_inspector_reflection_ = std::move(reflection);
		working_.object_inspector = std::move(inspector);
		refresh_object_inspector_values();
		set_status("Inspecting " + working_.object_inspector.type_name);
	}

	void RuntimeModel::refresh_object_inspector_values(bool force) {
		if ((!force && event_refresh_pending_) || !working_.object_inspector.valid || !object_inspector_handle_.handle)
			return;
		const Object object = Inspect::ResolveObjectHandle(object_inspector_handle_);
		if (!object) {
			working_.object_inspector = {};
			set_status("Object Inspector target was released; inspector closed safely");
			return;
		}
		if (working_.object_inspector.is_array) {
			auto values = std::make_shared<ComponentInfo::LiveValues>();
			Inspect::ValueInfo array{};
			array.kind = Inspect::ValueKind::ArrayReference;
			array.type_name = working_.object_inspector.type_name;
			array.object = object.handle();
			array.readable = true;
			const std::size_t length = Inspect::ArrayLength(array);
			const std::size_t offset = length == 0 ? 0
				: std::min(working_.object_inspector.array_offset,
					((length - 1) / kArrayPageSize) * kArrayPageSize);
			const std::size_t sampled = std::min(kArrayPageSize, length - offset);
			const std::string element_type = normalized_type(working_.object_inspector.array_element_type);
			const bool is_byte_array = element_type == "system.byte" || element_type == "byte";
			working_.object_inspector.array_offset = offset;
			values->fields.resize(sampled);
			values->field_references.resize(sampled);
			for (auto it = reference_handles_.begin(); it != reference_handles_.end();) {
				if ((it->first & 0xF000000000000000ull) == 0x3000000000000000ull) {
					if (it->second.handle) {
						Inspect::FreeObjectHandle(it->second);
					}
					it = reference_handles_.erase(it);
				}
				else {
					++it;
				}
			}
			for (std::size_t row = 0; row < sampled; ++row) {
				const std::size_t index = offset + row;
				values->fields[row] = guarded_managed_read(working_.object_inspector.array_element_type,
					[&] { return Inspect::ReadArrayElement(array, index); });
				Inspect::ValueInfo& value = values->fields[row];
				record_value_error(working_.object_inspector.type_name + "[" + std::to_string(index) + "]", value);
				if ((value.kind == Inspect::ValueKind::ObjectReference ||
					value.kind == Inspect::ValueKind::ArrayReference) &&
					value.object) {
					const std::uint64_t token = 0x3000000000000000ull | index;
					Inspect::ObjectHandle rooted = tracked_reference_handle(value);
					if (rooted.handle) {
						const Object tracked = Inspect::ResolveObjectHandle(rooted);
						if (tracked) {
							reference_handles_[token] = rooted;
							values->field_references[row] = { token, value.type_name, value.display,
															 pointer_text(tracked.handle()), false };
							value.object = nullptr;
						}
						else {
							Inspect::FreeObjectHandle(rooted);
							value = Inspect::unavailable_value(value.type_name, "array element was released");
						}
					}
					else {
						value = Inspect::unavailable_value(value.type_name, "could not track array element");
					}
				}
			}
			// Decode byte arrays from a native snapshot; refresh large arrays on demand.
			const bool capture_bytes = is_byte_array &&
				(force || !working_.object_inspector.byte_array || length <= kLiveByteArrayRefreshLimit);
			if (capture_bytes) {
				auto byte_array = std::make_shared<ObjectInspectorInfo::ByteArrayInspection>();
				const std::size_t byte_count = std::min(length, kMaxByteArrayInspectionBytes);
				byte_array->truncated = byte_count < length;
				byte_array->bytes.reserve(byte_count);
				for (std::size_t index = 0; index < byte_count; ++index) {
					const Inspect::ValueInfo value = guarded_managed_read("System.Byte", [&] {
						return Inspect::ReadArrayElement(array, index);
					});
					if (!value.readable ||
						(value.kind != Inspect::ValueKind::UnsignedInteger && value.kind != Inspect::ValueKind::SignedInteger)) {
						byte_array->read_error = value.display.empty()
							? "Could not read a byte from the managed array."
							: value.display;
						break;
					}
					const std::uint64_t raw = value.kind == Inspect::ValueKind::SignedInteger
						? static_cast<std::uint64_t>(value.signed_value) : value.unsigned_value;
					if (raw > std::numeric_limits<std::uint8_t>::max()) {
						byte_array->read_error = "Managed array element is outside the byte range.";
						break;
					}
					byte_array->bytes.push_back(static_cast<std::uint8_t>(raw));
				}
				if (byte_array->read_error.empty())
					byte_array->decoded = ByteData::decode(byte_array->bytes);
				working_.object_inspector.byte_array = std::move(byte_array);
			}
			else if (!is_byte_array) {
				working_.object_inspector.byte_array.reset();
			}
			working_.object_inspector.array_length = length;
			working_.object_inspector.array_values = std::move(values);
			return;
		}
		if (!working_.object_inspector.component.metadata)
			return;
		const ComponentInfo::Metadata& metadata = *working_.object_inspector.component.metadata;
		auto values = std::make_shared<ComponentInfo::LiveValues>();
		values->fields.resize(metadata.fields.size());
		values->properties.resize(metadata.properties.size());
		values->field_references.resize(metadata.fields.size());
		values->property_references.resize(metadata.properties.size());
		auto capture_reference = [&](Inspect::ValueInfo& value, std::uint64_t token,
			ComponentInfo::LiveValues::Reference& reference) {
				if ((value.kind != Inspect::ValueKind::ObjectReference && value.kind != Inspect::ValueKind::ArrayReference) ||
					!value.object) {
					release_reference_handle(token);
					value.object = nullptr;
					return;
				}
				if (const auto previous = reference_handles_.find(token); previous != reference_handles_.end()) {
					const Object current = Inspect::ResolveObjectHandle(previous->second);
					if (current && current.handle() == value.object) {
						reference = { token, value.type_name, value.display, pointer_text(current.handle()), false };
						value.object = nullptr;
						return;
					}
				}
				Inspect::ObjectHandle rooted = tracked_reference_handle(value);
				if (!rooted.handle) {
					value = Inspect::unavailable_value(value.type_name, "could not track referenced object");
					return;
				}
				const Object tracked = Inspect::ResolveObjectHandle(rooted);
				if (!tracked) {
					Inspect::FreeObjectHandle(rooted);
					value = Inspect::unavailable_value(value.type_name, "referenced object was released");
					return;
				}
				release_reference_handle(token);
				reference_handles_[token] = rooted;
				reference = { token, value.type_name, value.display, pointer_text(tracked.handle()), false };
				value.object = nullptr;
			};
		for (std::size_t index = 0; index < values->fields.size(); ++index) {
			const bool sampled = sampled_object_fields_.contains(index);
			values->fields[index] = !metadata.fields[index].runtime_safe
				? Inspect::unavailable_value(metadata.fields[index].type_name,
					"Metadata only: " + metadata.fields[index].capability_reason)
				: index < object_inspector_reflection_.fields.size() && sampled
				? guarded_managed_read(
					metadata.fields[index].type_name,
					[&] { return Inspect::ReadField(object, object_inspector_reflection_.fields[index]); })
				: Inspect::unavailable_value(metadata.fields[index].type_name, "Not sampled");
			if (metadata.fields[index].runtime_safe && sampled &&
				index < object_inspector_reflection_.fields.size())
				record_value_error(working_.object_inspector.type_name + "." + metadata.fields[index].name,
					values->fields[index]);
			capture_reference(values->fields[index], 0x4000000000000000ull | index, values->field_references[index]);
		}
		for (std::size_t index = 0; index < values->properties.size(); ++index) {
			const bool sampled = sampled_object_properties_.contains(index);
			values->properties[index] = !metadata.properties[index].runtime_safe
				? Inspect::unavailable_value(metadata.properties[index].type_name,
					"Metadata only: " + metadata.properties[index].capability_reason)
				: !metadata.properties[index].can_read
				? Inspect::unavailable_value(metadata.properties[index].type_name, "Property is not readable")
				: index < object_inspector_reflection_.properties.size() && sampled
				? guarded_managed_read(
					metadata.properties[index].type_name,
					[&] { return Inspect::ReadProperty(object, object_inspector_reflection_.properties[index]); })
				: Inspect::unavailable_value(metadata.properties[index].type_name, "Not sampled");
			if (metadata.properties[index].runtime_safe && metadata.properties[index].can_read && sampled &&
				index < object_inspector_reflection_.properties.size())
				record_value_error(working_.object_inspector.type_name + "." + metadata.properties[index].name,
					values->properties[index]);
			capture_reference(values->properties[index], 0x5000000000000000ull | index, values->property_references[index]);
		}
		working_.object_inspector.component.live_values = std::move(values);
	}

	GameObject RuntimeModel::resolve_live_game_object(
		int instance_id,
		Inspect::ObjectHandle& root) const {

		if (instance_id == 0)
			return {};

		const auto candidate_instance_id =
			[](GameObject candidate) -> int {

			if (!candidate ||
				!readable_address(
					reinterpret_cast<std::uintptr_t>(
						candidate.handle()))) {
				return 0;
			}

#if defined(_WIN32)
			__try {
				return candidate.GetInstanceID();
			}
			__except (capture_native_fault(_exception_info())) {
				clear_error();
				return 0;
			}
#else
			return candidate.GetInstanceID();
#endif
			};

		const auto root_candidate =
			[&](GameObject candidate) -> GameObject {

			if (candidate_instance_id(candidate) != instance_id)
				return {};

			Inspect::ObjectHandle candidate_root{};

#if defined(_WIN32)
			__try {
				candidate_root =
					Inspect::PinObject(
						Object{ candidate.handle() });
			}
			__except (capture_native_fault(_exception_info())) {
				clear_error();
				return {};
			}
#else
			candidate_root =
				Inspect::PinObject(
					Object{ candidate.handle() });
#endif

			if (!candidate_root.handle)
				return {};

			root = candidate_root;
			return candidate;
			};

		// Do not retain the complete FindObjectsOfTypeAll result after a census.
		// Pin only the clicked object and release this temporary managed array.
		const auto candidates = Object::FindObjectsOfTypeAllRooted<GameObject>();
		for (const GameObject& candidate : candidates) {

			if (candidate_instance_id(candidate) == instance_id)
				return root_candidate(candidate);
		}

		return {};
	}

	GameObject RuntimeModel::resolve_selected_object() const {
		// selected_handle_ keeps selected_ alive; reuse the validated wrapper.
		if (!selected_handle_.handle || !selected_)
			return {};
		return selected_;
	}

	Object RuntimeModel::resolve_component(int instance_id) const {
		if (const auto pinned = component_handles_.find(instance_id);
			pinned != component_handles_.end() && pinned->second.handle) {
			return Inspect::ResolveObjectHandle(pinned->second);
		}
		return {};
	}

	void RuntimeModel::select_object(GameObject object, Inspect::ObjectHandle root) {
		if (!safe_object_alive(object)) {
			Inspect::FreeObjectHandle(root);
			clear_selection();
			return;
		}
		const int instance_id = object.GetInstanceID();
		if (working_.selected_instance_id == instance_id) {
			Inspect::FreeObjectHandle(root);
			return;
		}
		// A new selection ends a temporary camera focus so the previous camera
		// pose is not carried into an unrelated object.
		if (camera_focus_.active())
			restore_focused_camera();

		if (!root.handle) {
#if defined(_WIN32)
			__try {
				root = Inspect::PinObject(Object{ object.handle() });
			}
			__except (capture_native_fault(_exception_info())) {
				clear_error();
			}
#else
			root = Inspect::PinObject(Object{ object.handle() });
#endif
		}
		if (!root.handle) {
			set_status("Selection failed: GameObject could not be rooted");
			return;
		}

		if (highlight_id_ != 0)
			ModUI::Highlight::enqueue_remove(highlight_id_);
		if (highlight_locator_id_ != 0)
			ModUI::Highlight::enqueue_remove(highlight_locator_id_);
		// Clear IDs immediately; queued removal runs before the next draw.
		highlight_id_ = 0;
		highlight_locator_id_ = 0;

		Inspect::FreeObjectHandle(selected_handle_);
		selected_handle_ = root;
		selected_ = object;
		working_.selected_instance_id = instance_id;
		working_.inspector = {};
		clear_highlight_renderer_cache();
	}

} // namespace Explorer

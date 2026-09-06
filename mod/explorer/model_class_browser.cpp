// Copyright (c) 2026 Jadis0x. All rights reserved.
#include "explorer_model.h"
#include "model_shared.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <deque>
#include <string>
#include <unordered_set>
#include <vector>

using namespace URK::Unity;

namespace Explorer {

	// RuntimeModel::ClassInstanceScan is defined in model_shared.h: explorer_model.cpp's
	// destructor and unique_ptr<ClassInstanceScan> resets also need the complete type.

	void RuntimeModel::load_component_class_catalog() {
		// Managed metadata is read only from the Unity main thread.
		const URK::managed::Class* component_base =
			URK::managed::find_class("UnityEngine.CoreModule.dll", "UnityEngine", "Component");
		if (!component_base)
			component_base = URK::managed::find_class("UnityEngine.dll", "UnityEngine", "Component");
		if (!component_base) {
			set_status("Component browser is unavailable: UnityEngine.Component was not found");
			return;
		}

		auto catalog = std::make_shared<ComponentClassCatalog>();
		std::unordered_set<std::string> seen;
		constexpr std::uint32_t kTypeAttributeInterface = 0x20u;
		constexpr std::uint32_t kTypeAttributeAbstract = 0x80u;
		constexpr std::size_t kMaxComponentClasses = 20000;

		const std::size_t assembly_count = std::min<std::size_t>(URK::managed::domain_get_assembly_count(), 4096);
		if (assembly_count == 0) {
			const char* error = URK::managed::last_error();
			set_status(error && error[0]
				? std::string("Component browser could not enumerate ") +
					URK::compiled_runtime_name + " assemblies: " + error
				: "Component browser found no loaded managed assemblies");
			return;
		}
		for (std::size_t assembly_index = 0; assembly_index < assembly_count; ++assembly_index) {
			const URK::managed::Assembly* assembly = URK::managed::domain_get_assembly(assembly_index);
			const URK::managed::Image* image = assembly ? URK::managed::assembly_get_image(assembly) : nullptr;
			const char* image_name = image ? URK::managed::image_get_name(image) : nullptr;
			if (!image || !image_name || !image_name[0])
				continue;

			const std::size_t class_count = std::min<std::size_t>(URK::managed::image_get_class_count(image), 1000000);
			for (std::size_t class_index = 0; class_index < class_count; ++class_index) {
				const URK::managed::Class* klass = URK::managed::image_get_class(image, class_index);
				if (!klass || klass == component_base)
					continue;
				const std::uint32_t flags = URK::managed::class_get_flags(klass);
				if ((flags & (kTypeAttributeInterface | kTypeAttributeAbstract)) != 0)
					continue;

				bool is_component = URK::managed::class_is_assignable_from(component_base, klass) != 0;
				if (!is_component) {
					std::unordered_set<const URK::managed::Class*> visited_parents;
					std::size_t parent_depth = 0;
					for (const URK::managed::Class* parent = URK::managed::class_get_parent(klass);
						parent && parent_depth++ < Inspect::kMaxMetadataInheritanceDepth &&
						visited_parents.insert(parent).second;
						parent = URK::managed::class_get_parent(parent)) {
						if (parent == component_base) {
							is_component = true;
							break;
						}
					}
				}
				if (!is_component)
					continue;

				const Inspect::TypeInfo type = Inspect::DescribeClass(klass);
				if (type.name.empty())
					continue;
				// Unity creates Transform components automatically.
				if (type.full_name == "UnityEngine.Transform" || type.full_name == "UnityEngine.RectTransform")
					continue;

				ComponentClassInfo entry{};
				entry.image = image_name;
				entry.namespc = type.namespc;
				entry.class_name = type.name;
				entry.full_name = type.full_name.empty() ? type.name : type.full_name;
				entry.pointer_text = pointer_text(const_cast<URK::managed::Class*>(klass));
				const std::string key = entry.image + "\n" + entry.full_name;
				if (!seen.insert(key).second)
					continue;
				catalog->classes.push_back(std::move(entry));
				if (catalog->classes.size() >= kMaxComponentClasses)
					break;
			}
			if (catalog->classes.size() >= kMaxComponentClasses)
				break;
		}

		std::sort(catalog->classes.begin(), catalog->classes.end(),
			[](const ComponentClassInfo& left, const ComponentClassInfo& right) {
				return left.full_name == right.full_name ? left.image < right.image
					: left.full_name < right.full_name;
			});
		component_class_catalog_ = std::move(catalog);
		set_status("Component browser loaded " + std::to_string(component_class_catalog_->classes.size()) + " classes");
	}

	void RuntimeModel::load_class_browser_catalog() {
		const URK::managed::Class* object_base =
			URK::managed::find_class("UnityEngine.CoreModule.dll", "UnityEngine", "Object");
		const URK::managed::Class* component_base =
			URK::managed::find_class("UnityEngine.CoreModule.dll", "UnityEngine", "Component");
		if (!object_base)
			object_base = URK::managed::find_class("UnityEngine.dll", "UnityEngine", "Object");
		if (!component_base)
			component_base = URK::managed::find_class("UnityEngine.dll", "UnityEngine", "Component");
		auto catalog = std::make_shared<ClassBrowserCatalog>();
		std::unordered_set<std::string> seen;
		constexpr std::uint32_t kTypeAttributeInterface = 0x20u;
		constexpr std::uint32_t kTypeAttributeAbstract = 0x80u;
		constexpr std::uint32_t kTypeAttributeSealed = 0x100u;
		constexpr std::size_t kMaxBrowserClasses = 50000;
		const std::size_t assembly_count = std::min<std::size_t>(URK::managed::domain_get_assembly_count(), 4096);
		if (assembly_count == 0) {
			const char* error = URK::managed::last_error();
			set_status(error && error[0]
				? std::string("Class browser could not enumerate managed assemblies: ") + error
				: "Class browser found no loaded managed assemblies");
			return;
		}
		for (std::size_t assembly_index = 0; assembly_index < assembly_count; ++assembly_index) {
			const URK::managed::Assembly* assembly = URK::managed::domain_get_assembly(assembly_index);
			const URK::managed::Image* image = assembly ? URK::managed::assembly_get_image(assembly) : nullptr;
			const char* image_name = image ? URK::managed::image_get_name(image) : nullptr;
			if (!image || !image_name || !image_name[0])
				continue;
			const std::size_t class_count = std::min<std::size_t>(URK::managed::image_get_class_count(image), 1000000);
			for (std::size_t class_index = 0; class_index < class_count; ++class_index) {
				const URK::managed::Class* klass = URK::managed::image_get_class(image, class_index);
				if (!klass)
					continue;
				const Inspect::TypeInfo type = Inspect::DescribeClass(klass);
				if (type.name.empty() || type.name == "<Module>")
					continue;
				BrowserClassInfo entry{};
				entry.image = image_name;
				entry.namespc = type.namespc;
				entry.class_name = type.name;
				entry.full_name = type.full_name.empty() ? type.name : type.full_name;
				entry.pointer_text = pointer_text(const_cast<URK::managed::Class*>(klass));
				const std::uint32_t flags = URK::managed::class_get_flags(klass);
				entry.parent_name = Inspect::DescribeClass(URK::managed::class_get_parent(klass)).full_name;
				entry.is_interface = (flags & kTypeAttributeInterface) != 0;
				entry.is_abstract = (flags & kTypeAttributeAbstract) != 0;
				entry.is_static = entry.is_abstract && (flags & kTypeAttributeSealed) != 0;
				entry.is_value_type = type.is_value_type;
				entry.is_enum = type.is_enum;
				entry.is_unity_object = object_base && URK::managed::class_is_assignable_from(object_base, klass) != 0;
				entry.is_component = component_base && (URK::managed::class_is_assignable_from(component_base, klass) != 0 ||
					URK::managed::class_has_parent(klass, component_base) != 0);
				void* interface_iterator = nullptr;
				std::unordered_set<const URK::managed::Class*> visited_interfaces;
				std::size_t interface_count = 0;
				while (const URK::managed::Class* interface_type =
					URK::managed::class_get_interfaces(klass, &interface_iterator)) {
					if (++interface_count > 4096 || !visited_interfaces.insert(interface_type).second)
						break;
					const Inspect::TypeInfo interface_info = Inspect::DescribeClass(interface_type);
					if (!interface_info.full_name.empty())
						entry.interfaces.push_back(interface_info.full_name);
				}
				const std::string key = entry.image + "\n" + entry.full_name;
				if (!seen.insert(key).second)
					continue;
				catalog->classes.push_back(std::move(entry));
				if (catalog->classes.size() >= kMaxBrowserClasses)
					break;
			}
			if (catalog->classes.size() >= kMaxBrowserClasses)
				break;
		}
		std::sort(catalog->classes.begin(), catalog->classes.end(),
			[](const BrowserClassInfo& left, const BrowserClassInfo& right) {
				return left.full_name == right.full_name ? left.image < right.image
					: left.full_name < right.full_name;
			});
		class_browser_catalog_ = std::move(catalog);
		set_status("Class browser indexed " + std::to_string(class_browser_catalog_->classes.size()) + " loaded types");
	}

	void RuntimeModel::find_class_instances(const Command& command) {
		clear_class_instance_scan();
		for (auto& [_, handle] : class_browser_handles_)
			Inspect::FreeObjectHandle(handle);
		class_browser_handles_.clear();
		working_.class_browser_instances.clear();
		working_.class_browser_scanned_objects = 0;
		working_.class_browser_static_roots = 0;
		working_.class_browser_scan_truncated = false;
		working_.class_browser_query = {};
		working_.class_browser_query.image = command.image;
		working_.class_browser_query.namespc = command.namespc;
		working_.class_browser_query.class_name = command.class_name;
		working_.class_browser_query.full_name =
			command.namespc.empty() ? command.class_name : command.namespc + "." + command.class_name;
		working_.class_browser_query.is_component = command.int_value != 0;
		working_.class_browser_query.is_unity_object = command.class_is_unity_object;
		if (class_browser_catalog_) {
			const auto selected = std::find_if(class_browser_catalog_->classes.begin(), class_browser_catalog_->classes.end(),
				[&](const BrowserClassInfo& entry) {
					return entry.image == command.image && entry.namespc == command.namespc &&
						entry.class_name == command.class_name;
				});
			if (selected != class_browser_catalog_->classes.end())
				working_.class_browser_query = *selected;
		}
		if (command.class_name.empty()) {
			set_status("Choose a class before finding instances");
			return;
		}

		const URK::managed::Class* target =
			URK::managed::find_class(command.image.c_str(), command.namespc.c_str(), command.class_name.c_str());
		if (!target) {
			set_status("Selected type could no longer be resolved");
			return;
		}

		// UnityEngine.Object subclasses have a complete, direct runtime query and
		// do not need the managed reachability scan used for ordinary classes.
		if (working_.class_browser_query.is_unity_object) {
		const auto direct_instances = command.bool_value
			? detail::FindObjectsUsingRooted<Object>(ResourcesType, "FindObjectsOfTypeAll", command.image,
				command.namespc, command.class_name)
			: detail::FindObjectsUsingRooted<Object>(UnityObjectType, "FindObjectsOfType", command.image,
				command.namespc, command.class_name);
		constexpr std::size_t kMaxDirectInstanceResults = 512;
		for (const Object& object : direct_instances) {
			if (working_.class_browser_instances.size() >= kMaxDirectInstanceResults)
				break;
			const Inspect::TypeInfo type = safe_type_of(object);
			Inspect::ObjectHandle handle = type.is_value_type ? Inspect::PinObject(object) : Inspect::WeakObject(object);
			if (!handle.handle)
				continue;
			const std::uint64_t token = 0x9000000000000000ull | (next_reference_token_++ & 0x0fffffffffffffffull);
			class_browser_handles_[token] = handle;
			ClassBrowserInstanceInfo result{};
			result.token = token;
			result.type_name = type.full_name.empty() ? working_.class_browser_query.full_name : type.full_name;
			result.pointer_text = pointer_text(object.handle());
			result.source = "Unity object query";
			result.name = object.name();
			if (working_.class_browser_query.is_component) {
				const GameObject owner = Component{ object.handle() }.gameObject();
				if (owner) {
					result.game_object_instance_id = owner.GetInstanceID();
					result.game_object_name = owner.name();
				}
			}
			if (result.name.empty())
				result.name = result.type_name;
			working_.class_browser_instances.push_back(std::move(result));
		}
		working_.class_browser_scanned_objects = direct_instances.size();
		set_status("Found " + std::to_string(working_.class_browser_instances.size()) + " Unity instance(s) of " +
			working_.class_browser_query.full_name);
		return;
		}

		class_instance_scan_ = std::make_unique<ClassInstanceScan>();
		class_instance_scan_->target = target;
		class_instance_scan_->unity_object_base =
			URK::managed::find_class("UnityEngine.CoreModule.dll", "UnityEngine", "Object");
		if (!class_instance_scan_->unity_object_base)
			class_instance_scan_->unity_object_base =
				URK::managed::find_class("UnityEngine.dll", "UnityEngine", "Object");
		class_instance_scan_->started = Clock::now();
		working_.class_browser_scan_active = true;
		next_class_scan_publish_ = Clock::now();
		set_status("Scanning static managed roots for " + working_.class_browser_query.full_name + "...");
	}

	void RuntimeModel::clear_class_instance_scan() {
		if (class_instance_scan_) {
			for (ClassInstanceScan::PendingObject& pending : class_instance_scan_->pending)
				Inspect::FreeObjectHandle(pending.handle);
			class_instance_scan_.reset();
		}
		working_.class_browser_scan_active = false;
	}

	void RuntimeModel::continue_class_instance_scan() {
		if (!class_instance_scan_)
			return;
		ClassInstanceScan& scan = *class_instance_scan_;
		constexpr std::size_t kMaxGraphNodes = 30000;
		constexpr std::size_t kMaxArrayElements = 256;
		constexpr int kMaxGraphDepth = 5;
		constexpr std::size_t kMaxInstanceResults = 512;
		constexpr auto kMaxScanDuration = std::chrono::seconds(10);
		if (Clock::now() - scan.started >= kMaxScanDuration) {
			working_.class_browser_scan_truncated = true;
			const std::size_t found = working_.class_browser_instances.size();
			const std::size_t scanned = working_.class_browser_scanned_objects;
			clear_class_instance_scan();
			set_status("Instance scan stopped after 10 seconds; found " + std::to_string(found) +
				" instance(s) after scanning " + std::to_string(scanned) + " reachable object(s)");
			publish();
			return;
		}
		const Clock::time_point deadline = Clock::now() + std::chrono::milliseconds(3);
		const auto enqueue_object = [&](Object object, int depth, bool array, std::string source) {
			if (!object.handle() || scan.seen.size() >= kMaxGraphNodes)
				return false;
			if (!scan.seen.insert(object.handle()).second)
				return false;
			Inspect::ObjectHandle handle = Inspect::PinObject(object);
			if (!handle.handle) {
				scan.seen.erase(object.handle());
				return false;
			}
			scan.pending.push_back({handle, depth, array, std::move(source)});
			return true;
		};

		if (scan.phase == ClassInstanceScan::Phase::StaticRoots) {
			const std::size_t assembly_count =
				std::min<std::size_t>(URK::managed::domain_get_assembly_count(), 4096);
			while (scan.assembly_index < assembly_count && Clock::now() < deadline &&
				scan.seen.size() < kMaxGraphNodes) {
				if (!scan.static_owner) {
					const URK::managed::Assembly* assembly = URK::managed::domain_get_assembly(scan.assembly_index);
					const URK::managed::Image* image = assembly ? URK::managed::assembly_get_image(assembly) : nullptr;
					const std::size_t class_count = image
						? std::min<std::size_t>(URK::managed::image_get_class_count(image), 1000000) : 0;
					if (!image || scan.class_index >= class_count) {
						++scan.assembly_index;
						scan.class_index = 0;
						continue;
					}
					scan.static_owner = URK::managed::image_get_class(image, scan.class_index++);
					if (!scan.static_owner)
						continue;
					const Inspect::TypeInfo owner = Inspect::DescribeClass(scan.static_owner);
					scan.static_owner_name = owner.full_name;
					scan.static_fields = Inspect::fields_from_class(scan.static_owner, false);
					scan.static_field_index = 0;
				}
				while (scan.static_field_index < scan.static_fields.size() && Clock::now() < deadline &&
					scan.seen.size() < kMaxGraphNodes) {
					const Inspect::FieldInfo& field = scan.static_fields[scan.static_field_index++];
					if (!field.is_static || field.is_value_type)
						continue;
					const Inspect::ValueInfo value = Inspect::ReadField({}, field);
					if ((value.kind != Inspect::ValueKind::ObjectReference &&
						value.kind != Inspect::ValueKind::ArrayReference) || !value.object)
						continue;
					if (enqueue_object(Object{value.object}, 0,
						value.kind == Inspect::ValueKind::ArrayReference,
						"static " + scan.static_owner_name + "." + field.name))
						++working_.class_browser_static_roots;
				}
				if (scan.static_field_index < scan.static_fields.size())
					break;
				scan.static_owner = nullptr;
				scan.static_owner_name.clear();
				scan.static_fields.clear();
				scan.static_field_index = 0;
			}
			if (scan.assembly_index >= assembly_count || scan.seen.size() >= kMaxGraphNodes)
			scan.phase = ClassInstanceScan::Phase::ReachableGraph;
		}

		while (scan.phase == ClassInstanceScan::Phase::ReachableGraph && !scan.pending.empty() &&
			Clock::now() < deadline) {
			ClassInstanceScan::PendingObject current = std::move(scan.pending.front());
			scan.pending.pop_front();
			const Object object = Inspect::ResolveObjectHandle(current.handle);
			if (!object) {
				Inspect::FreeObjectHandle(current.handle);
				continue;
			}
			++working_.class_browser_scanned_objects;
			const URK::managed::Class* actual =
				URK::managed::object_get_class(static_cast<URK::managed::Object*>(object.handle()));
			if (actual && URK::managed::class_is_assignable_from(scan.target, actual) != 0 &&
				working_.class_browser_instances.size() < kMaxInstanceResults) {
				Inspect::ObjectHandle result_handle = Inspect::PinObject(object);
				if (result_handle.handle) {
					const Inspect::TypeInfo type = Inspect::DescribeClass(actual);
					const std::uint64_t token = 0x9000000000000000ull |
						(next_reference_token_++ & 0x0fffffffffffffffull);
					class_browser_handles_[token] = result_handle;
					ClassBrowserInstanceInfo result{};
					result.token = token;
					result.type_name = type.full_name.empty() ? working_.class_browser_query.full_name : type.full_name;
					result.pointer_text = pointer_text(object.handle());
					result.source = current.source;
					result.name = result.type_name;
					if (scan.unity_object_base &&
						URK::managed::class_is_assignable_from(scan.unity_object_base, actual) != 0)
						result.name = object.name();
					if (result.name.empty())
						result.name = result.type_name;
					working_.class_browser_instances.push_back(std::move(result));
				}
			}

			if (actual && current.depth < kMaxGraphDepth && scan.seen.size() < kMaxGraphNodes) {
				if (current.array) {
					Inspect::ValueInfo array{};
					array.kind = Inspect::ValueKind::ArrayReference;
					array.object = object.handle();
					const std::size_t length = std::min(Inspect::ArrayLength(array), kMaxArrayElements);
					for (std::size_t index = 0; index < length; ++index) {
						const Inspect::ValueInfo element = Inspect::ReadArrayElement(array, index);
						if ((element.kind == Inspect::ValueKind::ObjectReference ||
							element.kind == Inspect::ValueKind::ArrayReference) && element.object)
							enqueue_object(Object{element.object}, current.depth + 1,
								element.kind == Inspect::ValueKind::ArrayReference, current.source + "[]");
					}
				}
				else {
					for (const Inspect::FieldInfo& field : Inspect::fields_from_class(actual, true)) {
						if (field.is_static || field.is_value_type)
							continue;
						const Inspect::ValueInfo value = Inspect::ReadField(object, field);
						if ((value.kind == Inspect::ValueKind::ObjectReference ||
							value.kind == Inspect::ValueKind::ArrayReference) && value.object)
							enqueue_object(Object{value.object}, current.depth + 1,
								value.kind == Inspect::ValueKind::ArrayReference,
								current.source + " -> " + field.name);
					}
				}
			}
			Inspect::FreeObjectHandle(current.handle);
		}

		working_.class_browser_scan_truncated = scan.seen.size() >= kMaxGraphNodes;
		const bool complete = scan.phase == ClassInstanceScan::Phase::ReachableGraph && scan.pending.empty();
		if (complete) {
			const std::size_t found = working_.class_browser_instances.size();
			const std::size_t scanned = working_.class_browser_scanned_objects;
			clear_class_instance_scan();
			set_status("Found " + std::to_string(found) + " instance(s); scanned " +
				std::to_string(scanned) + " reachable object(s)");
		}
		else {
			working_.class_browser_scan_active = true;
		}
		const Clock::time_point now = Clock::now();
		if (complete || now >= next_class_scan_publish_) {
			next_class_scan_publish_ = now + std::chrono::milliseconds(100);
			publish();
		}
	}

	void RuntimeModel::load_class_browser_static_state(const Command& command) {
		for (auto& [_, handle] : class_browser_static_handles_)
			Inspect::FreeObjectHandle(handle);
		class_browser_static_handles_.clear();
		working_.class_browser_static_fields.clear();
		working_.class_browser_static_query = {};
		working_.class_browser_static_query.image = command.image;
		working_.class_browser_static_query.namespc = command.namespc;
		working_.class_browser_static_query.class_name = command.class_name;
		working_.class_browser_static_query.full_name =
			command.namespc.empty() ? command.class_name : command.namespc + "." + command.class_name;
		if (class_browser_catalog_) {
			const auto match = std::find_if(class_browser_catalog_->classes.begin(), class_browser_catalog_->classes.end(),
				[&command](const BrowserClassInfo& entry) {
					return entry.image == command.image && entry.namespc == command.namespc &&
						entry.class_name == command.class_name;
				});
			if (match != class_browser_catalog_->classes.end())
				working_.class_browser_static_query = *match;
		}
		const URK::managed::Class* klass =
			URK::managed::find_class(command.image.c_str(), command.namespc.c_str(), command.class_name.c_str());
		if (!klass) {
			set_status("Selected type could no longer be resolved");
			return;
		}
		const std::vector<Inspect::FieldInfo> fields = Inspect::fields_from_class(klass, true);
		for (std::size_t index = 0; index < fields.size(); ++index) {
			const Inspect::FieldInfo& field = fields[index];
			if (!field.is_static)
				continue;
			const Inspect::ValueInfo value = Inspect::ReadField({}, field);
			ClassBrowserStaticFieldInfo result{};
			result.member_index = index;
			result.name = field.name;
			result.type_name = field.type_name;
			result.declaring_type = field.declaring_type.full_name;
			result.display = value.display.empty() ? "<unavailable>" : value.display;
			result.value = value;
			result.readable = value.readable;
			result.writable = Inspect::FieldCanWrite(field);
			result.is_reference =
				value.kind == Inspect::ValueKind::ObjectReference || value.kind == Inspect::ValueKind::ArrayReference;
			if (result.is_reference && value.object) {
				Inspect::ObjectHandle handle = Inspect::PinObject(Object{ value.object });
				if (handle.handle) {
					result.token = 0xa000000000000000ull | (next_reference_token_++ & 0x0fffffffffffffffull);
					result.pointer_text = pointer_text(value.object);
					class_browser_static_handles_[result.token] = handle;
				}
			}
			working_.class_browser_static_fields.push_back(std::move(result));
		}
		const std::vector<Inspect::PropertyInfo> properties = Inspect::properties_from_class(klass, true);
		for (std::size_t index = 0; index < properties.size(); ++index) {
			const Inspect::PropertyInfo& property = properties[index];
			if (!property.is_static)
				continue;
			const Inspect::ValueInfo value =
				property.can_read ? Inspect::ReadProperty({}, property) : Inspect::ValueInfo{};
			ClassBrowserStaticFieldInfo result{};
			result.member_index = index;
			result.name = property.name;
			result.type_name = property.type_name;
			result.declaring_type = property.declaring_type.full_name;
			result.display = property.can_read
				? (value.display.empty() ? "<unavailable>" : value.display)
				: "<write-only>";
			result.value = value;
			result.is_property = true;
			result.readable = property.can_read && value.readable;
			result.writable = property.can_write;
			result.is_reference =
				value.kind == Inspect::ValueKind::ObjectReference || value.kind == Inspect::ValueKind::ArrayReference;
			if (result.is_reference && value.object) {
				Inspect::ObjectHandle handle = Inspect::PinObject(Object{value.object});
				if (handle.handle) {
					result.token = 0xa000000000000000ull | (next_reference_token_++ & 0x0fffffffffffffffull);
					result.pointer_text = pointer_text(value.object);
					class_browser_static_handles_[result.token] = handle;
				}
			}
			working_.class_browser_static_fields.push_back(std::move(result));
		}
		set_status("Loaded " + std::to_string(working_.class_browser_static_fields.size()) + " static member(s) from " +
			working_.class_browser_static_query.full_name);
	}

	void RuntimeModel::set_class_browser_static_field(const Command& command) {
		const URK::managed::Class* klass =
			URK::managed::find_class(command.image.c_str(), command.namespc.c_str(), command.class_name.c_str());
		if (!klass) {
			set_status("Static field write failed: selected type could no longer be resolved");
			return;
		}
		Inspect::ValueInfo requested{};
		Inspect::ObjectHandle root{};
		bool written = false;
		bool verified = false;
		std::string member_name;
		if (command.int_value != 0) {
			const std::vector<Inspect::PropertyInfo> properties = Inspect::properties_from_class(klass, true);
			if (command.member_index < 0 || static_cast<std::size_t>(command.member_index) >= properties.size()) {
				set_status("Static property write failed: member metadata changed");
				return;
			}
			const Inspect::PropertyInfo& property = properties[static_cast<std::size_t>(command.member_index)];
			member_name = property.name;
			if (!property.is_static || !property.can_write) {
				set_status("Static property " + property.name + " is not writable");
				return;
			}
			const bool parsed = command_value(property.type_name, command, requested) ||
				(property.is_enum && enum_value_from_text(property.type_name, command.text, requested)) ||
				(!property.is_enum && (managed_reference_value_from_text(property.type_name, property.type, command.text, requested) || reference_value_from_text(
					property.type_name, property.type, command.text, requested, root)));
			if (!parsed) {
				Inspect::FreeObjectHandle(root);
				set_status("Static property " + property.name + " rejected the value; expected " + property.type_name);
				return;
			}
			written = Inspect::SetProperty({}, property, requested);
			if (written && property.can_read)
				verified = values_equivalent(requested, guarded_managed_read(property.type_name, [&] {
					return Inspect::ReadProperty({}, property);
				}));
			else
				verified = written;
		}
		else {
			const std::vector<Inspect::FieldInfo> fields = Inspect::fields_from_class(klass, true);
			if (command.member_index < 0 || static_cast<std::size_t>(command.member_index) >= fields.size()) {
				set_status("Static field write failed: member metadata changed");
				return;
			}
			const Inspect::FieldInfo& field = fields[static_cast<std::size_t>(command.member_index)];
			member_name = field.name;
			if (!field.is_static || !Inspect::FieldCanWrite(field)) {
				set_status("Static field " + field.name + " is not writable");
				return;
			}
			const bool parsed = command_value(field.type_name, command, requested) ||
				(field.is_enum && enum_value_from_text(field.type_name, command.text, requested)) ||
				(!field.is_enum &&
				 (managed_reference_value_from_text(field.type_name, field.type, command.text, requested) || reference_value_from_text(field.type_name, field.type, command.text, requested, root)));
			if (!parsed) {
				Inspect::FreeObjectHandle(root);
				set_status("Static field " + field.name + " rejected the value; expected " + field.type_name);
				return;
			}
			written = Inspect::SetField({}, field, requested);
			if (written)
				verified = values_equivalent(requested, guarded_managed_read(field.type_name, [&] {
					return Inspect::ReadField({}, field);
				}));
		}
		Inspect::FreeObjectHandle(root);
		if (!written) {
			capture_last_error(std::string("Set static member ") + member_name);
			return;
		}
		load_class_browser_static_state(command);
		set_status(verified ? "Set static member " + member_name + " (verified)"
			: "Static member " + member_name + " changed, but read-back differs from the requested value");
	}

	void RuntimeModel::create_class_instance(const Command& command) {
		if (working_.class_browser_members_query.image != command.image ||
			working_.class_browser_members_query.namespc != command.namespc ||
			working_.class_browser_members_query.class_name != command.class_name) {
			set_status("Create instance failed: Class Browser selection changed");
			return;
		}
		if (command.member_index < 0 || static_cast<std::size_t>(command.member_index) >= class_browser_reflection_.methods.size()) {
			set_status("Create instance failed: constructor metadata changed");
			return;
		}
		const Inspect::MethodInfo& constructor = class_browser_reflection_.methods[static_cast<std::size_t>(command.member_index)];
		if (constructor.name != ".ctor" || constructor.is_static || constructor.is_abstract) {
			set_status("Create instance failed: selected member is not an instance constructor");
			return;
		}
		if (command.method_arguments.size() != constructor.parameters.size()) {
			set_status("Create instance failed: argument count does not match the constructor");
			return;
		}
		const URK::managed::Class* klass = URK::managed::find_class(command.image.c_str(), command.namespc.c_str(), command.class_name.c_str());
		const URK::managed::Class* component_base = URK::managed::find_class("", "UnityEngine", "Component");
		const URK::managed::Class* scriptable_base = URK::managed::find_class("", "UnityEngine", "ScriptableObject");
		const URK::managed::Class* object_base = URK::managed::find_class("", "UnityEngine", "Object");
		if (!klass) {
			set_status("Create instance failed: selected class could no longer be resolved");
			return;
		}
		if (component_base && URK::managed::class_is_assignable_from(component_base, klass) != 0) {
			set_status("Create instance requires a GameObject owner for Components; use Add Component from that GameObject's Inspector");
			return;
		}
		Object created{};
		Inspect::ObjectHandle constructor_root{};
		if (scriptable_base && URK::managed::class_is_assignable_from(scriptable_base, klass) != 0) {
			if (!constructor.parameters.empty()) {
				set_status("Create instance failed: ScriptableObject constructors are not Unity creation APIs; use a parameterless ScriptableObject type");
				return;
			}
			created = ScriptableObject::CreateInstance(TypeRef{ command.image, command.namespc, command.class_name });
			if (!created) {
				capture_last_error("Create ScriptableObject instance");
				return;
			}
		}
		else if (object_base && URK::managed::class_is_assignable_from(object_base, klass) != 0) {
			set_status("Create instance failed: this UnityEngine.Object subtype needs its own Unity factory; raw constructors are unsafe");
			return;
		}
		else {
			std::vector<Inspect::ValueInfo> arguments;
			std::vector<Inspect::ObjectHandle> roots;
			const auto release_roots = [&] {
				for (Inspect::ObjectHandle& root : roots)
					Inspect::FreeObjectHandle(root);
				roots.clear();
			};
			arguments.reserve(constructor.parameters.size());
			for (std::size_t index = 0; index < constructor.parameters.size(); ++index) {
				const Inspect::MethodParamInfo& parameter = constructor.parameters[index];
				Command argument{};
				argument.text = command.method_arguments[index];
				argument.bool_value = normalized_type(parameter.type_name) == "system.boolean" &&
					(argument.text == "true" || argument.text == "1");
				Inspect::ValueInfo value{};
				if (command_value(parameter.type_name, argument, value) ||
					(Inspect::DescribeType(parameter.type).is_enum && enum_value_from_text(parameter.type_name, argument.text, value))) {
					arguments.push_back(std::move(value));
					continue;
				}
				Inspect::ObjectHandle root{};
				if (managed_reference_value_from_text(parameter.type_name, parameter.type, argument.text, value) ||
					reference_value_from_text(parameter.type_name, parameter.type, argument.text, value, root)) {
					if (root.handle)
						roots.push_back(root);
					arguments.push_back(std::move(value));
					continue;
				}
				Inspect::FreeObjectHandle(root);
				release_roots();
				set_status("Create instance failed: constructor argument " + std::to_string(index + 1) + " is invalid for " + parameter.type_name);
				return;
			}
			const Inspect::ValueInfo result = guarded_managed_read(constructor.declaring_type.full_name, [&] {
				return Inspect::ConstructObject(constructor, arguments, constructor_root);
			});
			release_roots();
			if (!result.readable || !constructor_root.handle) {
				Inspect::FreeObjectHandle(constructor_root);
				const char* error = last_error();
				set_status(std::string("Create instance failed: ") +
					(error && error[0] ? error : (result.display.empty() ? "constructor returned no object" : result.display)));
				return;
			}
			created = Inspect::ResolveObjectHandle(constructor_root);
		}
		std::string error;
		std::uint64_t token = 0;
		if (!managed_references_.capture(created, "Class Browser constructor", error, token)) {
			Inspect::FreeObjectHandle(constructor_root);
			set_status("Create instance failed after construction: " + error);
			return;
		}
		Inspect::FreeObjectHandle(constructor_root);
		set_status("Created " + command.namespc + "." + command.class_name + " and saved it as reference #" +
			std::to_string(token & 0x0fffffffffffffffull));
		// Open the new object in the inspector.
		inspect_reference(token);
	}

	void RuntimeModel::load_class_browser_members(const Command& command) {
		class_browser_reflection_ = {};
		working_.class_browser_members = {};
		working_.class_browser_members_query = {};
		working_.class_browser_members_query.image = command.image;
		working_.class_browser_members_query.namespc = command.namespc;
		working_.class_browser_members_query.class_name = command.class_name;
		working_.class_browser_members_query.full_name =
			command.namespc.empty() ? command.class_name : command.namespc + "." + command.class_name;
		if (class_browser_catalog_) {
			const auto match = std::find_if(class_browser_catalog_->classes.begin(), class_browser_catalog_->classes.end(),
				[&command](const BrowserClassInfo& entry) {
					return entry.image == command.image && entry.namespc == command.namespc &&
						entry.class_name == command.class_name;
				});
			if (match != class_browser_catalog_->classes.end())
				working_.class_browser_members_query = *match;
		}
		const URK::managed::Class* klass =
			URK::managed::find_class(command.image.c_str(), command.namespc.c_str(), command.class_name.c_str());
		if (!klass) {
			set_status("Selected type could no longer be resolved");
			return;
		}
		auto members = std::make_shared<ComponentInfo::Metadata>();
		for (const Inspect::FieldInfo& field : Inspect::fields_from_class(klass, true)) {
			class_browser_reflection_.fields.push_back(field);
			ComponentInfo::Field member = field_metadata(field);
			member.pointer_text = pointer_text(const_cast<void*>(field.handle));
			members->fields.push_back(std::move(member));
		}
		for (const Inspect::PropertyInfo& property : Inspect::properties_from_class(klass, true)) {
			class_browser_reflection_.properties.push_back(property);
			members->properties.push_back({ property.name, property.type_name, property.declaring_type.full_name,
									 property.can_read, property.can_write,
									 pointer_text(const_cast<void*>(property.handle)), property.is_value_type, property.is_enum,
									 !property.type_is_opaque,
									 property.type_is_opaque ? "Runtime-specific type; metadata only." : "" });
		}
		for (const Inspect::MethodInfo& method : Inspect::methods_from_class(klass, true)) {
			class_browser_reflection_.methods.push_back(method);
			// Types inspected through the Class Browser or MCP contribute names to
			// caller resolution. IL2CPP can index existing method pointers without
			// executing game code; Mono deliberately keeps its JIT-safe behavior.
			remember_managed_method(method);
			ComponentInfo::Method entry{};
			entry.name = method.name;
			entry.return_type = method.return_type;
			entry.declaring_type = method.declaring_type.full_name;
			entry.is_static = method.is_static;
			entry.pointer_text = pointer_text(const_cast<void*>(method.handle));
			entry.return_is_value_type = method.return_is_value_type;
			entry.return_is_enum = method.return_is_enum;
			entry.uses_generic_parameter = method.return_type_is_generic_parameter ||
				std::any_of(method.parameters.begin(), method.parameters.end(), [](const Inspect::MethodParamInfo& parameter) {
					return parameter.is_generic_parameter;
				});
			entry.runtime_callable = (!method.return_type_is_opaque || method.return_type_is_generic_parameter) &&
				std::none_of(method.parameters.begin(), method.parameters.end(), [](const Inspect::MethodParamInfo& parameter) {
					return parameter.is_opaque;
					});
			if (!entry.runtime_callable)
				entry.capability_reason = "Runtime-specific by-ref/internal signature; generic invocation is disabled.";
			for (const Inspect::MethodParamInfo& parameter : method.parameters) {
				entry.parameter_types.push_back(parameter.type_name);
				entry.parameter_names.push_back(parameter.name);
				entry.parameter_is_value_types.push_back(parameter.is_value_type);
				entry.parameter_is_enums.push_back(parameter.is_enum);
			}
			members->methods.push_back(std::move(entry));
		}
		working_.class_browser_members = std::move(members);
		set_status("Loaded members for " + working_.class_browser_members_query.full_name);
	}

} // namespace Explorer

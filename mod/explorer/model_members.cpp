// Copyright (c) 2026 Jadis0x. All rights reserved.
#include "explorer_model.h"
#include "model_shared.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using namespace URK::Unity;

namespace Explorer {
	namespace {

		std::optional<TypeObject> generic_type_from_text(std::string_view text, std::string& error,
			const ClassBrowserCatalog* catalog) {
			const std::size_t image_separator = text.find(':');
			if (image_separator == std::string_view::npos) {
				if (!catalog) {
					error = "Generic type search is not ready; use image:Namespace.Type or open the suggestion list";
					return std::nullopt;
				}
				const BrowserClassInfo* match = nullptr;
				for (const BrowserClassInfo& entry : catalog->classes) {
					const bool same_full_name = entry.full_name == text;
					const bool same_class_name = entry.class_name == text;
					if (!same_full_name && !same_class_name)
						continue;
					if (match) {
						error = "Generic type is ambiguous; choose a fully qualified suggestion";
						return std::nullopt;
					}
					match = &entry;
				}
				if (!match) {
					error = "Generic type was not found in loaded assemblies: " + std::string(text);
					return std::nullopt;
				}
				TypeRef type{ match->image, match->namespc, match->class_name };
				void* type_object = type.resolve_type_object();
				if (!type_object) {
					const char* detail = last_error();
					error = detail && detail[0] ? detail : "Generic type could not be resolved: " + std::string(text);
					return std::nullopt;
				}
				return TypeObject{ type_object };
			}
			if (image_separator == 0 || image_separator + 1 >= text.size()) {
				error = "Generic type must use image:Namespace.Type (example: bolt.user.dll:Photon.Bolt.IPlayerState)";
				return std::nullopt;
			}
			const std::string_view image = text.substr(0, image_separator);
			const std::string_view full_name = text.substr(image_separator + 1);
			const std::size_t type_separator = full_name.rfind('.');
			if (type_separator == std::string_view::npos || type_separator == 0 || type_separator + 1 >= full_name.size()) {
				error = "Generic type must include both namespace and class name";
				return std::nullopt;
			}
			TypeRef type{ image, full_name.substr(0, type_separator), full_name.substr(type_separator + 1) };
			void* type_object = type.resolve_type_object();
			if (!type_object) {
				const char* detail = last_error();
				error = detail && detail[0] ? detail : "Generic type could not be resolved: " + std::string(text);
				return std::nullopt;
			}
			return TypeObject{ type_object };
		}

		std::uint64_t member_reference_token(int component_id, bool property, std::size_t index) {
			return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(component_id)) << 32) |
				(property ? 0x80000000ull : 0ull) | static_cast<std::uint64_t>(index & 0x0fffffffu);
		}

	} // namespace

	void RuntimeModel::refresh_live_member_values(bool force) {
		if ((!force && event_refresh_pending_) || !safe_object_alive(selected_))
			return;

		for (ComponentInfo& component : working_.inspector.components) {
			if (!component.metadata)
				continue;
			const auto reflection = component_reflection_.find(component.instance_id);
			const Object target = resolve_component(component.instance_id);
			if (reflection == component_reflection_.end() || !safe_object_alive(target))
				continue;

			auto values = std::make_shared<ComponentInfo::LiveValues>();
			values->fields.resize(component.metadata->fields.size());
			values->properties.resize(component.metadata->properties.size());
			values->field_references.resize(component.metadata->fields.size());
			values->property_references.resize(component.metadata->properties.size());
			auto capture_reference = [&](Inspect::ValueInfo& value, std::uint64_t token,
				ComponentInfo::LiveValues::Reference& reference) {
					const bool is_reference =
						value.kind == Inspect::ValueKind::ObjectReference || value.kind == Inspect::ValueKind::ArrayReference;
					if (!is_reference || !value.object) {
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
					reference.token = token;
					reference.type_name = value.type_name;
					reference.display = value.display;
					reference.pointer_text = pointer_text(tracked.handle());
					reference.is_null = false;
					value.object = nullptr;
				};
			for (std::size_t index = 0; index < values->fields.size(); ++index) {
				const bool sampled =
					sampled_component_members_.contains(component_sample_token(component.instance_id, false, index));
				if (!component.metadata->fields[index].runtime_safe)
					values->fields[index] = Inspect::unavailable_value(component.metadata->fields[index].type_name,
						"Metadata only: " + component.metadata->fields[index].capability_reason);
				else if (index < reflection->second.fields.size() && sampled)
					values->fields[index] = guarded_managed_read(component.metadata->fields[index].type_name, [&] {
					return Inspect::ReadField(target, reflection->second.fields[index]);
						});
				else
					values->fields[index] =
					Inspect::unavailable_value(component.metadata->fields[index].type_name, "Not sampled");
				if (component.metadata->fields[index].runtime_safe && sampled &&
					index < reflection->second.fields.size())
					record_value_error(component.type_name + "." + component.metadata->fields[index].name,
						values->fields[index]);
				capture_reference(values->fields[index], member_reference_token(component.instance_id, false, index),
					values->field_references[index]);
			}
			for (std::size_t index = 0; index < values->properties.size(); ++index) {
				const bool sampled =
					sampled_component_members_.contains(component_sample_token(component.instance_id, true, index));
				if (!component.metadata->properties[index].runtime_safe)
					values->properties[index] = Inspect::unavailable_value(component.metadata->properties[index].type_name,
						"Metadata only: " + component.metadata->properties[index].capability_reason);
				else if (!component.metadata->properties[index].can_read) {
					// A stripped getter is not the end of the road: an
					// auto-property still has its backing field.
					const Inspect::FieldInfo *backing = backing_field_for(
						reflection->second.fields, component.metadata->properties[index].name,
						component.metadata->properties[index].declaring_type);
					values->properties[index] =
						backing && sampled
							? guarded_managed_read(component.metadata->properties[index].type_name,
								[&] { return Inspect::ReadField(target, *backing); })
						: backing ? Inspect::unavailable_value(component.metadata->properties[index].type_name,
							"Not sampled")
						: Inspect::unavailable_value(component.metadata->properties[index].type_name,
							"No getter in this build (IL2CPP stripped it)");
				}
				else if (index < reflection->second.properties.size() && sampled)
					values->properties[index] = guarded_managed_read(component.metadata->properties[index].type_name, [&] {
					return Inspect::ReadProperty(target, reflection->second.properties[index]);
						});
				else
					values->properties[index] =
					Inspect::unavailable_value(component.metadata->properties[index].type_name, "Not sampled");
				if (component.metadata->properties[index].runtime_safe &&
					component.metadata->properties[index].can_read && sampled &&
					index < reflection->second.properties.size())
					record_value_error(component.type_name + "." + component.metadata->properties[index].name,
						values->properties[index]);
				capture_reference(values->properties[index], member_reference_token(component.instance_id, true, index),
					values->property_references[index]);
			}
			component.live_values = std::move(values);
		}
	}

	void RuntimeModel::sample_member_value(const Command& command) {
		if (command.member_index < 0) {
			set_status("Cannot sample an invalid member index");
			return;
		}
		const std::size_t index = static_cast<std::size_t>(command.member_index);
		if (command.object_inspector_target) {
			if (!working_.object_inspector.valid || command.object_inspector_token == 0 ||
				command.object_inspector_token != working_.object_inspector.token) {
				set_status("Object Inspector tab changed before the value could be sampled");
				return;
			}
			(command.bool_value ? sampled_object_properties_ : sampled_object_fields_).insert(index);
			refresh_object_inspector_values();
		}
		else {
			sampled_component_members_.insert(component_sample_token(command.instance_id, command.bool_value, index));
			refresh_live_member_values();
		}
		set_status(std::string("Sampled ") + (command.bool_value ? "property" : "field") + " [" + std::to_string(index) +
			"]");
	}

#if defined(_WIN32)
	namespace {
		// MSVC forbids mixing __try/__except with objects that require
		// unwinding (e.g. std::string) in the same function (C2712).
		// apply_member() below builds many std::string locals, so the guarded
		// SetProperty/SetField call is isolated in this leaf function instead.
		template <bool IsProperty, typename Member>
		bool set_member_value_guarded(Object target, const Member& member, const Inspect::ValueInfo& value, bool& faulted) {
			faulted = false;
			__try {
				if constexpr (IsProperty)
					return Inspect::SetProperty(target, member, value);
				else
					return Inspect::SetField(target, member, value);
			}
			__except (capture_native_fault(_exception_info())) {
				faulted = true;
				return false;
			}
		}
	} // namespace
#endif

	void RuntimeModel::set_member_value(const Command& command, bool property, const Inspect::ValueInfo* prepared,
		bool verify) {
		const std::uint64_t lock_key = command.reference_token;
		if (command.unlock_value) {
			const auto found = locked_members_.find(lock_key);
			if (found != locked_members_.end()) {
				Inspect::FreeObjectHandle(found->second.value_root);
				locked_members_.erase(found);
			}
			working_.locked_member_keys.erase(lock_key);
			set_status("Member unlocked");
			return;
		}

		const bool nested = command.object_inspector_target;
		if (nested && (!working_.object_inspector.valid || command.object_inspector_token == 0 ||
			command.object_inspector_token != working_.object_inspector.token)) {
			set_status("Object Inspector tab changed before the value could be applied");
			return;
		}
		const Object target =
			nested ? Inspect::ResolveObjectHandle(object_inspector_handle_) : resolve_component(command.instance_id);
		if (nested && working_.object_inspector.is_array && !property) {
			if (!target || command.member_index < 0 ||
				static_cast<std::size_t>(command.member_index) >= working_.object_inspector.array_length) {
				set_status("Array element is no longer available");
				return;
			}
			Inspect::ValueInfo value{};
			Inspect::ObjectHandle argument_root{};
			const auto* array_class = static_cast<const URK::managed::Class*>(
				URK::managed::object_get_class(static_cast<URK::managed::Object*>(target.handle())));
			const auto* element_class = array_class ? URK::managed::class_get_element_class(array_class) : nullptr;
			const void* element_type = element_class ? URK::managed::class_get_type(element_class) : nullptr;
			const bool parsed = command_value(working_.object_inspector.array_element_type, command, value) ||
				(managed_reference_value_from_text(working_.object_inspector.array_element_type, element_type, command.text, value) ||
				 reference_value_from_text(working_.object_inspector.array_element_type, element_type,
					command.text, value, argument_root));
			if (!parsed) {
				set_status("Unsupported array element type: " + working_.object_inspector.array_element_type);
				return;
			}
			if (value.object) {
				const void* actual_class = URK::managed::object_get_class(value.object);
				const bool is_value_element = element_class && URK::managed::class_is_valuetype(element_class);
				const bool matches =
					element_class && actual_class &&
					(is_value_element ? element_class == actual_class
						: URK::managed::class_is_assignable_from(element_class, actual_class) != 0);
				if (!matches) {
					Inspect::FreeObjectHandle(argument_root);
					set_status("Array reference type mismatch: expected " + working_.object_inspector.array_element_type);
					return;
				}
			}
			Inspect::ValueInfo array{};
			array.kind = Inspect::ValueKind::ArrayReference;
			array.type_name = working_.object_inspector.type_name;
			array.object = target.handle();
			array.readable = true;
			if (Inspect::SetArrayElement(array, static_cast<std::size_t>(command.member_index), value)) {
				const Inspect::ValueInfo actual = guarded_managed_read(working_.object_inspector.array_element_type, [&] {
					return Inspect::ReadArrayElement(array, static_cast<std::size_t>(command.member_index));
					});
				if (values_equivalent(value, actual))
					set_status("Set array element [" + std::to_string(command.member_index) + "] (verified)");
				else
					set_status("Write verification failed for array element [" + std::to_string(command.member_index) +
						"]: requested " + value.display + ", read " + actual.display);
				refresh_object_inspector_values(true);
			}
			else {
				capture_last_error("Set array element");
			}
			Inspect::FreeObjectHandle(argument_root);
			return;
		}
		const ComponentReflection* reflection = nested ? &object_inspector_reflection_ : nullptr;
		if (!nested) {
			const auto found = component_reflection_.find(command.instance_id);
			if (found != component_reflection_.end())
				reflection = &found->second;
		}
		if (!reflection || !target || (!nested && !safe_object_alive(target)) || command.member_index < 0) {
			set_status("Member is no longer available");
			return;
		}

		Inspect::ValueInfo value{};
		Inspect::ObjectHandle argument_root{};
		bool written = false;
		bool failure_reported = false;
		auto publish_write_result = [&](bool succeeded) {
			Snapshot::MemberWriteResult record{};
			record.component_instance_id = command.instance_id;
			record.member_index = static_cast<std::size_t>(command.member_index);
			record.property = property;
			record.object_inspector_token = nested ? command.object_inspector_token : 0;
			record.succeeded = succeeded;
			record.display = working_.status.empty() ?
				(succeeded ? "Write completed" : "Write failed") : working_.status;
			for (auto it = working_.member_write_results.begin(); it != working_.member_write_results.end();) {
				const Snapshot::MemberWriteResult& previous = it->second;
				if (previous.component_instance_id != record.component_instance_id ||
					previous.member_index != record.member_index || previous.property != record.property ||
					previous.object_inspector_token != record.object_inspector_token) {
					++it;
					continue;
				}
				it = working_.member_write_results.erase(it);
			}
			working_.member_write_results[next_method_result_id_++] = std::move(record);
		};
		const bool keep_locked = !prepared && lock_key != 0 && (command.lock_value || locked_members_.contains(lock_key));
		auto apply_member = [&](const auto& member) {
			constexpr bool is_property = requires { member.can_write; };
			if (member.type_is_opaque) {
				record_flight("BLOCKED", std::string(is_property ? "Write property " : "Write field ") + member.name,
					"runtime-specific type");
				set_status(std::string(is_property ? "Property " : "Field ") + member.name +
					" is metadata-only: " + member.type_name);
				failure_reported = true;
				return;
			}
			record_flight("TARGET", std::string(is_property ? "Write property " : "Write field ") + member.name,
				member.declaring_type.full_name.empty() ? member.type_name
					: member.declaring_type.full_name + " : " + member.type_name);
			if (prepared) {
				value = *prepared;
			}
			else {
				const bool parsed = command_value(member.type_name, command, value) ||
					(member.is_enum && enum_value_from_text(member.type_name, command.text, value)) ||
					(!member.is_enum && (managed_reference_value_from_text(member.type_name, member.type, command.text, value) ||
						reference_value_from_text(member.type_name, member.type, command.text, value, argument_root)));
				if (!parsed) {
					if (!member.is_enum) {
						set_status(std::string("Invalid reference for ") + (is_property ? "property " : "field ") +
							member.name + ": expected " + member.type_name +
							" (paste a Copy Ptr or choose a pinned reference)");
					}
					else {
						set_status(std::string("Unsupported ") + (is_property ? "property" : "field") +
							" type: " + member.type_name);
					}
					failure_reported = true;
					return;
				}
				if (keep_locked && value.kind == Inspect::ValueKind::String && !value.object) {
					value.object =
						URK::managed::string_new_len(value.display.data(), static_cast<std::uint32_t>(value.display.size()));
					if (value.object)
						argument_root = Inspect::PinValue(value);
					if (!argument_root.handle) {
						capture_last_error("Lock string value");
						value.object = nullptr;
						failure_reported = true;
						return;
					}
				}
			}

#if defined(_WIN32)
			bool faulted = false;
			written = set_member_value_guarded<is_property>(target, member, value, faulted);
			if (faulted) {
				Inspect::FreeObjectHandle(argument_root);
				clear_error();
				set_status(std::string("Set ") + (is_property ? "property" : "field") +
					" blocked an invalid native access");
				failure_reported = true;
				return;
			}
#else
			if constexpr (is_property)
				written = Inspect::SetProperty(target, member, value);
			else
				written = Inspect::SetField(target, member, value);
#endif

			if (written && verify) {
				if constexpr (is_property) {
					if (member.can_read) {
						const Inspect::ValueInfo actual =
							guarded_managed_read(member.type_name, [&] { return Inspect::ReadProperty(target, member); });
						if (values_equivalent(value, actual))
							set_status("Set property " + member.name + " (verified)");
						else
							set_status("Write verification failed for property " + member.name + ": requested " +
								value.display + ", read " + actual.display);
					}
					else {
						set_status("Set write-only property " + member.name + " (not verifiable)");
					}
				}
				else {
					const Inspect::ValueInfo actual =
						guarded_managed_read(member.type_name, [&] { return Inspect::ReadField(target, member); });
					if (values_equivalent(value, actual))
						set_status("Set field " + member.name + " (verified)");
					else
						set_status("Write verification failed for field " + member.name + ": requested " + value.display +
							", read " + actual.display);
				}
			}

			if (written && keep_locked) {
				const auto old = locked_members_.find(lock_key);
				if (old != locked_members_.end())
					Inspect::FreeObjectHandle(old->second.value_root);
				LockedMember locked{};
				locked.command = command;
				locked.command.lock_value = false;
				locked.command.unlock_value = false;
				locked.value = value;
				locked.value_root = argument_root;
				argument_root = {};
				locked_members_[lock_key] = std::move(locked);
				working_.locked_member_keys.insert(lock_key);
				set_status(std::string(is_property ? "Property " : "Field ") + member.name + " locked");
			}
			};

		const std::size_t index = static_cast<std::size_t>(command.member_index);
		if (property) {
			if (index >= reflection->properties.size()) {
				set_status("Property is no longer available");
				return;
			}
			apply_member(reflection->properties[index]);
		}
		else {
			if (index >= reflection->fields.size()) {
				set_status("Field is no longer available");
				return;
			}
			apply_member(reflection->fields[index]);
		}
		Inspect::FreeObjectHandle(argument_root);
		if (written && nested && working_.object_inspector.is_value_type &&
			working_.object_inspector.value_origin_component_id != 0) {
			write_back_value_type_object_inspector();
		}
		if (written && prepared && !verify)
			return;
		if (written) {
			for (auto& [_, watch] : field_watches_) {
				if (watch.snapshot.component_instance_id == command.instance_id &&
					watch.snapshot.object_inspector_token == (nested ? command.object_inspector_token : 0) &&
					watch.snapshot.field_index == index && watch.snapshot.property == property)
					watch.explorer_write_pending = true;
			}
			// Keep the write result before refreshing the component.
			publish_write_result(true);
			// Refresh the component so the new value is visible.
			if (nested)
				refresh_object_inspector_values(true);
			else
				refresh_live_member_values(true);
		}
		else if (!failure_reported) {
			capture_last_error(property ? "Set property" : "Set field");
			publish_write_result(false);
		}
		else {
			publish_write_result(false);
		}
	}

	void RuntimeModel::apply_locked_members() {
		for (auto& [_, locked] : locked_members_) {
			const bool property = locked.command.kind == CommandKind::SetPropertyValue;
			set_member_value(locked.command, property, &locked.value, false);
			if (native_faulted_.load(std::memory_order_acquire))
				return;
		}
	}

	void RuntimeModel::clear_locked_members(bool nested_only) {
		for (auto it = locked_members_.begin(); it != locked_members_.end();) {
			if (nested_only && !it->second.command.object_inspector_target) {
				++it;
				continue;
			}
			Inspect::FreeObjectHandle(it->second.value_root);
			working_.locked_member_keys.erase(it->first);
			it = locked_members_.erase(it);
		}
	}

	void RuntimeModel::invoke_method(const Command& command) {
		const bool nested = command.object_inspector_target;
		const bool browser = command.class_browser_target;
		if (nested && (!working_.object_inspector.valid || command.object_inspector_token == 0 ||
			command.object_inspector_token != working_.object_inspector.token)) {
			set_status("Object Inspector tab changed before the method could be invoked");
			return;
		}
		if (browser &&
			(working_.class_browser_members_query.image != command.image ||
			 working_.class_browser_members_query.namespc != command.namespc ||
			 working_.class_browser_members_query.class_name != command.class_name)) {
			set_status("Class Browser selection changed before the method could be invoked");
			return;
		}
		const ComponentReflection* reflection =
			browser ? &class_browser_reflection_ : nested ? &object_inspector_reflection_ : nullptr;
		if (!nested && !browser) {
			const auto found = component_reflection_.find(command.instance_id);
			if (found != component_reflection_.end())
				reflection = &found->second;
		}
		if (!reflection || command.member_index < 0 ||
			static_cast<std::size_t>(command.member_index) >= reflection->methods.size()) {
			set_status("Method is no longer available");
			return;
		}
		const Inspect::MethodInfo& method = reflection->methods[command.member_index];
		const auto invocation_started = Clock::now();
		auto publish_method_result = [&](bool succeeded, std::string display, const Inspect::ValueInfo* value = nullptr) {
			Snapshot::MethodResult record{};
			record.component_instance_id = command.instance_id;
			record.method_index = static_cast<std::size_t>(command.member_index);
			record.object_inspector_token = browser ? command.object_inspector_token
				: nested ? command.object_inspector_token : 0;
			record.return_type = method.return_type;
			record.succeeded = succeeded;
			record.elapsed_milliseconds = std::chrono::duration<double, std::milli>(Clock::now() - invocation_started).count();
			record.display = std::move(display);
			for (auto it = working_.method_results.begin(); it != working_.method_results.end();) {
				const Snapshot::MethodResult& previous = it->second;
				if (previous.component_instance_id != record.component_instance_id ||
					previous.method_index != record.method_index ||
					previous.object_inspector_token != record.object_inspector_token) {
					++it;
					continue;
				}
				if (previous.reference.token != 0)
					release_reference_handle(previous.reference.token);
				it = working_.method_results.erase(it);
			}
			// Any result that still carries a managed pointer is worth retaining,
			// including a boxed value type and a result whose type could not be
			// named: the token is the only way to open it in an inspector.
			if (value && value->object) {
				std::uint64_t reference_token = 0;
				do {
					reference_token = 0x1000000000000000ull | (next_reference_token_++ & 0x0fffffffffffffffull);
				} while (reference_handles_.contains(reference_token));
				// Root object results until the method result is replaced.
				Inspect::ObjectHandle rooted = Inspect::PinObject(Object{value->object});
				const Object returned = Inspect::ResolveObjectHandle(rooted);
				if (rooted.handle && returned) {
					reference_handles_[reference_token] = rooted;
					record.reference = { reference_token, value->type_name, value->display, pointer_text(returned.handle()), false };
				}
				else {
					Inspect::FreeObjectHandle(rooted);
					record.display += " (object could not be retained for inspection)";
				}
			}
			working_.method_results[next_method_result_id_++] = std::move(record);
		};
		// Only the arguments have to be marshalled into the callee. A
		// runtime-specific or generic return is decoded from the runtime class of
		// whatever comes back, so it no longer blocks the call.
		if (std::any_of(method.parameters.begin(), method.parameters.end(),
			[](const Inspect::MethodParamInfo& parameter) { return parameter.is_opaque && !parameter.is_generic_parameter; })) {
			record_flight("BLOCKED", "Execute " + method.name, "runtime-specific signature");
			const std::string message = "Method requires runtime-specific marshalling";
			publish_method_result(false, message);
			set_status("Invoke " + method.name + " failed: " + message);
			return;
		}
		record_flight("TARGET", "Execute " + method.name,
			method.declaring_type.full_name.empty() ? method.return_type
				: method.declaring_type.full_name + " -> " + method.return_type);
		Object target{};
		if (browser) {
			if (command.reference_token != 0) {
				const auto found = class_browser_handles_.find(command.reference_token);
				if (found != class_browser_handles_.end())
					target = Inspect::ResolveObjectHandle(found->second);
			}
		}
		else {
			target = nested ? Inspect::ResolveObjectHandle(object_inspector_handle_) : resolve_component(command.instance_id);
		}
		if (!method.is_static && (!target || (!nested && !browser && !safe_object_alive(target)))) {
			publish_method_result(false, "Method target is no longer available");
			set_status("Invoke " + method.name + " failed: method target is no longer available");
			return;
		}
		if (command.method_arguments.size() != method.parameters.size()) {
			publish_method_result(false, "Argument count does not match the method signature");
			set_status("Invoke " + method.name + " failed: argument count does not match");
			return;
		}
		std::vector<Inspect::ValueInfo> arguments;
		std::vector<Inspect::ObjectHandle> roots;
		arguments.reserve(method.parameters.size());
		roots.reserve(method.parameters.size());
		const auto release_roots = [&] {
			for (Inspect::ObjectHandle& root : roots)
				Inspect::FreeObjectHandle(root);
			roots.clear();
			};
		for (std::size_t index = 0; index < method.parameters.size(); ++index) {
			const Inspect::MethodParamInfo& parameter = method.parameters[index];
			Command argument_command{};
			argument_command.text = command.method_arguments[index];
			const std::string normalized = normalized_type(parameter.type_name);
			argument_command.bool_value =
				normalized == "system.boolean" && (argument_command.text == "true" || argument_command.text == "1");
			Inspect::ValueInfo value{};
			if (command_value(parameter.type_name, argument_command, value)) {
				arguments.push_back(std::move(value));
				continue;
			}
			const Inspect::TypeInfo parameter_type = Inspect::DescribeType(parameter.type);
			if (parameter_type.is_enum && enum_value_from_text(parameter.type_name, argument_command.text, value)) {
				arguments.push_back(std::move(value));
				continue;
			}
			Inspect::ObjectHandle rooted{};
			if (!parameter_type.is_enum &&
				(managed_reference_value_from_text(parameter.type_name, parameter.type, argument_command.text, value) ||
				 reference_value_from_text(parameter.type_name, parameter.type, argument_command.text, value, rooted))) {
				if (rooted.handle)
					roots.push_back(rooted);
				arguments.push_back(std::move(value));
				continue;
			}
			if (rooted.handle)
				Inspect::FreeObjectHandle(rooted);
			{
				release_roots();
				const char* conversion_error = last_error();
				const std::string message = "Argument " + std::to_string(index + 1) + " is invalid: expected " +
					parameter.type_name + " (use a value, null/default, a pinned reference, or Copy Ptr address)" +
					(conversion_error && conversion_error[0] ? std::string("; ") + conversion_error : std::string{});
				publish_method_result(false, message);
				set_status("Invoke " + method.name + " failed: " + message);
				return;
			}
		}
		std::vector<TypeObject> generic_types;
		if (method.return_type_is_generic_parameter || std::any_of(method.parameters.begin(), method.parameters.end(),
			[](const Inspect::MethodParamInfo& parameter) { return parameter.is_generic_parameter; })) {
			if (command.generic_type_arguments.empty()) {
				release_roots();
				const std::string message = "Generic method requires a concrete type (for example bolt.user.dll:Photon.Bolt.IPlayerState)";
				publish_method_result(false, message);
				set_status("Invoke " + method.name + " failed: " + message);
				return;
			}
			for (const std::string& text : command.generic_type_arguments) {
				std::string error;
				const std::optional<TypeObject> resolved = generic_type_from_text(text, error,
					class_browser_catalog_.get());
				if (!resolved) {
					release_roots();
					publish_method_result(false, error);
					set_status("Invoke " + method.name + " failed: " + error);
					return;
				}
				generic_types.push_back(*resolved);
			}
		}
		const Inspect::ValueInfo result = guarded_managed_read(method.return_type, [&] {
			return generic_types.empty() ? Inspect::InvokeMethod(target, method, arguments)
				: Inspect::InvokeGenericMethod(target, method, generic_types, arguments);
		});
		release_roots();
		if (!result.readable) {
			const char* error = last_error();
			if (error && is_expected_empty_container_error(error)) {
				publish_method_result(false, "Skipped: map is empty");
				set_status("Invoke " + method.name + " skipped: map is empty");
				return;
			}
			const std::string message = error && error[0] ? error :
				(result.display.empty() ? "runtime invocation failed without an error message" : result.display);
			publish_method_result(false, message);
			set_status("Invoke " + method.name + " failed: " + message);
			return;
		}
		if (!nested && !browser) {
			const auto component = std::find_if(working_.inspector.components.begin(), working_.inspector.components.end(),
				[&command](const ComponentInfo& info) { return info.instance_id == command.instance_id; });
			if (component != working_.inspector.components.end() &&
				component->dynamic_bridge.type_getter_method_index == command.member_index) {
				if (result.kind == Inspect::ValueKind::String)
					component->dynamic_bridge.behaviour_type = result.display;
				else
					component->dynamic_bridge.diagnostic = "Type getter completed but did not return a managed string.";
			}
		}
		publish_method_result(true, result.display.empty() ? "<no display value>" : result.display, &result);
		set_status("Invoked " + method.name + " -> " + result.display);
	}

	void RuntimeModel::delete_component(int component_instance_id) {
		const Object component = resolve_component(component_instance_id);
		if (!safe_object_alive(component)) {
			set_status("Component is no longer available");
			return;
		}
		Object::Destroy(component);
		capture_last_error("Delete component");
		request_refresh();
		event_refresh_pending_ = true;
		event_refresh_due_ = Clock::now() + kEventRefreshDebounce;
	}

	bool RuntimeModel::managed_reference_value_from_text(std::string_view type_name, const void* destination_type,
		std::string_view text, Inspect::ValueInfo& value) {
		constexpr std::string_view kPrefix = "@ref:";
		if (!text.starts_with(kPrefix))
			return false;
		const std::string token_text(text.substr(kPrefix.size()));
		char* end = nullptr;
		errno = 0;
		const unsigned long long parsed = std::strtoull(token_text.c_str(), &end, 10);
		if (errno != 0 || end == token_text.c_str() || *end != '\0' || parsed == 0) {
			detail::set_error("Pinned reference token is malformed");
			return false;
		}
		std::string error;
		if (managed_references_.resolve(static_cast<std::uint64_t>(parsed), destination_type, type_name, value, error))
			return true;
		detail::set_error(error.empty() ? "Pinned reference could not be resolved" : error);
		return false;
	}

	void RuntimeModel::pin_managed_reference(const Command& command) {
		Object source{};
		std::string source_name;
		if (command.reference_token != 0) {
			const auto found = reference_handles_.find(command.reference_token);
			if (found == reference_handles_.end()) {
				set_status("Save reference failed: the inspected value changed before it could be saved");
				return;
			}
			source = Inspect::ResolveObjectHandle(found->second);
			source_name = "Inspector value";
		}
		else if (command.object_inspector_target) {
			source = Inspect::ResolveObjectHandle(object_inspector_handle_);
			source_name = "Object Inspector target";
		}
		else {
			source = resolve_selected_object();
			source_name = "Selected GameObject";
		}
		if (!source) {
			const char* detail = last_error();
			set_status(std::string("Save reference failed: ") +
				(detail && detail[0] ? detail : "target is no longer available"));
			return;
		}
		std::string error;
		std::uint64_t token = 0;
		if (!managed_references_.capture(source, source_name, error, token)) {
			set_status("Save reference failed: " + error);
			return;
		}
		set_status("Saved " + source_name + " as reference #" + std::to_string(token & 0x0fffffffffffffffull));
	}

} // namespace Explorer

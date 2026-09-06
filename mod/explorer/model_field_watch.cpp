// Copyright (c) 2026 Jadis0x. All rights reserved.
#include "explorer_model.h"
#include "model_shared.h"

#include "method_trace_format.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_set>

using namespace URK::Unity;

namespace Explorer {
	namespace {

		std::string watched_value_display(const Inspect::ValueInfo& value) {
			if (value.readable)
				return value.display.empty() ? "<empty>" : value.display;
			if (value.display.empty() || value.display == "Not sampled")
				return "<not available>";
			return "<unavailable: " + value.display + ">";
		}

	} // namespace

	bool RuntimeModel::has_active_field_watches() const {
		return std::any_of(field_watches_.begin(), field_watches_.end(),
			[](const auto& entry) { return entry.second.snapshot.active; });
	}

	ComponentInfo::LiveValues::Reference RuntimeModel::watch_reference_for(const Inspect::ValueInfo& value) {
		ComponentInfo::LiveValues::Reference reference{};
		if ((value.kind != Inspect::ValueKind::ObjectReference && value.kind != Inspect::ValueKind::ArrayReference) ||
			!value.object)
			return reference;
		Inspect::ObjectHandle rooted = tracked_reference_handle(value);
		if (!rooted.handle)
			return reference;
		const Object tracked = Inspect::ResolveObjectHandle(rooted);
		if (!tracked) {
			Inspect::FreeObjectHandle(rooted);
			return reference;
		}
		const std::uint64_t token = 0xb000000000000000ull | (next_reference_token_++ & 0x0fffffffffffffffull);
		reference_handles_[token] = rooted;
		reference.token = token;
		reference.type_name = value.type_name;
		reference.display = value.display;
		reference.pointer_text = pointer_text(tracked.handle());
		reference.is_null = false;
		return reference;
	}

	void RuntimeModel::release_field_watch_references(Snapshot::FieldWatch& watch) {
		std::unordered_set<std::uint64_t> tokens;
		if (watch.current_reference.token != 0)
			tokens.insert(watch.current_reference.token);
		watch.current_reference = {};
		for (Snapshot::FieldWatchEvent& event : watch.events) {
			if (event.current_reference.token != 0)
				tokens.insert(event.current_reference.token);
			event.current_reference = {};
		}
		for (const std::uint64_t token : tokens)
			release_reference_handle(token);
	}

	void RuntimeModel::set_field_watch(const Command& command) {
		if (command.member_index < 0) {
			set_status("Cannot watch an invalid member");
			return;
		}
		const std::size_t field_index = static_cast<std::size_t>(command.member_index);
		auto found = std::find_if(field_watches_.begin(), field_watches_.end(), [&](const auto& entry) {
			return entry.second.snapshot.component_instance_id == command.instance_id &&
				entry.second.snapshot.object_inspector_token == command.object_inspector_token &&
				entry.second.snapshot.field_index == field_index &&
				entry.second.snapshot.property == command.member_is_property;
			});
		if (!command.bool_value) {
			if (found == field_watches_.end() || !found->second.snapshot.active) {
				set_status("Value watch is no longer active");
				return;
			}
			found->second.snapshot.active = false;
			detach_setter_hook(found->second);
			set_status("Value watch stopped");
			return;
		}

		const bool nested = command.object_inspector_target;
		const ComponentInfo::Metadata* metadata = nullptr;
		const ComponentReflection* reflection = nullptr;
		Object target{};
		std::string component_type;
		if (nested) {
			if (!working_.object_inspector.valid || command.object_inspector_token == 0 ||
				command.object_inspector_token != working_.object_inspector.token ||
				!working_.object_inspector.component.metadata) {
				set_status("Object Inspector member is no longer available to watch");
				return;
			}
			metadata = working_.object_inspector.component.metadata.get();
			reflection = &object_inspector_reflection_;
			target = Inspect::ResolveObjectHandle(object_inspector_handle_);
			component_type = working_.object_inspector.type_name;
		}
		else {
			const auto component =
				std::find_if(working_.inspector.components.begin(), working_.inspector.components.end(),
					[&command](const ComponentInfo& info) { return info.instance_id == command.instance_id; });
			const auto component_reflection = component_reflection_.find(command.instance_id);
			if (component == working_.inspector.components.end() || !component->metadata ||
				component_reflection == component_reflection_.end()) {
				set_status("Component member is no longer available to watch");
				return;
			}
			metadata = component->metadata.get();
			reflection = &component_reflection->second;
			target = resolve_component(command.instance_id);
			component_type = component->type_name;
		}
		const bool property = command.member_is_property;
		const bool member_available = property
			? field_index < metadata->properties.size() && field_index < reflection->properties.size() &&
				metadata->properties[field_index].can_read
			: field_index < metadata->fields.size() && field_index < reflection->fields.size();
		if (!target || !metadata || !reflection || !member_available) {
			set_status(property ? "Property is not readable or is no longer available to watch"
				: "Field is no longer available to watch");
			return;
		}

		FieldWatchState* state = nullptr;
		if (found == field_watches_.end()) {
			FieldWatchState created{};
			created.snapshot.id = next_field_watch_id_++;
			created.snapshot.component_instance_id = command.instance_id;
			created.snapshot.object_inspector_token = nested ? command.object_inspector_token : 0;
			created.snapshot.field_index = field_index;
			created.snapshot.property = property;
			created.snapshot.component_type = component_type;
			created.snapshot.field_name = property ? metadata->properties[field_index].name : metadata->fields[field_index].name;
			created.snapshot.field_type = property ? metadata->properties[field_index].type_name : metadata->fields[field_index].type_name;
			if (property)
				created.property = reflection->properties[field_index];
			else
				created.field = reflection->fields[field_index];
			created.target_handle = Inspect::WeakObject(target);
			if (!created.target_handle.handle) {
				set_status("Value watch could not retain its runtime target");
				return;
			}
			const auto inserted = field_watches_.emplace(created.snapshot.id, std::move(created));
			state = &inserted.first->second;
		}
		else {
			state = &found->second;
			if (property)
				state->property = reflection->properties[field_index];
			else
				state->field = reflection->fields[field_index];
			if (!Inspect::ResolveObjectHandle(state->target_handle)) {
				Inspect::FreeObjectHandle(state->target_handle);
				state->target_handle = Inspect::WeakObject(target);
			}
			if (!state->target_handle.handle) {
				set_status("Value watch could not retain its runtime target");
				return;
			}
			state->snapshot.active = true;
			release_field_watch_references(state->snapshot);
			state->snapshot.events.clear();
			state->snapshot.samples.clear();
			state->snapshot.change_count = 0;
			state->snapshot.alarm_count = 0;
			state->alarm_latched = false;
		}

		const Inspect::ValueInfo value = guarded_managed_read(state->snapshot.field_type, [&] {
			const Object watched_target = Inspect::ResolveObjectHandle(state->target_handle);
			return property ? Inspect::ReadProperty(watched_target, state->property)
				: Inspect::ReadField(watched_target, state->field);
			});
		state->started = Clock::now();
		state->last_value = value;
		state->has_baseline = value.readable;
		state->snapshot.active = true;
		state->snapshot.value_available = value.readable;
		state->snapshot.current_value = watched_value_display(value);
		state->snapshot.current_reference = watch_reference_for(value);
		if (nested)
			(property ? sampled_object_properties_ : sampled_object_fields_).insert(field_index);
		else
			sampled_component_members_.insert(component_sample_token(command.instance_id, property, field_index));
		// A property with a setter can report writes exactly; a raw field has no
		// managed setter to hook and stays on sampling.
		const bool hooked = attach_setter_hook(*state, target);
		state->snapshot.setter_hooked = hooked;
		set_status("Watching " + state->snapshot.component_type + "." + state->snapshot.field_name +
			" (" + (property ? "property" : "field") + ")" +
			(hooked ? " with a setter hook" : " for value changes"));
	}

	void RuntimeModel::configure_field_watch(const Command& command) {
		const auto found = field_watches_.find(command.reference_token);
		if (found == field_watches_.end()) {
			set_status("Value watch is no longer available");
			return;
		}
		const int raw_condition = std::clamp(command.int_value, 0,
			static_cast<int>(WatchAnalysis::AlarmCondition::NotEqual));
		found->second.snapshot.alarm_condition = static_cast<WatchAnalysis::AlarmCondition>(raw_condition);
		found->second.snapshot.alarm_threshold = static_cast<double>(command.float_value);
		found->second.snapshot.alarm_active = false;
		found->second.alarm_latched = false;
		set_status(found->second.snapshot.alarm_condition == WatchAnalysis::AlarmCondition::Disabled
			? "Watch alarm disabled" : "Watch alarm configured");
	}

	void RuntimeModel::clear_field_watch(std::uint64_t id) {
		const auto found = field_watches_.find(id);
		if (found == field_watches_.end()) {
			set_status("Field watch is no longer available");
			return;
		}
		release_field_watch_references(found->second.snapshot);
		found->second.snapshot.events.clear();
		found->second.snapshot.samples.clear();
		found->second.snapshot.change_count = 0;
		found->second.snapshot.alarm_count = 0;
		found->second.snapshot.alarm_active = false;
		found->second.has_baseline = false;
		found->second.alarm_latched = false;
		found->second.snapshot.value_available = false;
		found->second.snapshot.current_value = "Waiting for a sample...";
		found->second.started = Clock::now();
		set_status("Field change history cleared");
	}

	void RuntimeModel::close_field_watch(std::uint64_t id) {
		const auto found = field_watches_.find(id);
		if (found == field_watches_.end()) {
			set_status("Field watch is no longer available");
			return;
		}
		if (found->second.snapshot.active) {
			set_status("Stop a field watch before closing it");
			return;
		}
		detach_setter_hook(found->second);
		release_field_watch_references(found->second.snapshot);
		Inspect::FreeObjectHandle(found->second.target_handle);
		field_watches_.erase(found);
		set_status("Closed field watch tab");
	}

	bool RuntimeModel::attach_setter_hook(FieldWatchState& state, URK::Unity::Object target) {
		detach_setter_hook(state);
		if (!state.snapshot.property || !state.property.set_method || !target)
			return false;
		// A setter shared by every instance is filtered down to this object.
		const Inspect::MethodInfo setter = Inspect::method_info(
			static_cast<const URK::managed::Method*>(state.property.set_method), state.property.declaring_type);
		std::string error;
		if (!MethodTracer::start(setter, false, target.handle(), error)) {
			record_flight("WATCH", "Setter hook unavailable", error);
			return false;
		}
		state.setter_trace = MethodTracer::last_started_id();
		state.setter_calls_seen = 0;
		return state.setter_trace != 0;
	}

	void RuntimeModel::detach_setter_hook(FieldWatchState& state) {
		if (state.setter_trace == 0)
			return;
		MethodTracer::stop(state.setter_trace);
		MethodTracer::close(state.setter_trace);
		state.setter_trace = 0;
		state.setter_calls_seen = 0;
	}

	void RuntimeModel::release_all_field_watches() {
		for (auto& [_, state] : field_watches_) {
			detach_setter_hook(state);
			release_field_watch_references(state.snapshot);
			Inspect::FreeObjectHandle(state.target_handle);
		}
		field_watches_.clear();
		working_.field_watches.clear();
	}

	void RuntimeModel::refresh_field_watches(bool record_sample) {
		constexpr std::size_t kMaxFieldWatchEvents = 256;
		constexpr std::size_t kMaxFieldWatchSamples = 512;
		const Clock::time_point now = Clock::now();
		for (auto& [_, state] : field_watches_) {
			Snapshot::FieldWatch& watch = state.snapshot;
			if (!watch.active)
				continue;
			const Object target = Inspect::ResolveObjectHandle(state.target_handle);
			const bool metadata_available = watch.property ? state.property.handle != nullptr : state.field.handle != nullptr;
			if (!target || !metadata_available) {
				watch.active = false;
				watch.value_available = false;
				watch.current_value = "<runtime target is no longer available>";
				set_status("Field watch stopped because its runtime target was released");
				continue;
			}
			const Inspect::ValueInfo value = guarded_managed_read(watch.field_type, [&] {
				return watch.property ? Inspect::ReadProperty(target, state.property) : Inspect::ReadField(target, state.field);
				});
			watch.value_available = value.readable;
			watch.current_value = watched_value_display(value);
			if (!value.readable) {
				record_value_error(watch.component_type + "." + watch.field_name, value);
				continue;
			}
			// A hooked setter reports the exact number of writes, so a value that
			// was written and reverted between two samples still shows up.
			std::uint64_t setter_writes = 0;
			std::uintptr_t setter_caller = 0;
			if (state.setter_trace != 0) {
				const MethodTracer::WriteSignal signal = MethodTracer::write_signal(state.setter_trace);
				if (!signal.active) {
					state.setter_trace = 0;
					watch.setter_hooked = false;
				} else if (signal.total_calls > state.setter_calls_seen) {
					setter_writes = signal.total_calls - state.setter_calls_seen;
					setter_caller = signal.last_caller;
					state.setter_calls_seen = signal.total_calls;
				}
			}
			const double elapsed = std::chrono::duration<double>(now - state.started).count();
			if (const std::optional<double> numeric = WatchAnalysis::numeric_value(value)) {
				if (record_sample) {
					if (watch.samples.size() == kMaxFieldWatchSamples)
						watch.samples.erase(watch.samples.begin());
					watch.samples.push_back({elapsed, static_cast<float>(*numeric)});
				}
				const bool alarm_now = WatchAnalysis::evaluate(watch.alarm_condition, *numeric, watch.alarm_threshold);
				if (alarm_now && !state.alarm_latched)
					++watch.alarm_count;
				state.alarm_latched = alarm_now;
				watch.alarm_active = alarm_now;
			}
			else {
				watch.alarm_active = false;
				state.alarm_latched = false;
			}
			if (!state.has_baseline) {
				state.last_value = value;
				state.has_baseline = true;
				state.started = now;
				release_reference_handle(watch.current_reference.token);
				watch.current_reference = watch_reference_for(value);
				continue;
			}
			if (values_equivalent(state.last_value, value) && setter_writes == 0)
				continue;
			Snapshot::FieldWatchEvent event{};
			event.sequence = ++watch.change_count;
			event.seconds_since_start = elapsed;
			event.previous_value = watched_value_display(state.last_value);
			event.current_value = watched_value_display(value);
			if (state.explorer_write_pending)
				event.source = "Explorer write";
			else if (setter_writes != 0)
				event.source = "Setter hook" +
					(setter_writes > 1 ? " (" + std::to_string(setter_writes) + " writes)" : std::string{}) +
					(setter_caller != 0 ? " from " + MethodTraceFormat::address(setter_caller) : std::string{});
			else
				event.source = watch.property ? "Runtime / property getter sample"
					: "Runtime / sampled write window";
			event.alarm_triggered = watch.alarm_active;
			state.explorer_write_pending = false;
			// The newest history entry already retains the current reference.
			if (watch.current_reference.token != 0 &&
				(watch.events.empty() || watch.events.back().current_reference.token != watch.current_reference.token))
				release_reference_handle(watch.current_reference.token);
			if (watch.events.size() == kMaxFieldWatchEvents) {
				release_reference_handle(watch.events.front().current_reference.token);
				watch.events.erase(watch.events.begin());
			}
			event.current_reference = watch_reference_for(value);
			watch.current_reference = event.current_reference;
			watch.events.push_back(std::move(event));
			state.last_value = value;
		}
	}

} // namespace Explorer

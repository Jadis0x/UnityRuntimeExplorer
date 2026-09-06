// Copyright (c) 2026 Jadis0x. All rights reserved.
#include "explorer_model.h"
#include "model_shared.h"

#include "method_trace_value_decoder.h"
#include "method_trace_format.h"
#include "diagnostic_bundle.h"
#include "config/mod_config.h"

#include "support/mod_log.h"
#include "ui/highlight.h"

#include "sdk/runtime_api.h"
#include "sdk/unity/unity_inspect.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>

using namespace URK::Unity;

namespace Explorer {
	namespace {
		constexpr auto kInspectorInterval = std::chrono::milliseconds(250);
		constexpr auto kHighlightInterval = std::chrono::milliseconds(66);
		constexpr auto kMemberValueInterval = std::chrono::milliseconds(500);
		constexpr auto kFieldWatchInterval = std::chrono::milliseconds(250);
		constexpr auto kTracePublishInterval = std::chrono::milliseconds(250);

		// Temporary root transferred to RuntimeModel after selection succeeds.
		struct ScopedObjectRoot {
			Inspect::ObjectHandle handle{};

			ScopedObjectRoot() = default;
			ScopedObjectRoot(const ScopedObjectRoot&) = delete;
			ScopedObjectRoot& operator=(const ScopedObjectRoot&) = delete;
			~ScopedObjectRoot() { Inspect::FreeObjectHandle(handle); }

			Inspect::ObjectHandle release() {
				const Inspect::ObjectHandle result = handle;
				handle = {};
				return result;
			}
		};

		bool status_is_error(std::string_view message) {
			std::string text(message);
			std::transform(text.begin(), text.end(), text.begin(),
				[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			constexpr std::string_view markers[] = { "failed",    "error",    "blocked",     "unavailable", "no longer",
													"could not", "cannot",   "unsupported", "invalid",     "rejected",
													"released",  "mismatch", "overwrote" };
			return std::any_of(std::begin(markers), std::end(markers),
				[&text](std::string_view marker) { return text.find(marker) != std::string::npos; });
		}

		std::string unity_version_text() {
			constexpr const char* images[] = {"UnityEngine.CoreModule.dll", "UnityEngine.dll", ""};
			for (const char* image : images) {
				const URK::managed::Class* application =
					URK::managed::find_class(image, "UnityEngine", "Application");
				if (!application)
					continue;
				for (const Inspect::PropertyInfo& property : Inspect::properties_from_class(application, true)) {
					if (property.name != "unityVersion" || !property.is_static || !property.can_read)
						continue;
					const Inspect::ValueInfo value = Inspect::ReadProperty({}, property);
					if (value.readable && !value.display.empty())
						return value.display;
				}
			}
			return "unavailable";
		}

		std::string describe_traced_reference(std::uint64_t raw, std::string_view declared_type) {
			const std::uintptr_t address = static_cast<std::uintptr_t>(raw);
			if (!address)
				return "null";
			const std::string fallback = std::string(declared_type.empty() ? "object" : declared_type);
			if (!readable_address(address))
				return fallback + " (reference no longer readable)";

			// Resolve captured register values on the Explorer thread under SEH.
			const Inspect::TypeInfo actual = safe_type_of(Object{reinterpret_cast<void*>(address)});
			if (!actual.handle)
				return fallback + " (runtime type unavailable)";
			const std::string actual_name = actual.full_name.empty() ? fallback : actual.full_name;
			if (!declared_type.empty() && actual_name != declared_type)
				return actual_name + " (declared " + std::string(declared_type) + ")";
			return actual_name;
		}

	} // namespace

	RuntimeModel::~RuntimeModel() = default;
	RuntimeModel& RuntimeModel::instance() {
		static RuntimeModel model;
		return model;
	}

	void RuntimeModel::start() {
		hierarchy_ = std::make_shared<const HierarchyInfo>();
		working_ = {};
		flight_recorder_started_ = Clock::now();
		next_flight_sequence_ = 1;
		working_.hierarchy = hierarchy_;
		working_.status = std::string(URK::compiled_runtime_name) + " runtime ready";
		URK::SceneInfo current_scene{};
		if (URK::current_scene(&current_scene)) {
			active_scene_handle_hint_ = current_scene.handle;
			active_scene_name_hint_ = current_scene.name;
		}
		published_.store(std::make_shared<const Snapshot>(working_));
		next_inspector_refresh_ = Clock::now();
		next_member_value_refresh_ = Clock::now();
		next_field_watch_refresh_ = Clock::now();
		next_trace_publish_ = Clock::now();
		next_class_scan_publish_ = Clock::now();
		request_refresh();
	}

	void RuntimeModel::notify_native_fault(std::uint32_t code, std::uintptr_t address, std::uintptr_t instruction) {
		native_fault_code_.store(code, std::memory_order_release);
		native_fault_address_.store(address, std::memory_order_release);
		native_fault_instruction_.store(instruction, std::memory_order_release);
		native_faulted_.store(true, std::memory_order_release);
	}

	void RuntimeModel::tick() {
		if (native_faulted_.exchange(false, std::memory_order_acq_rel)) {
			// Defer managed handle release after faults; the handle may be invalid.
			discard_managed_state_after_native_fault();
			{
				std::lock_guard<std::mutex> lock(command_mutex_);
				// Preserve commands that re-resolve targets after recovery.
				std::erase_if(commands_, [](const Command& command) {
					return command.kind != CommandKind::Select && command.kind != CommandKind::ClearSelection &&
						command.kind != CommandKind::Refresh && command.kind != CommandKind::SceneHint &&
						command.kind != CommandKind::ObjectDestroyRequested &&
						command.kind != CommandKind::ClearDiagnostics &&
						command.kind != CommandKind::ClearFlightRecorder;
					});
			}
			const std::uint32_t fault_code = native_fault_code_.exchange(0, std::memory_order_acq_rel);
			const std::uintptr_t fault_address = native_fault_address_.exchange(0, std::memory_order_acq_rel);
			const std::uintptr_t fault_instruction = native_fault_instruction_.exchange(0, std::memory_order_acq_rel);
			record_flight("FAULT", std::string("Native ") + URK::compiled_runtime_name + " access",
				"code=" + std::to_string(fault_code));
			set_status(std::string("Explorer isolated a native ") + URK::compiled_runtime_name +
				" access fault; stale managed state was released and a census retry is pending.");
			ModLog::error("Native recovery: code=0x%08X address=%p instruction=%p", fault_code,
				reinterpret_cast<void*>(fault_address), reinterpret_cast<void*>(fault_instruction));
			refresh_requested_.store(false, std::memory_order_release);
			event_refresh_pending_ = true;
			event_refresh_due_ = Clock::now() + std::chrono::milliseconds(750);
			publish_recovery_snapshot();
			return;
		}
		// Apply retained values before processing this frame's commands.
		apply_locked_members();
		process_commands();
		if (native_faulted_.load(std::memory_order_acquire))
			return;
		if ((!ModConfig::enable_mcp.load() || !ModConfig::enable_mcp_tracing.load()) &&
			!mcp_method_trace_ids_.empty()) {
			for (const MethodTracer::TraceId id : mcp_method_trace_ids_)
				MethodTracer::stop(id);
			record_flight("MCP", "Revoke method tracing", "in-game permission disabled");
			mcp_method_trace_ids_.clear();
			set_status("MCP method traces stopped because tracing permission was disabled");
			publish();
		}
		update_pending_scene_load();
		if (class_instance_scan_)
			continue_class_instance_scan();
		if (caller_index_scan_)
			continue_managed_caller_index();

		const Clock::time_point now = Clock::now();
		update_camera_focus();
		if (event_refresh_pending_ && now >= event_refresh_due_) {
			event_refresh_pending_ = false;
			request_refresh();
		}
		const bool refresh_requested = refresh_requested_.exchange(false);
		if (refresh_requested || hierarchy_census_) {
			if (refresh_hierarchy()) {
				refresh_inspector(true);
				update_highlight();
				next_inspector_refresh_ = now + kInspectorInterval;
				next_highlight_refresh_ = now + kHighlightInterval;
				publish();
				return;
			}
		}

		// Paused Live Data preserves the published snapshot.
		if (live_data_ && !event_refresh_pending_ && now >= next_inspector_refresh_ &&
			(working_.selected_instance_id != 0 || working_.object_inspector.valid)) {
			if (working_.selected_instance_id != 0)
				refresh_inspector(false);
			if (now >= next_member_value_refresh_) {
				if (live_data_ && working_.selected_instance_id != 0)
					refresh_live_member_values();
				if (live_data_)
					refresh_object_inspector_values();
				next_member_value_refresh_ = now + kMemberValueInterval;
			}
			if (working_.selected_instance_id != 0)
				update_highlight();
			next_inspector_refresh_ = now + kInspectorInterval;
			next_highlight_refresh_ = now + kHighlightInterval;
			publish();
		}
		// Highlighting refreshes independently of reflective data.
		if (!event_refresh_pending_ && working_.selected_instance_id != 0 &&
			now >= next_highlight_refresh_) {
			update_highlight();
			next_highlight_refresh_ = now + kHighlightInterval;
		}
		// Watches continue sampling while Live Data is paused. Sampling runs
		// every frame so a write that lands and reverts between two publishes
		// still registers as a change; only graph points and the snapshot
		// publish stay on the slower cadence.
		if (has_active_field_watches() && !event_refresh_pending_) {
			const bool due = now >= next_field_watch_refresh_;
			refresh_field_watches(due);
			if (due) {
				next_field_watch_refresh_ = now + kFieldWatchInterval;
				publish();
			}
		}
		if (MethodTracer::any_active() && now >= next_trace_publish_) {
			next_trace_publish_ = now + kTracePublishInterval;
			publish();
		}
	}

	void RuntimeModel::stop() {
		MethodTracer::stop_all();
		mcp_method_trace_ids_.clear();
		hierarchy_census_.reset();
		clear_class_instance_scan();
		clear_reference_graph();
		clear_selection();
		ModUI::Highlight::enqueue_clear();
		hierarchy_instance_ids_.clear();
		clear_component_cache();
		clear_object_inspector();
		managed_references_.clear();
		release_all_field_watches();
		for (auto& [_, handle] : class_browser_handles_)
			Inspect::FreeObjectHandle(handle);
		class_browser_handles_.clear();
		for (auto& [_, handle] : class_browser_static_handles_)
			Inspect::FreeObjectHandle(handle);
		class_browser_static_handles_.clear();
		class_browser_reflection_ = {};
		clear_highlight_renderer_cache();
		clear_highlight_camera_cache();
		highlight_enabled_ = true;
		highlight_max_distance_ = 0.0f;
		camera_focus_.set_settings(CameraFocus::Settings{});
		hierarchy_ = std::make_shared<const HierarchyInfo>();
		working_ = {};
		working_.hierarchy = hierarchy_;
		active_scene_handle_hint_ = 0;
		active_scene_name_hint_.clear();
		logged_hierarchy_signature_.clear();

		publish();
	}

	void RuntimeModel::request_refresh() {
		refresh_requested_.store(true);
	}

	void RuntimeModel::enqueue(Command command) {
		command.sequence = next_command_sequence_.fetch_add(1, std::memory_order_relaxed);
		if (command.scene_generation == 0 && command.kind != CommandKind::SceneHint &&
			command.kind != CommandKind::ObjectDestroyRequested) {
			if (const auto current = published_.load(std::memory_order_acquire); current && current->hierarchy)
				command.scene_generation = current->hierarchy->scene_generation;
		}
		std::lock_guard<std::mutex> lock(command_mutex_);
		if (command.kind == CommandKind::LoadComponentMetadata) {
			const bool already_queued = std::any_of(commands_.begin(), commands_.end(), [&](const Command& pending) {
				return pending.kind == CommandKind::LoadComponentMetadata && pending.instance_id == command.instance_id;
				});
			if (already_queued)
				return;
		}

		if (command.kind == CommandKind::SetHighlightDistance ||
			command.kind == CommandKind::SetHighlightEnabled ||
			command.kind == CommandKind::SetCameraFocusDistance ||
			command.kind == CommandKind::SetCameraFocusTopDown ||
			command.kind == CommandKind::SetCameraFocusTilt) {
			std::erase_if(commands_, [&command](const Command& pending) {
				return pending.kind == command.kind;
			});
		}

		if (command.kind == CommandKind::Select ||
			command.kind == CommandKind::ClearSelection) {

			std::erase_if(
				commands_,
				[](const Command& pending) {
					return pending.kind == CommandKind::Select ||
						pending.kind == CommandKind::ClearSelection;
				});
		}

		if (command.kind == CommandKind::CloseObjectInspectorTab) {
			const bool already_queued =
				std::any_of(
					commands_.begin(),
					commands_.end(),
					[&](const Command& pending) {
						return pending.kind ==
							CommandKind::CloseObjectInspectorTab &&
							pending.object_inspector_token ==
							command.object_inspector_token;
					});

			if (already_queued)
				return;
		}

		commands_.push_back(std::move(command));
	}

	std::shared_ptr<const Snapshot> RuntimeModel::snapshot() const {
		auto current = published_.load();
		return current ? current : std::make_shared<const Snapshot>();
	}
	void RuntimeModel::process_commands() {
		std::vector<Command> pending;
		{
			std::lock_guard<std::mutex> lock(command_mutex_);
			pending.swap(commands_);
		}

		// Coalesce destruction bursts in place. Scene hints remain ordering
		// barriers; moving them to the end allowed pre-transition commands to run
		// against objects from a different scene generation.
		if (pending.size() > 1) {
			const std::size_t original_count = pending.size();
			std::vector<Command> coalesced;
			coalesced.reserve(pending.size());
			std::optional<std::size_t> destroy_index;
			for (Command& command : pending) {
				if (command.kind != CommandKind::ObjectDestroyRequested) {
					coalesced.push_back(std::move(command));
					continue;
				}
				if (!destroy_index) {
					destroy_index = coalesced.size();
					coalesced.push_back(std::move(command));
				}
				else if (command.instance_id == working_.selected_instance_id) {
					// Clearing early is safe and retains the selected object's
					// identity without processing every notification in a burst.
					coalesced[*destroy_index] = std::move(command);
				}
			}
			pending = std::move(coalesced);
			if (pending.size() != original_count)
				ModLog::info("Command queue coalesced destruction burst: before=%zu after=%zu", original_count,
					pending.size());
		}
		for (std::size_t command_index = 0; command_index < pending.size(); ++command_index) {
			const Command& command = pending[command_index];
			record_flight("BEGIN", command_name(command.kind), "seq=" + std::to_string(command.sequence));
#if defined(_WIN32)
			bool native_fault = false;
			__try {
				process_command(command);
			}
			__except (capture_native_fault(_exception_info())) {
				native_fault = true;
			}
			if (native_fault) {
				record_flight("FAULT", command_name(command.kind), "native access violation");
				// Isolate malformed metadata to its component and retain other state.
				if (command.kind == CommandKind::LoadComponentMetadata) {
					const auto component = std::find_if(
						working_.inspector.components.begin(), working_.inspector.components.end(),
						[&command](const ComponentInfo& info) { return info.instance_id == command.instance_id; });
					if (component != working_.inspector.components.end()) {
						component->metadata = std::make_shared<ComponentInfo::Metadata>();
						component->metadata_unavailable = true;
						component->metadata_error = "Native metadata access failed";
						if (!active_metadata_stage_.empty())
							component->metadata_error += " while reading " + active_metadata_stage_;
						component->metadata_error +=
							"; retry after the component finishes initializing or changes state.";
					}
					component_reflection_.erase(command.instance_id);
					detail::clear_metadata_caches();
					clear_error();
					set_status("Component metadata access failed for " +
						(component != working_.inspector.components.end() ? component->type_name
							: std::to_string(command.instance_id)));
					ModLog::error("Component metadata access violation: id=%d type=%s stage=%s code=0x%08X address=%p instruction=%p",
						command.instance_id,
						component != working_.inspector.components.end() ? component->type_name.c_str()
						: "<unknown>",
						active_metadata_stage_.empty() ? "<unknown>" : active_metadata_stage_.c_str(),
						last_native_fault().code, reinterpret_cast<void*>(last_native_fault().address),
						reinterpret_cast<void*>(last_native_fault().instruction));
					active_metadata_stage_.clear();
					publish_recovery_snapshot();
					continue;
				}
				clear_error();
				set_status("Explorer blocked a native access violation while processing a command.");
				ModLog::warn("Explorer command kind=%d sequence=%llu blocked a native access violation: code=0x%08X address=%p instruction=%p",
					static_cast<int>(command.kind), static_cast<unsigned long long>(command.sequence),
					last_native_fault().code, reinterpret_cast<void*>(last_native_fault().address),
					reinterpret_cast<void*>(last_native_fault().instruction));
				std::vector<Command> retry;
				std::size_t dropped = 0;
				for (std::size_t index = command_index + 1; index < pending.size(); ++index) {
					const CommandKind kind = pending[index].kind;
					if (kind == CommandKind::Select || kind == CommandKind::ClearSelection ||
						kind == CommandKind::Refresh || kind == CommandKind::SceneHint ||
						kind == CommandKind::ObjectDestroyRequested || kind == CommandKind::ClearDiagnostics ||
						kind == CommandKind::ClearFlightRecorder)
						retry.push_back(std::move(pending[index]));
					else
						++dropped;
				}
				const std::size_t safe_requeued = retry.size();
				if (!retry.empty()) {
					std::lock_guard<std::mutex> lock(command_mutex_);
					retry.insert(retry.end(), std::make_move_iterator(commands_.begin()),
						std::make_move_iterator(commands_.end()));
					commands_ = std::move(retry);
				}
				ModLog::warn("Command fault disposition: safe_requeued=%zu unsafe_dropped=%zu", safe_requeued, dropped);
				// Defer publication and managed cleanup to the recovery tick.
				notify_native_fault(last_native_fault().code, last_native_fault().address, last_native_fault().instruction);
				return;
			}
#else
			process_command(command);
#endif
			record_flight("DONE", command_name(command.kind));
		}
	}

	void RuntimeModel::process_command(const Command& command) {
		const bool lifecycle_command = command.kind == CommandKind::SceneHint ||
			command.kind == CommandKind::ObjectDestroyRequested;
		if (!lifecycle_command && command.kind != CommandKind::LoadScene &&
			command.scene_generation != 0 && command.scene_generation != scene_generation_) {
			set_status("Ignored a command from an expired scene generation");
			ModLog::warn("Dropped stale command: kind=%d sequence=%llu queued_generation=%llu current_generation=%llu",
				static_cast<int>(command.kind), static_cast<unsigned long long>(command.sequence),
				static_cast<unsigned long long>(command.scene_generation),
				static_cast<unsigned long long>(scene_generation_));
			return;
		}
		if (command.hierarchy_revision != 0 &&
			(!hierarchy_ || command.hierarchy_revision != hierarchy_->revision)) {
			set_status("Hierarchy changed before the command could be applied; please retry");
			request_refresh();
			return;
		}
		const bool component_command =
			command.kind == CommandKind::LoadComponentMetadata || command.kind == CommandKind::LoadComponentClassCatalog ||
			command.kind == CommandKind::LoadClassBrowserCatalog || command.kind == CommandKind::FindClassInstances ||
			command.kind == CommandKind::LoadClassBrowserStaticState ||
			command.kind == CommandKind::LoadClassBrowserMembers ||
			command.kind == CommandKind::SetClassBrowserStaticField || command.kind == CommandKind::DeleteComponent ||
			command.kind == CommandKind::CreateClassInstance ||
			command.kind == CommandKind::SetComponentEnabled || command.kind == CommandKind::SetFieldValue ||
			command.kind == CommandKind::SetPropertyValue || command.kind == CommandKind::SampleMemberValue ||
			command.kind == CommandKind::SetArrayPage || command.kind == CommandKind::RefreshByteArrayInspection ||
			command.kind == CommandKind::InvokeMethod ||
			command.kind == CommandKind::SetMethodTrace || command.kind == CommandKind::ClearMethodTrace ||
			command.kind == CommandKind::CloseMethodTrace ||
			command.kind == CommandKind::CaptureMethodTraceReturns ||
			command.kind == CommandKind::BuildManagedCallerIndex || command.kind == CommandKind::SetFieldWatch ||
			command.kind == CommandKind::ConfigureFieldWatch ||
			command.kind == CommandKind::BuildReferenceGraph || command.kind == CommandKind::ClearReferenceGraph ||
			command.kind == CommandKind::ClearFieldWatch || command.kind == CommandKind::CloseFieldWatch ||
			command.kind == CommandKind::InspectReference || command.kind == CommandKind::InspectRawReference ||
			command.kind == CommandKind::CloseObjectInspectorTab ||
			command.kind == CommandKind::SetLiveData ||
			command.kind == CommandKind::SetHighlightDistance ||
			command.kind == CommandKind::SetHighlightEnabled ||
			command.kind == CommandKind::SetCameraFocusDistance ||
			command.kind == CommandKind::SetCameraFocusTopDown ||
			command.kind == CommandKind::SetCameraFocusTilt ||
			command.kind == CommandKind::SetCameraFocusOffset ||
			command.kind == CommandKind::LoadScene || command.kind == CommandKind::PinManagedReference ||
			command.kind == CommandKind::ReleaseManagedReference || command.kind == CommandKind::ClearManagedReferences;
		// Diagnostic export is native-only and does not require a selected GameObject.
		const bool diagnostic_command = command.kind == CommandKind::ExportDiagnosticBundle;
		const bool event_command = command.kind == CommandKind::ObjectDestroyRequested;
		ScopedObjectRoot command_root;
		GameObject object{};
		const bool needs_object = !component_command && !event_command && !diagnostic_command && command.kind != CommandKind::Refresh &&
			command.kind != CommandKind::ClearSelection && command.kind != CommandKind::SceneHint &&
			command.kind != CommandKind::ClearDiagnostics && command.kind != CommandKind::ClearFlightRecorder &&
			command.kind != CommandKind::RestoreCamera;

		if (needs_object) {
			if (command.instance_id == working_.selected_instance_id)
				object = resolve_selected_object();

			if (!object)
				object = resolve_live_game_object(command.instance_id, command_root.handle);

			if (object && command.expected_object_address != 0 &&
				reinterpret_cast<std::uintptr_t>(object.handle()) != command.expected_object_address) {
				object = {};
				set_status("Object identity changed before the command could be applied; please retry");
				request_refresh();
			}
		}

		if (!object && command.kind != CommandKind::Refresh && command.kind != CommandKind::ClearSelection &&
			command.kind != CommandKind::SceneHint && command.kind != CommandKind::ClearDiagnostics &&
			command.kind != CommandKind::ClearFlightRecorder && command.kind != CommandKind::RestoreCamera && !component_command &&
			!event_command && !diagnostic_command) {
			set_status("Object is no longer available");
			request_refresh();
			return;
		}

		clear_error();
		bool full_inspector_refresh = false;
		switch (command.kind) {
		case CommandKind::Select:
			select_object(object, command_root.release());
			full_inspector_refresh = true;
			break;
		case CommandKind::FocusSelected:
			// Keep selection and camera focus on the same hierarchy target.
			if (object.GetInstanceID() != working_.selected_instance_id) {
				select_object(object, command_root.release());
				refresh_inspector(true);
				update_highlight();
			}
			focus_selected_camera(object);
			publish();
			return;
		case CommandKind::RestoreCamera:
			restore_focused_camera();
			publish();
			return;
		case CommandKind::ClearSelection:
			clear_selection();
			break;
		case CommandKind::ClearDiagnostics:
			working_.diagnostics.clear();
			set_status("Diagnostics cleared");
			publish();
			return;
		case CommandKind::ClearFlightRecorder:
			working_.flight_recorder.clear();
			set_status("Flight recorder cleared");
			publish();
			return;
		case CommandKind::SetMethodTrace:
			set_method_trace(command);
			publish();
			return;
		case CommandKind::ClearMethodTrace:
			clear_method_trace(command.reference_token);
			publish();
			return;
		case CommandKind::CloseMethodTrace:
			close_method_trace(command.reference_token);
			publish();
			return;
		case CommandKind::CaptureMethodTraceReturns:
			capture_method_trace_returns(command.reference_token);
			publish();
			return;
		case CommandKind::BuildManagedCallerIndex:
			build_managed_caller_index();
			publish();
			return;
		case CommandKind::SetFieldWatch:
			set_field_watch(command);
			publish();
			return;
		case CommandKind::ExportDiagnosticBundle: {
			working_.runtime_backend = ModConfig::backend_name;
			working_.runtime_capabilities = URK::runtime_capabilities();
			if (working_.unity_version.empty())
				working_.unity_version = unity_version_text();
			const DiagnosticBundle::Result result = DiagnosticBundle::write(working_);
			if (result.succeeded) {
				working_.diagnostic_bundle_path = result.path;
				set_status("Diagnostic bundle saved: " + result.path);
			}
			else {
				set_status("Diagnostic bundle failed: " + result.error);
			}
			publish();
			return;
		}
		case CommandKind::BuildReferenceGraph:
			build_reference_graph(command);
			publish();
			return;
		case CommandKind::ClearReferenceGraph:
			clear_reference_graph();
			set_status("Reference graph cleared");
			publish();
			return;
		case CommandKind::ConfigureFieldWatch:
			configure_field_watch(command);
			publish();
			return;
		case CommandKind::ClearFieldWatch:
			clear_field_watch(command.reference_token);
			publish();
			return;
		case CommandKind::CloseFieldWatch:
			close_field_watch(command.reference_token);
			publish();
			return;
		case CommandKind::Refresh:
			request_refresh();
			return;
		case CommandKind::LoadScene:
			load_scene(command.int_value, command.text);
			publish();
			return;
		case CommandKind::PinManagedReference:
			pin_managed_reference(command);
			publish();
			return;
		case CommandKind::ReleaseManagedReference:
			if (managed_references_.erase(command.reference_token))
				set_status("Pinned reference removed");
			else
				set_status("Pinned reference was already removed");
			publish();
			return;
		case CommandKind::ClearManagedReferences:
			managed_references_.clear();
			set_status("Pinned reference shelf cleared");
			publish();
			return;
		case CommandKind::DeleteObject:
			if (working_.selected_instance_id == command.instance_id)
				clear_selection();
			Object::Destroy(object);
			capture_last_error("Delete");
			request_refresh();
			event_refresh_pending_ = true;
			event_refresh_due_ = Clock::now() + kEventRefreshDebounce;
			return;
		case CommandKind::DuplicateObject: {
			const Transform parent = object.transform().parent();
			// Instantiate overloads are declared on UnityEngine.Object.
			const Object original{ object.handle() };
			Object cloned =
				parent ? Object::Instantiate<Object>(original, parent) : Object::Instantiate<Object>(original);
			// Some stripped players expose only Instantiate(Object).
			if (!cloned && parent) {
				clear_error();
				cloned = Object::Instantiate<Object>(original);
			}
			const GameObject clone{ cloned.handle() };
			if (clone)
				set_status("Duplicated " + object.name());
			else if (const char* error = last_error(); error && error[0])
				set_status(std::string("Duplicate failed: ") + error);
			else
				set_status("Duplicate failed: Unity returned a null object");
			request_refresh();
			event_refresh_pending_ = true;
			event_refresh_due_ = Clock::now() + kEventRefreshDebounce;
			return;
		}
		case CommandKind::Rename:
			object.SetProperty("name", command.text);
			capture_last_error("Rename");
			request_refresh();
			return;
		case CommandKind::SetTag:
			object.SetProperty("tag", command.text);
			capture_last_error("Set tag");
			full_inspector_refresh = true;
			break;
		case CommandKind::SetLayer:
			object.SetProperty("layer", std::clamp(command.int_value, 0, 31));
			capture_last_error("Set layer");
			full_inspector_refresh = true;
			break;
		case CommandKind::SetStatic:
			object.SetProperty("isStatic", command.bool_value);
			capture_last_error("Set static");
			full_inspector_refresh = true;
			break;
		case CommandKind::SetActive:
			object.SetActive(command.bool_value);
			capture_last_error("Set active");
			// Confirm the state change so a protected object is not mistaken for a missed click.
			if (!(last_error() && last_error()[0]) && object.activeSelf() != command.bool_value)
				set_status("Set active failed: Unity did not apply the requested state");
			request_refresh();
			return;
		case CommandKind::SetLocalPosition:
			object.transform().set_localPosition(command.vector_value);
			capture_last_error("Set position");
			break;
		case CommandKind::SetLocalRotation:
			object.transform().SetProperty("localEulerAngles", command.vector_value);
			capture_last_error("Set rotation");
			break;
		case CommandKind::SetLocalScale:
			object.transform().set_localScale(command.vector_value);
			capture_last_error("Set scale");
			break;
		case CommandKind::PasteLocalTransform: {
			const Transform transform = object.transform();
			transform.set_localPosition(command.vector_value);
			if (const char* error = last_error(); error && error[0]) {
				capture_last_error("Paste transform position");
				break;
			}
			transform.SetProperty("localEulerAngles", command.vector_value_secondary);
			if (const char* error = last_error(); error && error[0]) {
				capture_last_error("Paste transform rotation");
				break;
			}
			transform.set_localScale(command.vector_value_tertiary);
			capture_last_error("Paste transform scale");
			if (!(last_error() && last_error()[0]))
				set_status("Pasted local transform onto " + object.name());
			full_inspector_refresh = true;
			break;
		}
		case CommandKind::CopyLocalTransform: {
			const Transform transform = object.transform();
			working_.transform_clipboard.valid = false;
			working_.transform_clipboard.source_instance_id = object.GetInstanceID();
			working_.transform_clipboard.source_name = object.name();
			working_.transform_clipboard.local_position = transform.localPosition();
			if (const char* error = last_error(); error && error[0]) {
				capture_last_error("Copy transform position");
				break;
			}
			working_.transform_clipboard.local_rotation = transform.GetProperty<Vector3>("localEulerAngles");
			if (const char* error = last_error(); error && error[0]) {
				capture_last_error("Copy transform rotation");
				break;
			}
			working_.transform_clipboard.local_scale = transform.localScale();
			if (const char* error = last_error(); error && error[0]) {
				capture_last_error("Copy transform scale");
				break;
			}
			working_.transform_clipboard.valid = true;
			set_status("Copied local transform: " + working_.transform_clipboard.source_name);
			break;
		}
		case CommandKind::AddComponent: {
			const Object component = object.AddComponent(command.image, command.namespc, command.class_name);
			if (component)
				set_status("Added " + command.class_name);
			else
				capture_last_error("Add component");
			full_inspector_refresh = true;
			break;
		}
		case CommandKind::LoadComponentMetadata:
			load_component_metadata(command.instance_id);
			publish();
			return;
		case CommandKind::LoadComponentClassCatalog:
			load_component_class_catalog();
			publish();
			return;
		case CommandKind::LoadClassBrowserCatalog:
			load_class_browser_catalog();
			publish();
			return;
		case CommandKind::FindClassInstances:
			find_class_instances(command);
			publish();
			return;
		case CommandKind::LoadClassBrowserStaticState:
			load_class_browser_static_state(command);
			publish();
			return;
		case CommandKind::LoadClassBrowserMembers:
			load_class_browser_members(command);
			publish();
			return;
		case CommandKind::SetClassBrowserStaticField:
			set_class_browser_static_field(command);
			publish();
			return;
		case CommandKind::CreateClassInstance:
			create_class_instance(command);
			publish();
			return;
		case CommandKind::DeleteComponent:
			delete_component(command.instance_id);
			return;
		case CommandKind::SetComponentEnabled: {
			const Object component = resolve_component(command.instance_id);
			if (!safe_object_alive(component)) {
				set_status("Component is no longer available");
				return;
			}
#if defined(_WIN32)
			__try {
				component.SetProperty("enabled", command.bool_value);
			}
			__except (capture_native_fault(_exception_info())) {
				clear_error();
				set_status("Set component enabled blocked an invalid native access");
				return;
			}
#else
			component.SetProperty("enabled", command.bool_value);
#endif
			capture_last_error("Set component enabled");
			for (ComponentInfo& info : working_.inspector.components) {
				if (info.instance_id == command.instance_id) {
					info.enabled = command.bool_value;
					break;
				}
			}
			publish();
			return;
		}
		case CommandKind::SetLiveData:
			live_data_ = command.bool_value;
			if (live_data_) {
				refresh_live_member_values();
				refresh_object_inspector_values();
			}
			set_status(live_data_ ? "Live Data enabled" : "Live Data paused");
			publish();
			return;
		case CommandKind::SetHighlightDistance:
			highlight_max_distance_ = std::clamp(command.float_value, 0.0f, 100000.0f);
			next_highlight_refresh_ = Clock::now();
			if (selected_)
				update_highlight();
			set_status(highlight_max_distance_ > 0.0f
				? "Highlight max distance set to " + std::to_string(static_cast<int>(highlight_max_distance_))
				: "Highlight max distance disabled");
			publish();
			return;
		case CommandKind::SetHighlightEnabled:
			highlight_enabled_ = command.bool_value;
			next_highlight_refresh_ = Clock::now();
			if (!highlight_enabled_) {
				if (highlight_id_ != 0) {
					ModUI::Highlight::enqueue_remove(highlight_id_);
					highlight_id_ = 0;
				}
				if (highlight_locator_id_ != 0) {
					ModUI::Highlight::enqueue_remove(highlight_locator_id_);
					highlight_locator_id_ = 0;
				}
			}
			else if (selected_) {
				update_highlight();
			}
			set_status(highlight_enabled_ ? "Selection highlight enabled" : "Selection highlight disabled");
			publish();
			return;
		case CommandKind::SetCameraFocusDistance: {
			CameraFocus::Settings settings = camera_focus_.settings();
			settings.distance = std::clamp(command.float_value, 1.0f, 100.0f);
			camera_focus_.set_settings(settings);
			update_camera_focus();
			set_status(std::string(settings.top_down ? "Camera focus height set to " : "Camera focus distance set to ") +
				std::to_string(settings.distance) + " units");
			publish();
			return;
		}

		case CommandKind::SetCameraFocusTopDown: {
			CameraFocus::Settings settings = camera_focus_.settings();
			settings.top_down = command.bool_value;
			camera_focus_.set_settings(settings);
			update_camera_focus();
			set_status(settings.top_down ? "Camera focus changed to top-down" : "Camera focus now preserves the game view angle");
			publish();
			return;
		}
		case CommandKind::SetCameraFocusTilt: {
			CameraFocus::Settings settings = camera_focus_.settings();
			settings.top_down_tilt = std::clamp(command.float_value, 0.0f, 100.0f);
			camera_focus_.set_settings(settings);
			update_camera_focus();
			set_status("Camera top-down tilt set to " + std::to_string(settings.top_down_tilt) + " units");
			publish();
			return;
		}
		case CommandKind::SetCameraFocusOffset: {
			CameraFocus::Settings settings = camera_focus_.settings();
			settings.offset_x = std::clamp(command.vector_value.x, -10000.0f, 10000.0f);
			settings.offset_y = std::clamp(command.vector_value.y, -10000.0f, 10000.0f);
			settings.offset_z = std::clamp(command.vector_value.z, -10000.0f, 10000.0f);
			camera_focus_.set_settings(settings);
			update_camera_focus();
			set_status("Camera focus offset updated");
			publish();
			return;
		}
		case CommandKind::SetFieldValue:
			set_member_value(command, false);
			publish();
			return;
		case CommandKind::SetPropertyValue:
			set_member_value(command, true);
			publish();
			return;
		case CommandKind::SampleMemberValue:
			sample_member_value(command);
			publish();
			return;
		case CommandKind::SetArrayPage:
			if (!working_.object_inspector.valid || !working_.object_inspector.is_array ||
				command.object_inspector_token == 0 ||
				command.object_inspector_token != working_.object_inspector.token) {
				set_status("Array page target is no longer available");
				return;
			}
			working_.object_inspector.array_offset =
				command.int_value < 0 ? 0u : static_cast<std::size_t>(command.int_value);
			refresh_object_inspector_values();
			set_status("Array page loaded");
			publish();
			return;
		case CommandKind::RefreshByteArrayInspection:
			if (!working_.object_inspector.valid || !working_.object_inspector.is_array ||
				command.object_inspector_token == 0 ||
				command.object_inspector_token != working_.object_inspector.token) {
				set_status("Byte array target is no longer available");
				return;
			}
			refresh_object_inspector_values(true);
			set_status("Byte array decoded from a fresh snapshot");
			publish();
			return;
		case CommandKind::InvokeMethod:
			invoke_method(command);
			if (live_data_) {
				if (command.object_inspector_target)
					refresh_object_inspector_values();
				else
					refresh_live_member_values();
			}
			publish();
			return;
		case CommandKind::InspectReference:
			inspect_reference(command.reference_token);
			publish();
			return;
		case CommandKind::InspectRawReference:
			inspect_raw_reference(command.reference_token);
			publish();
			return;
		case CommandKind::CloseObjectInspectorTab:
			close_object_inspector_tab(command.object_inspector_token);
			publish();
			return;
		case CommandKind::SceneHint:
			++scene_generation_;
			hierarchy_census_.reset();
			clear_class_instance_scan();
			clear_reference_graph();
			ModLog::info("Scene generation advanced: generation=%llu handle=%d name=%s",
				static_cast<unsigned long long>(scene_generation_), command.int_value,
				command.text.empty() ? "<unknown>" : command.text.c_str());
			active_scene_handle_hint_ = command.int_value;
			active_scene_name_hint_ = command.text;
			// Treat the runtime callback as authoritative for scene activation.
			if (pending_scene_load_.active) {
				const bool build_index_matches = pending_scene_load_.build_index >= 0 &&
					command.member_index == pending_scene_load_.build_index;
				const bool name_matches = !pending_scene_load_.key.empty() &&
					(command.text == pending_scene_load_.key ||
					 command.text == scene_display_name(pending_scene_load_.key, -1));
				if (build_index_matches || name_matches) {
					const bool unchanged = command.member_index == pending_scene_load_.previous_build_index &&
						command.text == pending_scene_load_.previous_name;
					set_status(unchanged
						? "Scene load request targeted the already active scene: " + command.text
						: "Scene activated: " + command.text + " (build index " +
							std::to_string(command.member_index) + ")");
					pending_scene_load_ = {};
				}
			}
			// Release strong explorer roots before scene teardown.
			clear_selection();
			clear_object_inspector();
			// Wait for destruction events before rebuilding the hierarchy.
			event_refresh_pending_ = true;
			event_refresh_due_ = Clock::now() + kEventRefreshDebounce;
			return;
		case CommandKind::ObjectDestroyRequested:
			if (command.instance_id == working_.selected_instance_id) {
				clear_selection();
				clear_object_inspector();
			}
			// Non-selected destruction also invalidates hierarchy membership.
			// Debounce the census rather than silently retaining a stale tree.
			event_refresh_pending_ = true;
			event_refresh_due_ = Clock::now() + kEventRefreshDebounce;
			return;
		}

		refresh_inspector(full_inspector_refresh);
		// Defer the global camera/renderer census beyond the selection path.
		if (command.kind != CommandKind::Select)
			update_highlight();
		publish();
	}

	void RuntimeModel::discard_managed_state_after_native_fault() {
		const std::uint64_t quarantined_before = Inspect::QuarantinedObjectHandleCount();
		hierarchy_census_.reset();
		clear_class_instance_scan();
		clear_reference_graph();
		if (highlight_id_ != 0)
			ModUI::Highlight::enqueue_remove(highlight_id_);
		if (highlight_locator_id_ != 0)
			ModUI::Highlight::enqueue_remove(highlight_locator_id_);
		highlight_id_ = 0;
		highlight_locator_id_ = 0;
		camera_focus_.abandon_after_native_fault();
		managed_references_.abandon_after_native_fault();
		traced_return_references_.clear();

		selected_ = {};
		Inspect::FreeObjectHandle(selected_handle_);
		hierarchy_instance_ids_.clear();
		clear_locked_members();
		clear_component_cache();
		clear_object_inspector();
		release_all_field_watches();
		for (auto& [_, handle] : class_browser_handles_)
			Inspect::FreeObjectHandle(handle);
		class_browser_handles_.clear();
		for (auto& [_, handle] : class_browser_static_handles_)
			Inspect::FreeObjectHandle(handle);
		class_browser_static_handles_.clear();
		component_reflection_.clear();
		class_browser_reflection_ = {};
		active_metadata_stage_.clear();
		detail::clear_metadata_caches();
		object_inspector_reflection_ = {};
		sampled_component_members_.clear();
		sampled_object_fields_.clear();
		sampled_object_properties_.clear();
		clear_highlight_renderer_cache();
		clear_highlight_camera_cache();
		working_.selected_instance_id = 0;
		working_.inspector = {};
		working_.camera_focus_active = false;
		working_.object_inspector = {};
		working_.method_results.clear();
		working_.member_write_results.clear();
		working_.field_watches.clear();
		working_.locked_member_keys.clear();
		working_.class_browser_instances.clear();
		working_.class_browser_static_fields.clear();
		working_.class_browser_members = {};
		working_.managed_references.clear();

		// Retain native hierarchy data while rebuilding the object index.
		working_.hierarchy = hierarchy_;
		const std::uint64_t quarantined_after = Inspect::QuarantinedObjectHandleCount();
		if (quarantined_after != quarantined_before)
			ModLog::error("GC handle recovery quarantined %llu handle(s); total=%llu",
				static_cast<unsigned long long>(quarantined_after - quarantined_before),
				static_cast<unsigned long long>(quarantined_after));
	}

	void RuntimeModel::set_status(std::string message) {
		const bool error = status_is_error(message);
		working_.status = std::move(message);
		if (!error)
			return;
		if (working_.diagnostics.empty() || working_.diagnostics.back() != working_.status) {
			constexpr std::size_t kMaxDiagnostics = 32;
			if (working_.diagnostics.size() == kMaxDiagnostics)
				working_.diagnostics.erase(working_.diagnostics.begin());
			working_.diagnostics.push_back(working_.status);
		}
		ModLog::error("%s", working_.status.c_str());
	}

	const char* RuntimeModel::command_name(CommandKind kind) {
		switch (kind) {
		case CommandKind::Select: return "Select object";
		case CommandKind::Refresh: return "Refresh hierarchy";
		case CommandKind::LoadComponentMetadata: return "Load component metadata";
		case CommandKind::SetFieldValue: return "Write field";
		case CommandKind::SetPropertyValue: return "Write property";
		case CommandKind::SampleMemberValue: return "Read member";
		case CommandKind::InvokeMethod: return "Execute method";
		case CommandKind::SetMethodTrace: return "Configure method trace";
		case CommandKind::CaptureMethodTraceReturns: return "Record method trace returns";
		case CommandKind::BuildManagedCallerIndex: return "Index managed caller names";
		case CommandKind::SetFieldWatch: return "Configure value watch";
		case CommandKind::ConfigureFieldWatch: return "Configure watch alarm";
		case CommandKind::ExportDiagnosticBundle: return "Export diagnostic bundle";
		case CommandKind::BuildReferenceGraph: return "Build reference graph";
		case CommandKind::ClearReferenceGraph: return "Clear reference graph";
		case CommandKind::InspectReference: return "Inspect reference";
		case CommandKind::SetActive: return "Set GameObject active";
		case CommandKind::DeleteObject: return "Delete GameObject";
		case CommandKind::DeleteComponent: return "Delete component";
		case CommandKind::FocusSelected: return "Focus camera";
		case CommandKind::RestoreCamera: return "Restore camera";
		case CommandKind::SetCameraFocusDistance: return "Set camera focus distance";
		case CommandKind::SetCameraFocusTopDown: return "Set camera focus orientation";
		case CommandKind::SetCameraFocusTilt: return "Set camera focus tilt";
		case CommandKind::SetCameraFocusOffset: return "Set camera focus offset";
		case CommandKind::SetClassBrowserStaticField: return "Set Class Browser static member";
		case CommandKind::LoadScene: return "Load build scene";
		case CommandKind::PinManagedReference: return "Pin managed reference";
		case CommandKind::ReleaseManagedReference: return "Release managed reference";
		case CommandKind::ClearManagedReferences: return "Clear managed references";
		case CommandKind::CreateClassInstance: return "Create class instance";
		case CommandKind::RefreshByteArrayInspection: return "Refresh byte array inspection";
		case CommandKind::PasteLocalTransform: return "Paste local transform";
		case CommandKind::CopyLocalTransform: return "Copy local transform";
		case CommandKind::SceneHint: return "Scene transition";
		case CommandKind::ClearFlightRecorder: return "Clear flight recorder";
		default: return "Explorer command";
		}
	}

	void RuntimeModel::record_flight(std::string stage, std::string operation, std::string detail) {
		constexpr std::size_t kMaxFlightEvents = 50;
		Snapshot::FlightEvent event{};
		event.sequence = next_flight_sequence_++;
		event.seconds_since_start = std::chrono::duration<double>(Clock::now() - flight_recorder_started_).count();
		event.stage = std::move(stage);
		event.operation = std::move(operation);
		event.detail = std::move(detail);
		if (working_.flight_recorder.size() == kMaxFlightEvents)
			working_.flight_recorder.erase(working_.flight_recorder.begin());
		working_.flight_recorder.push_back(std::move(event));
	}

	void RuntimeModel::record_value_error(std::string context, const Inspect::ValueInfo& value) {
		if (value.readable || value.display.empty() || value.display == "Not sampled")
			return;
		if (is_expected_empty_container_error(value.display))
			return;
		// Show sampled getter failures once without flooding diagnostics.
		std::string message = "Read " + std::move(context) + " failed: " + value.display;
		if (logged_value_errors_.size() >= 256)
			logged_value_errors_.clear();
		if (logged_value_errors_.insert(message).second)
			set_status(std::move(message));
	}

	void RuntimeModel::capture_last_error(std::string_view action) {
		const char* error = last_error();
		if (error && error[0]) {
			set_status(std::string(action) + " failed: " + error);
		}
		else {
			set_status(std::string(action) + " completed");
		}
	}

	void RuntimeModel::publish() {
		working_.runtime_backend = ModConfig::backend_name;
#if defined(URK_BACKEND_MONO)
		// Mono resolves a method's native code by JIT-compiling it, and compiling
		// arbitrary metadata methods can fault, so the address index is IL2CPP only.
		working_.caller_index_supported = false;
#endif
		working_.runtime_capabilities = URK::runtime_capabilities();
		if (working_.unity_version.empty())
			working_.unity_version = unity_version_text();
		working_.hierarchy = hierarchy_;
		working_.component_class_catalog = component_class_catalog_;
		working_.class_browser_catalog = class_browser_catalog_;
		working_.live_data = live_data_;
		working_.highlight_enabled = highlight_enabled_;
		const CameraFocus::Settings& camera_settings = camera_focus_.settings();
		working_.camera_focus_distance = camera_settings.distance;
		working_.camera_focus_tilt = camera_settings.top_down_tilt;
		working_.camera_focus_offset =
			Vector3{camera_settings.offset_x, camera_settings.offset_y, camera_settings.offset_z};
		working_.camera_focus_top_down = camera_settings.top_down;
		working_.camera_focus_active = camera_focus_.active();
		working_.method_traces = MethodTracer::snapshots();
		working_.managed_references = managed_references_.snapshot();
		working_.field_watches.clear();
		working_.field_watches.reserve(field_watches_.size());
		for (const auto& [_, state] : field_watches_)
			working_.field_watches.push_back(state.snapshot);
		std::sort(working_.field_watches.begin(), working_.field_watches.end(), [](const Snapshot::FieldWatch& left,
			const Snapshot::FieldWatch& right) {
				return left.id < right.id;
			});
        for (MethodTracer::Snapshot& trace : working_.method_traces) {
            // Decode ABI captures on the Explorer thread, outside the detour.
            MethodTraceValueDecoder::resolve_displays(trace);
            for (MethodTracer::Record& record : trace.records)
                record.caller_display = managed_caller_location(record.caller_address);
            MethodTraceFormat::collapse_repeated_records(trace);
        }
		std::unordered_set<TraceReturnKey, TraceReturnKeyHash> live_trace_returns;
		for (MethodTracer::Snapshot& trace : working_.method_traces) {
			// Root only recent object results; older trace rows remain raw diagnostics.
			constexpr std::size_t kMaxRootedTraceReturns = 128;
			const std::size_t first_rooted_reference = trace.records.size() > kMaxRootedTraceReturns
				? trace.records.size() - kMaxRootedTraceReturns : 0;
			for (std::size_t record_index = 0; record_index < trace.records.size(); ++record_index) {
				MethodTracer::Record& record = trace.records[record_index];
				const bool reference_return = trace.return_is_reference && record.return_captured &&
					record.return_rax != 0;
				const bool boxed_value_return = trace.return_value_class && trace.return_value_size != 0 &&
					record.return_captured && record.return_value_bytes.size() == trace.return_value_size;
				if (!reference_return && !boxed_value_return)
					continue;
				if (reference_return && record_index < first_rooted_reference)
					continue;
				const TraceReturnKey key{trace.id, record.sequence};
				live_trace_returns.insert(key);
				const auto existing = traced_return_references_.find(key);
				if (existing != traced_return_references_.end()) {
					record.return_reference_token = existing->second;
					const auto handle = reference_handles_.find(existing->second);
					if (handle != reference_handles_.end()) {
						const Object value = Inspect::ResolveObjectHandle(handle->second);
						if (!value) {
							record.return_display = "retained trace result was released";
						} else if (trace.return_type != "System.String" && trace.return_type != "String" &&
							trace.return_type != "string") {
							record.return_display = describe_traced_reference(
								static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(value.handle())), trace.return_type);
							record.return_readable = true;
						}
					}
					continue;
				}
				if (reference_return) {
					const Object candidate{reinterpret_cast<void*>(static_cast<std::uintptr_t>(record.return_rax))};
					const Inspect::TypeInfo type = safe_type_of(candidate);
					if (!type.handle) {
						record.return_display = "returned reference could not be identified";
						continue;
					}
					Inspect::ObjectHandle root = Inspect::PinObject(candidate);
					const Object value = Inspect::ResolveObjectHandle(root);
					if (!root.handle || !value) {
						Inspect::FreeObjectHandle(root);
						record.return_display = "returned reference could not be retained";
						continue;
					}
					std::uint64_t token = 0;
					do {
						token = 0xc000000000000000ull | (next_reference_token_++ & 0x0fffffffffffffffull);
					} while (reference_handles_.contains(token));
					reference_handles_[token] = root;
					traced_return_references_.emplace(key, token);
					record.return_reference_token = token;
					if (trace.return_type != "System.String" && trace.return_type != "String" &&
						trace.return_type != "string") {
						record.return_display = describe_traced_reference(
							static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(value.handle())), trace.return_type);
						record.return_readable = true;
					}
					continue;
				}
				void* boxed = nullptr;
#if defined(_WIN32)
				__try {
#endif
					boxed = URK::managed::value_box(static_cast<const URK::managed::Class*>(trace.return_value_class),
						record.return_value_bytes.data());
#if defined(_WIN32)
				}
				__except (capture_native_fault(_exception_info())) {
					record.return_display = std::string(URK::compiled_runtime_name) +
						" value_box raised a native access fault";
					continue;
				}
#endif
				if (!boxed) {
					record.return_display = std::string(URK::compiled_runtime_name) +
						" value_box failed for returned " + trace.return_type;
					continue;
				}
				Inspect::ObjectHandle root = Inspect::PinObject(Object{boxed});
				const Object value = Inspect::ResolveObjectHandle(root);
				if (!root.handle || !value) {
					Inspect::FreeObjectHandle(root);
					record.return_display = "Could not root boxed trace result";
					continue;
				}
				std::uint64_t token = 0;
				do {
					token = 0xc000000000000000ull | (next_reference_token_++ & 0x0fffffffffffffffull);
				} while (reference_handles_.contains(token));
				reference_handles_[token] = root;
				traced_return_references_.emplace(key, token);
				record.return_reference_token = token;
				record.return_display = describe_traced_reference(
					static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(value.handle())), trace.return_type);
			}
		}
		for (auto it = traced_return_references_.begin(); it != traced_return_references_.end();) {
			if (live_trace_returns.contains(it->first)) {
				++it;
				continue;
			}
			release_reference_handle(it->second);
			it = traced_return_references_.erase(it);
		}
		working_.strong_handle_count = component_handles_.size();
		working_.weak_handle_count = 0;
		if (selected_handle_.handle)
			++(selected_handle_.weak ? working_.weak_handle_count : working_.strong_handle_count);
		for (const auto& [_, handle] : reference_handles_)
			++(handle.weak ? working_.weak_handle_count : working_.strong_handle_count);
		working_.strong_handle_count += managed_references_.size();
		for (const auto& [_, handle] : object_inspector_history_)
			++(handle.weak ? working_.weak_handle_count : working_.strong_handle_count);
		for (const auto& [_, handle] : class_browser_handles_)
			++(handle.weak ? working_.weak_handle_count : working_.strong_handle_count);
		for (const auto& [_, handle] : class_browser_static_handles_)
			++(handle.weak ? working_.weak_handle_count : working_.strong_handle_count);
		for (const auto& [_, locked] : locked_members_)
			if (locked.value_root.handle)
				++(locked.value_root.weak ? working_.weak_handle_count : working_.strong_handle_count);
		for (const Inspect::ObjectHandle& handle : highlight_renderers_)
			if (handle.handle)
				++(handle.weak ? working_.weak_handle_count : working_.strong_handle_count);
		for (const Inspect::ObjectHandle& handle : highlight_cameras_)
			if (handle.handle)
				++(handle.weak ? working_.weak_handle_count : working_.strong_handle_count);
		if (object_inspector_handle_.handle)
			++(object_inspector_handle_.weak ? working_.weak_handle_count : working_.strong_handle_count);
		if (hierarchy_census_ && hierarchy_census_->candidates)
			++working_.strong_handle_count;
		working_.quarantined_handle_count = Inspect::QuarantinedObjectHandleCount();
		working_.hierarchy_census_active = hierarchy_census_ != nullptr;
		working_.hierarchy_census_processed = hierarchy_census_ ? hierarchy_census_->candidate_index : 0;
		working_.hierarchy_census_candidates = hierarchy_census_ ? hierarchy_census_->candidate_count : 0;
		working_.managed_used_bytes = URK::managed::gc_get_used_size();
		working_.managed_heap_bytes = URK::managed::gc_get_heap_size();
		++working_.revision;
		published_.store(std::make_shared<const Snapshot>(working_));
	}

	void RuntimeModel::publish_recovery_snapshot() {
		// Keep this strictly native-only.  In particular, gc_get_* and trace
		// display formatting call the managed runtime and are not safe immediately after SEH.
		working_.hierarchy = hierarchy_;
		working_.strong_handle_count = component_handles_.size();
		working_.weak_handle_count = 0;
		const auto count_handle = [&](const Inspect::ObjectHandle& handle) {
			if (handle.handle)
				++(handle.weak ? working_.weak_handle_count : working_.strong_handle_count);
			};
		count_handle(selected_handle_);
		for (const auto& [_, handle] : reference_handles_)
			count_handle(handle);
		working_.managed_references.clear();
		for (const auto& [_, handle] : object_inspector_history_)
			count_handle(handle);
		for (const auto& [_, handle] : class_browser_handles_)
			count_handle(handle);
		for (const auto& [_, handle] : class_browser_static_handles_)
			count_handle(handle);
		for (const auto& [_, locked] : locked_members_)
			count_handle(locked.value_root);
		for (const Inspect::ObjectHandle& handle : highlight_renderers_)
			count_handle(handle);
		for (const Inspect::ObjectHandle& handle : highlight_cameras_)
			count_handle(handle);
		count_handle(object_inspector_handle_);
		working_.quarantined_handle_count = Inspect::QuarantinedObjectHandleCount();
		working_.hierarchy_census_active = hierarchy_census_ != nullptr;
		working_.hierarchy_census_processed = hierarchy_census_ ? hierarchy_census_->candidate_index : 0;
		working_.hierarchy_census_candidates = hierarchy_census_ ? hierarchy_census_->candidate_count : 0;
		++working_.revision;
		published_.store(std::make_shared<const Snapshot>(working_));
	}


} // namespace Explorer

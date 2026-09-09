// Copyright (c) 2026 Jadis0x. All rights reserved.
#include "explorer_model.h"

#include "model_shared.h"
#include "support/mod_log.h"

#include <algorithm>
#include <string>

using namespace URK::Unity;

namespace Explorer {

	void RuntimeModel::set_method_trace(const Command& command) {
		if (!command.bool_value && command.reference_token != 0) {
			if (MethodTracer::stop(command.reference_token))
				set_status("Method tracing stopped");
			else
				set_status("Method trace is no longer available");
			return;
		}
		const bool nested = command.object_inspector_target;
		const bool browser = command.class_browser_target;
		if (nested && (!working_.object_inspector.valid || command.object_inspector_token == 0 ||
			command.object_inspector_token != working_.object_inspector.token)) {
			set_status("Object Inspector tab changed before tracing could start");
			return;
		}
		if (browser &&
			(working_.class_browser_members_query.image != command.image ||
			 working_.class_browser_members_query.namespc != command.namespc ||
			 working_.class_browser_members_query.class_name != command.class_name)) {
			set_status("Class Browser selection changed before tracing could start");
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
			set_status("Method is no longer available for tracing");
			return;
		}
		const Inspect::MethodInfo& method = reflection->methods[command.member_index];
		if (!command.bool_value) {
			if (MethodTracer::stop(static_cast<const URK::managed::Method*>(method.handle)))
				set_status("Method tracing stopped");
			else
				set_status("Method trace is no longer active");
			return;
		}
		std::string error;
		remember_managed_method(method);
		if (!MethodTracer::start(method, command.capture_return, nullptr, true, error)) {
			set_status("Method tracing failed: " + error);
			return;
		}
		set_status("Tracing " + method.declaring_type.full_name + "." + method.name +
			(command.capture_return ? " (all calls, with return values)" : " (all calls)"));
	}

	void RuntimeModel::clear_method_trace(MethodTracer::TraceId id) {
		if (MethodTracer::clear(id))
			set_status("Method trace history cleared");
		else
			set_status("Method trace is no longer available");
	}

	void RuntimeModel::build_managed_caller_index() {
		if (caller_index_scan_) {
			set_status("Caller name index is already being built");
			return;
		}
		if (!working_.caller_index_supported) {
			set_status("Caller names cannot be indexed on this runtime backend");
			return;
		}
		caller_index_scan_ = std::make_unique<CallerIndexScan>();
		caller_index_scan_->started = Clock::now();
		ModLog::info("caller index: walking every loaded assembly for managed method addresses");
		working_.caller_index_active = true;
		working_.caller_index_built = false;
		working_.caller_index_methods = 0;
		working_.caller_index_classes = 0;
		set_status("Indexing managed method addresses for caller names...");
	}

	void RuntimeModel::continue_managed_caller_index() {
		if (!caller_index_scan_)
			return;
		CallerIndexScan& scan = *caller_index_scan_;
		// Same 3 ms slice the instance scan uses: enough to make progress within
		// a frame, small enough that the game keeps its frame rate.
		const Clock::time_point deadline = Clock::now() + std::chrono::milliseconds(3);
		const std::size_t assembly_count = std::min<std::size_t>(URK::managed::domain_get_assembly_count(), 4096);
		while (scan.assembly_index < assembly_count && Clock::now() < deadline) {
			const URK::managed::Assembly* assembly = URK::managed::domain_get_assembly(scan.assembly_index);
			const URK::managed::Image* image = assembly ? URK::managed::assembly_get_image(assembly) : nullptr;
			const std::size_t class_count =
				image ? std::min<std::size_t>(URK::managed::image_get_class_count(image), 1000000) : 0;
			if (!image || scan.class_index >= class_count) {
				++scan.assembly_index;
				scan.class_index = 0;
				continue;
			}
			const URK::managed::Class* klass = URK::managed::image_get_class(image, scan.class_index++);
			if (!klass)
				continue;
			++scan.scanned_classes;
			// Declared methods only: an inherited method is indexed once, by the
			// type that actually owns its code.
			for (const Inspect::MethodInfo& method : Inspect::methods_from_class(klass, false)) {
				remember_managed_method(method);
				++scan.indexed_methods;
			}
		}
		working_.caller_index_methods = scan.indexed_methods;
		working_.caller_index_classes = scan.scanned_classes;
		const bool complete = scan.assembly_index >= assembly_count;
		if (complete) {
			const std::size_t methods = scan.indexed_methods;
			const std::size_t classes = scan.scanned_classes;
			caller_index_scan_.reset();
			working_.caller_index_active = false;
			working_.caller_index_built = true;
			mark_caller_index_complete();
			ModLog::info("caller index: %zu method(s) across %zu class(es)", methods, classes);
			if (methods == 0)
				set_status("Caller name index finished with no entries; this runtime does not expose method addresses");
			else
				set_status("Caller name index built: " + std::to_string(methods) + " method(s) across " +
					std::to_string(classes) + " class(es)");
			publish();
			return;
		}
		const Clock::time_point now = Clock::now();
		if (now >= next_caller_index_publish_) {
			next_caller_index_publish_ = now + std::chrono::milliseconds(200);
			publish();
		}
	}

	void RuntimeModel::capture_method_trace_returns(MethodTracer::TraceId id) {
		std::string error;
		if (MethodTracer::capture_returns(id, error))
			set_status("Now recording return values; earlier calls were cleared");
		else
			set_status("Could not record return values: " + error);
	}

	void RuntimeModel::close_method_trace(MethodTracer::TraceId id) {
		if (MethodTracer::close(id))
			set_status("Closed method trace tab");
		else
			set_status("Stop a trace before closing it");
	}

} // namespace Explorer

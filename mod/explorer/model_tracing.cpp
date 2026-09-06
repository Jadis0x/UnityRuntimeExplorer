// Copyright (c) 2026 Jadis0x. All rights reserved.
#include "explorer_model.h"
#include "model_shared.h"

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

	void RuntimeModel::close_method_trace(MethodTracer::TraceId id) {
		if (MethodTracer::close(id))
			set_status("Closed method trace tab");
		else
			set_status("Stop a trace before closing it");
	}

} // namespace Explorer

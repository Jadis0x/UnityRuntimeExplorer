// Copyright (c) 2026 Jadis0x. All rights reserved.
#pragma once

// Helpers that more than one Explorer panel needs. Panels live in their own
// translation units; anything they share is declared here and defined in the
// unit that owns the state behind it.

#include "explorer_types.h"
#include "method_tracer.h"
#include "watch_analysis.h"

#include <cstdint>
#include <string_view>

namespace Explorer::UI {

bool contains_case_insensitive(std::string_view text, std::string_view filter);

// Object Inspector tab state. Defined in explorer_ui.cpp, which owns the strip.
void request_object_reference_tab(std::uint64_t token);
bool &object_inspector_window_requested();

void enqueue_reference_inspection(std::uint64_t token, bool request_object_tab = true);
void enqueue_raw_reference_inspection(std::uint64_t address);
bool render_reference_context_menu(const ComponentInfo::LiveValues::Reference *reference, bool allow_assign = false);
// Attaches a right-click "Copy value" menu (with a matching hover tooltip) to
// the item rendered immediately before this call.
void render_copy_context_menu(const char *popup_id, std::string_view text);

// Opens every ancestor of `instance_id` in the Hierarchy tree and scrolls to
// it. Selecting an object from outside the tree - a screen pick, a reference
// jump - is useless if the row stays folded away somewhere off screen.
// Defined in ui_hierarchy.cpp.
void reveal_in_hierarchy(int instance_id);

void enqueue_method_trace_clear(MethodTracer::TraceId id);
void enqueue_field_watch(int component_id, int field_index, bool enabled,
                         std::uint64_t object_inspector_token = 0, bool property = false,
                         bool class_browser_target = false);
void enqueue_field_watch_clear(std::uint64_t id);
void enqueue_field_watch_close(std::uint64_t id);
void enqueue_watch_alarm(std::uint64_t id, WatchAnalysis::AlarmCondition condition, float threshold);

// Object Inspector, defined in ui_object_inspector.cpp.
void render_current_object_inspector(const Snapshot &snapshot);
void render_object_inspector(const Snapshot &snapshot);
void close_object_reference_tab(std::uint64_t token);

// Component inspector, defined in ui_inspector.cpp.
void render_inspector(const Snapshot &snapshot);
void render_current_inspector(const Snapshot &snapshot);

// Class Browser, defined in ui_class_browser.cpp.
void render_class_browser(const Snapshot &snapshot);
std::uint64_t class_browser_target_token();

// Panels defined in ui_trace_panel.cpp.
void render_method_traces(const Snapshot &snapshot);
void render_field_watches(const Snapshot &snapshot);

} // namespace Explorer::UI

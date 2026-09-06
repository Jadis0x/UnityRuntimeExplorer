// Copyright (c) 2026 Jadis0x. All rights reserved.
#pragma once

// Member rendering primitives and the C++ code generation behind the member
// context menus. Every inspector panel draws fields, properties and methods
// through this surface.

#include "explorer_commands.h"
#include "explorer_types.h"
#include "method_tracer.h"
#include "ui_state.h"

#include <imgui.h>

#include <cstdio>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace Explorer::UI {

// Identifies the managed owner a generated snippet should target.
struct CodeContext {
    std::string image;
    std::string namespc;
    std::string class_name;
    std::string game_object_name;
};

CodeContext code_context(std::string_view image, std::string_view namespc, std::string_view class_name,
                         std::string_view full_name = {});
CodeContext declaring_code_context(CodeContext context, std::string_view declaring_type);

// Buffers the Add Component popup types into.
struct AddComponentBuffers {
    std::array<char, 128> image{};
    std::array<char, 128> namespc{};
    std::array<char, 128> class_name{};
    std::array<char, 192> class_search{};
    bool catalog_requested = false;
};
AddComponentBuffers &component_buffers();

// `copy_text` for a fixed-size buffer stays inline so any array size works.
void copy_text(auto &buffer, std::string_view value) {
    std::snprintf(buffer.data(), buffer.size(), "%.*s", static_cast<int>(value.size()), value.data());
}
void copy_text(std::vector<char> &buffer, std::string_view value);

void enqueue_simple(CommandKind kind, int instance_id);
std::string type_details_text(std::string_view assembly, std::string_view namespc, std::string_view class_name,
                              std::string_view full_name);
const MethodTracer::Snapshot *trace_for_method(const std::vector<MethodTracer::Snapshot> &traces,
                                               const ComponentInfo::Method &method);
const Snapshot::FieldWatch *field_watch_for(const Snapshot &snapshot, int component_instance_id,
                                            std::size_t field_index, std::uint64_t object_inspector_token = 0,
                                            bool property = false);

// Shared widgets.
bool input_text_dynamic(const char *label, const char *hint, std::vector<char> &buffer);
bool workspace_button(const char *label, const ImVec4 &color,
                      const ImVec4 &text_color = ImVec4(0.90f, 0.91f, 0.92f, 1.0f));
bool workspace_link_button(const char *label, const char *url, const ImVec4 &color,
                           const ImVec4 &text_color = ImVec4(0.90f, 0.91f, 0.92f, 1.0f));
void render_hierarchy(const HierarchyInfo &hierarchy, int selected_instance_id,
                      const Snapshot::TransformClipboard &clipboard);

// Layout.
bool property_label(const char *label);
void render_type_details(const char *label, std::string_view assembly, std::string_view namespc,
                         std::string_view class_name, std::string_view full_name, bool default_open = true);
void render_identity(const InspectorInfo &info);
void render_transform(const InspectorInfo &info, const Snapshot::TransformClipboard &clipboard);

// Per-widget editor state, keyed so each inspector tab keeps its own drafts.
MemberBuffer &member_buffer(std::uint64_t key);
bool &method_boolean_argument(std::uint64_t key);
std::array<char, 128> &component_filter(int component_id);
bool &component_show_inherited(int component_id);
int &component_member_tab(int component_id);
int &object_member_tab_for(std::uint64_t token);
std::array<char, 128> &object_member_filter(std::uint64_t token);
std::uint64_t scoped_ui_key(std::uint64_t scope, std::uint64_t domain, std::size_t first, std::size_t second = 0);
std::uint64_t method_argument_key(int component_id, std::size_t method, std::size_t parameter,
                                  std::uint64_t object_inspector_token = 0);
std::uint64_t generic_type_key(int component_id, std::size_t method, std::uint64_t object_inspector_token = 0);

// Filtering.
bool member_matches_filter(std::string_view name, std::string_view type, std::string_view declaring_type,
                           std::string_view filter);
bool method_matches_filter(const ComponentInfo::Method &method, std::string_view filter);

// Context menus. `extra` prepends caller-specific entries and may be empty.
void render_field_context_menu(const ComponentInfo::Field &field, const CodeContext &context,
                               const std::function<void()> &extra);
void render_property_context_menu(const ComponentInfo::Property &property, const CodeContext &context,
                                  const std::function<void()> &extra);
void render_method_context_menu(const ComponentInfo::Method &method, const CodeContext &context);

// Values and invocation.
bool editable_value(const URK::Unity::Inspect::ValueInfo &value);
bool invokable_method(const ComponentInfo::Method &method);
bool boolean_type(std::string_view type);
void render_live_value(CommandKind kind, int component_id, int member_index,
                       const URK::Unity::Inspect::ValueInfo *value, bool writable, std::uint64_t buffer_key,
                       const ComponentInfo::LiveValues::Reference *reference,
                       bool object_inspector_target = false, bool live_data = false, bool locked = false,
                       bool lockable = true, std::uint64_t object_inspector_token = 0, bool runtime_safe = true,
                       std::string_view capability_reason = {},
                       const std::vector<ManagedReferenceInfo> *managed_references = nullptr);
void render_method_argument(int component_id, std::size_t method_index, std::size_t parameter_index,
                            std::string_view type, std::string_view name,
                            std::uint64_t object_inspector_token = 0,
                            const std::vector<ManagedReferenceInfo> *managed_references = nullptr);
void render_generic_type_input(const Snapshot &snapshot, const char *id, std::vector<char> &buffer);
void render_trace_button(const std::function<void(bool)> &start_trace);
void render_method_result(const Snapshot &snapshot, int component_id, std::size_t method_index,
                          std::uint64_t object_inspector_token = 0);
void render_member_write_result(const Snapshot &snapshot, int component_id, std::size_t member_index,
                                bool property, std::uint64_t object_inspector_token = 0);

void enqueue_method_invoke(int component_id, int method_index, const ComponentInfo::Method &method,
                           bool object_inspector_target = false, std::uint64_t object_inspector_token = 0);
void enqueue_method_trace(int component_id, int method_index, bool enabled, bool object_inspector_target = false,
                          std::uint64_t object_inspector_token = 0, bool capture_return = false);

} // namespace Explorer::UI

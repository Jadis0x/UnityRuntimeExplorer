// Copyright (c) 2026 Jadis0x. All rights reserved.
#pragma once

#include "camera/camera_focus_controller.h"
#include "explorer_commands.h"
#include "method_tracer.h"
#include "managed_reference_store.h"
#include "sdk/unity/unity.h"
#include "sdk/unity/unity_inspect.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Explorer {

namespace Mcp { class RuntimeTools; }

class RuntimeModel {
  public:
    static RuntimeModel &instance();
    ~RuntimeModel();

    void start();
    void tick();
    void stop();
    // Called by the main-thread SEH boundary; recovery waits for the next tick.
    void notify_native_fault(std::uint32_t code = 0, std::uintptr_t address = 0,
                             std::uintptr_t instruction = 0);
    void request_refresh();
    void enqueue(Command command);
    std::shared_ptr<const Snapshot> snapshot() const;

  private:
    friend class Mcp::RuntimeTools;
    using Clock = std::chrono::steady_clock;

    void process_commands();
    void process_command(const Command &command);
    bool refresh_hierarchy();
    void refresh_inspector(bool include_components);
    void load_component_metadata(int component_instance_id);
    void load_component_class_catalog();
    void load_class_browser_catalog();
    void find_class_instances(const Command &command);
    void build_reference_graph(const Command &command);
    void clear_reference_graph();
    void continue_class_instance_scan();
    void clear_class_instance_scan();
    void load_class_browser_static_state(const Command &command);
    void load_class_browser_members(const Command &command);
    void set_class_browser_static_field(const Command &command);
    void create_class_instance(const Command &command);
    void refresh_live_member_values(bool force = false);
    void clear_component_cache();
    void clear_object_inspector();
    void set_member_value(const Command &command, bool property,
                          const URK::Unity::Inspect::ValueInfo *prepared = nullptr, bool verify = true);
    void apply_locked_members();
    void clear_locked_members(bool nested_only = false);
    void sample_member_value(const Command &command);
    void write_back_value_type_object_inspector();
    void invoke_method(const Command &command);
    void set_method_trace(const Command &command);
    void clear_method_trace(MethodTracer::TraceId id);
    void close_method_trace(MethodTracer::TraceId id);
    void set_field_watch(const Command &command);
    void configure_field_watch(const Command &command);
    void clear_field_watch(std::uint64_t id);
    void close_field_watch(std::uint64_t id);
    void release_all_field_watches();
    void refresh_field_watches();
    bool has_active_field_watches() const;
    ComponentInfo::LiveValues::Reference watch_reference_for(const URK::Unity::Inspect::ValueInfo &value);
    void release_field_watch_references(Snapshot::FieldWatch &watch);
    void delete_component(int component_instance_id);
    void inspect_reference(std::uint64_t token);
    void inspect_raw_reference(std::uint64_t address);
    void close_object_inspector_tab(std::uint64_t token);
    void load_scene(int build_index, std::string_view scene_key);
    void update_pending_scene_load();
    void pin_managed_reference(const Command &command);
    bool managed_reference_value_from_text(std::string_view type_name, const void* destination_type,
                                           std::string_view text, URK::Unity::Inspect::ValueInfo& value);
    void refresh_object_inspector_values(bool force = false);
    void release_reference_handle(std::uint64_t token);
    void update_highlight();
    void update_camera_focus();
    void focus_selected_camera(URK::Unity::GameObject object);
    void restore_focused_camera();
    void clear_highlight_renderer_cache();
    void clear_highlight_camera_cache();
    // The hierarchy stores non-owning Unity object wrappers. Resolve a fresh
    // object at selection time so a scene transition cannot turn a stale
    // hierarchy entry into an inspector target.
    URK::Unity::GameObject resolve_live_game_object(
        int instance_id, URK::Unity::Inspect::ObjectHandle &root) const;
    URK::Unity::GameObject resolve_selected_object() const;
    URK::Unity::Object resolve_component(int instance_id) const;
    void select_object(URK::Unity::GameObject object, URK::Unity::Inspect::ObjectHandle root);
    void clear_selection();
    // After an SEH fault, do not call back into the managed runtime to release handles:
    // one of those handles may be the invalid pointer that raised the fault.
    void discard_managed_state_after_native_fault();
    void set_status(std::string message);
    void record_flight(std::string stage, std::string operation, std::string detail = {});
    static const char* command_name(CommandKind kind);
    void record_value_error(std::string context, const URK::Unity::Inspect::ValueInfo &value);
    void capture_last_error(std::string_view action);
    void publish();
    void publish_recovery_snapshot();

    mutable std::mutex command_mutex_;
    std::vector<Command> commands_;
    std::atomic<bool> refresh_requested_{true};
    std::atomic<std::shared_ptr<const Snapshot>> published_;
    Snapshot working_{};
    std::shared_ptr<const HierarchyInfo> hierarchy_;
    struct HierarchyCensus;
    std::unique_ptr<HierarchyCensus> hierarchy_census_;
    // Membership only; hierarchy snapshots must never retain managed wrappers.
    std::unordered_set<int> hierarchy_instance_ids_;
    // Keeps cached selected components alive.
    std::unordered_map<int, URK::Unity::Inspect::ObjectHandle> component_handles_;
    std::unordered_map<std::uint64_t, URK::Unity::Inspect::ObjectHandle> reference_handles_;
    struct TraceReturnKey {
        MethodTracer::TraceId trace_id = 0;
        std::uint64_t sequence = 0;
        bool operator==(const TraceReturnKey&) const = default;
    };
    struct TraceReturnKeyHash {
        std::size_t operator()(const TraceReturnKey& key) const {
            return std::hash<std::uint64_t>{}(key.trace_id) ^
                (std::hash<std::uint64_t>{}(key.sequence) + 0x9e3779b9u +
                 (std::hash<std::uint64_t>{}(key.trace_id) << 6u) +
                 (std::hash<std::uint64_t>{}(key.trace_id) >> 2u));
        }
    };
    std::unordered_map<TraceReturnKey, std::uint64_t, TraceReturnKeyHash> traced_return_references_;
    // Only traces installed through MCP are revoked when the MCP permission is
    // disabled; traces started from the in-game UI remain under UI control.
    std::unordered_set<MethodTracer::TraceId> mcp_method_trace_ids_;
    ManagedReferenceStore managed_references_;
    // Object Inspector tabs retain their own managed handles.
    std::unordered_map<std::uint64_t, URK::Unity::Inspect::ObjectHandle> object_inspector_history_;
    std::uint64_t next_reference_token_ = 1;
    std::uint64_t next_hierarchy_revision_ = 1;
    std::uint64_t scene_generation_ = 1;
    std::atomic<std::uint64_t> next_command_sequence_{1};
    std::uint64_t next_flight_sequence_ = 1;
    Clock::time_point flight_recorder_started_{};
    std::uint64_t next_method_result_id_ = 1;
    std::uint64_t next_field_watch_id_ = 1;
    URK::Unity::Inspect::ObjectHandle object_inspector_handle_{};
    struct ComponentReflection {
        std::vector<URK::Unity::Inspect::FieldInfo> fields;
        std::vector<URK::Unity::Inspect::PropertyInfo> properties;
        std::vector<URK::Unity::Inspect::MethodInfo> methods;
    };
    std::unordered_map<int, ComponentReflection> component_reflection_;
    std::string active_metadata_stage_;
    std::shared_ptr<const ComponentClassCatalog> component_class_catalog_;
    std::shared_ptr<const ClassBrowserCatalog> class_browser_catalog_;
    struct ClassInstanceScan;
    std::unique_ptr<ClassInstanceScan> class_instance_scan_;
    ComponentReflection class_browser_reflection_;
    std::unordered_map<std::uint64_t, URK::Unity::Inspect::ObjectHandle> class_browser_handles_;
    std::unordered_map<std::uint64_t, URK::Unity::Inspect::ObjectHandle> class_browser_static_handles_;
    std::unordered_map<std::uint64_t, URK::Unity::Inspect::ObjectHandle> reference_graph_handles_;
    std::unordered_set<std::uint64_t> sampled_component_members_;
    struct FieldWatchState {
        Snapshot::FieldWatch snapshot;
        URK::Unity::Inspect::FieldInfo field;
        URK::Unity::Inspect::PropertyInfo property;
        URK::Unity::Inspect::ObjectHandle target_handle;
        URK::Unity::Inspect::ValueInfo last_value;
        bool has_baseline = false;
        bool alarm_latched = false;
        bool explorer_write_pending = false;
        Clock::time_point started{};
    };
    std::unordered_map<std::uint64_t, FieldWatchState> field_watches_;
    std::unordered_set<std::size_t> sampled_object_fields_;
    std::unordered_set<std::size_t> sampled_object_properties_;
    std::unordered_set<std::string> logged_value_errors_;
    struct LockedMember {
        Command command;
        URK::Unity::Inspect::ValueInfo value;
        URK::Unity::Inspect::ObjectHandle value_root;
    };
    std::unordered_map<std::uint64_t, LockedMember> locked_members_;
    ComponentReflection object_inspector_reflection_;
    // A hierarchy snapshot only contains non-owning pointers. Keep the active
    // managed GameObject wrapper rooted independently from that snapshot.
    URK::Unity::Inspect::ObjectHandle selected_handle_{};
    URK::Unity::GameObject selected_{};
    std::vector<URK::Unity::Inspect::ObjectHandle> highlight_renderers_;
    // Cache active cameras instead of relying on Camera.main.
    std::vector<URK::Unity::Inspect::ObjectHandle> highlight_cameras_;
    std::uint32_t highlight_id_ = 0;
    std::uint32_t highlight_locator_id_ = 0;
    bool highlight_enabled_ = true;
    // Zero means unlimited highlight distance.
    float highlight_max_distance_ = 0.0f;
    CameraFocus::Controller camera_focus_;
    int active_scene_handle_hint_ = 0;
    std::string active_scene_name_hint_;
    struct PendingSceneLoad {
        bool active = false;
        int build_index = -1;
        int previous_build_index = -1;
        std::string key;
        std::string previous_name;
        Clock::time_point requested{};
    } pending_scene_load_;
    std::string logged_hierarchy_signature_;
    std::string logged_component_query_error_;
    Clock::time_point next_inspector_refresh_{};
    // Highlight bounds refresh more often than reflective inspector data.
    Clock::time_point next_highlight_refresh_{};
    Clock::time_point next_highlight_camera_refresh_{};
    Clock::time_point next_member_value_refresh_{};
    Clock::time_point next_field_watch_refresh_{};
    Clock::time_point next_trace_publish_{};
    Clock::time_point next_class_scan_publish_{};
    bool event_refresh_pending_ = false;
    bool live_data_ = false;
    std::atomic<bool> native_faulted_{false};
    std::atomic<std::uint32_t> native_fault_code_{0};
    std::atomic<std::uintptr_t> native_fault_address_{0};
    std::atomic<std::uintptr_t> native_fault_instruction_{0};
    Clock::time_point event_refresh_due_{};

};

} // namespace Explorer

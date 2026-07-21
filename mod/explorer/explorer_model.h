// Copyright (c) 2026 Jadis0x. All rights reserved.
#pragma once

#include "method_tracer.h"
#include "sdk/unity/unity.h"
#include "sdk/unity/unity_inspect.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Explorer {

struct HierarchyNode {
    int instance_id = 0;
    std::uintptr_t object_address = 0;
    std::string name;
    std::string pointer_text;
    bool active = false;
    std::vector<HierarchyNode> children;
};

struct SceneNode {
    int handle = 0;
    std::string name;
    bool active = false;
    bool dont_destroy_on_load = false;
    bool hide_and_dont_save = false;
    std::vector<HierarchyNode> roots;
};

struct HierarchyInfo {
    std::vector<SceneNode> scenes;
    std::string source;
    std::size_t roots = 0;
    std::size_t objects = 0;
    std::uint64_t revision = 0;
    std::uint64_t scene_generation = 0;
};

struct ComponentInfo {
    int instance_id = 0;
    std::string type_name;
    std::string assembly_name;
    std::string namespace_name;
    std::string class_name;
    std::string pointer_text;
    bool enabled_supported = false;
    bool enabled = false;
    bool metadata_unavailable = false;
    std::string metadata_error;
    struct Field {
        std::string name;
        std::string type_name;
        std::string declaring_type;
        bool is_static = false;
        std::string pointer_text;
    };
    struct Property {
        std::string name;
        std::string type_name;
        std::string declaring_type;
        bool can_read = false;
        bool can_write = false;
        std::string pointer_text;
    };
    struct Method {
        std::string name;
        std::string return_type;
        std::string declaring_type;
        std::vector<std::string> parameter_types;
        std::vector<std::string> parameter_names;
        bool is_static = false;
        std::string pointer_text;
    };
    // Reflection metadata is immutable and shared across snapshots.
    struct Metadata {
        std::vector<Field> fields;
        std::vector<Property> properties;
        std::vector<Method> methods;
    };
    std::shared_ptr<const Metadata> metadata;
    struct LiveValues {
        struct Reference {
            std::uint64_t token = 0;
            std::string type_name;
            std::string display;
            std::string pointer_text;
            bool is_null = true;
        };
        std::vector<URK::Unity::Inspect::ValueInfo> fields;
        std::vector<URK::Unity::Inspect::ValueInfo> properties;
        std::vector<Reference> field_references;
        std::vector<Reference> property_references;
    };
    // Replaced atomically by the model.
    std::shared_ptr<const LiveValues> live_values;
};

struct ComponentClassInfo {
    // Image name is the exact value required by GameObject::AddComponent.
    std::string image;
    std::string namespc;
    std::string class_name;
    std::string full_name;
    std::string pointer_text;
};

struct ComponentClassCatalog {
    std::vector<ComponentClassInfo> classes;
};

struct BrowserClassInfo {
    std::string image;
    std::string namespc;
    std::string class_name;
    std::string full_name;
    std::string pointer_text;
    std::string parent_name;
    std::vector<std::string> interfaces;
    bool is_component = false;
    bool is_unity_object = false;
    bool is_interface = false;
    bool is_value_type = false;
    bool is_enum = false;
    bool is_static = false;
    bool is_abstract = false;
};

struct ClassBrowserCatalog {
    std::vector<BrowserClassInfo> classes;
};

struct ClassBrowserInstanceInfo {
    std::uint64_t token = 0;
    std::string name;
    std::string type_name;
    std::string pointer_text;
    std::string source;
    int game_object_instance_id = 0;
    std::string game_object_name;
};

struct ClassBrowserStaticFieldInfo {
    std::uint64_t token = 0;
    std::string name;
    std::string type_name;
    std::string declaring_type;
    std::string display;
    std::string pointer_text;
    bool readable = false;
    bool is_reference = false;
};

struct ObjectInspectorInfo {
    bool valid = false;
    bool is_value_type = false;
    bool is_array = false;
    std::uint64_t token = 0;
    std::size_t array_length = 0;
    std::size_t array_offset = 0;
    std::string type_name;
    std::string pointer_text;
    std::string array_element_type;
    // For arrays, `fields`/`field_references` are element values/references.
    std::shared_ptr<const ComponentInfo::LiveValues> array_values;
    // Boxed values are copied back to their source member after editing.
    int value_origin_component_id = 0;
    int value_origin_member_index = -1;
    bool value_origin_property = false;
    ComponentInfo component;
};

struct InspectorInfo {
    bool valid = false;
    int instance_id = 0;
    std::string pointer_text;
    std::string type_name;
    std::string assembly_name;
    std::string namespace_name;
    std::string class_name;
    std::string name;
    std::string tag;
    int layer = 0;
    bool is_static = false;
    bool active = false;
    URK::Unity::Vector3 local_position{};
    URK::Unity::Vector3 local_rotation{};
    URK::Unity::Vector3 local_scale{1.0f, 1.0f, 1.0f};
    std::vector<ComponentInfo> components;
};

struct Snapshot {
    struct FieldWatchEvent {
        std::uint64_t sequence = 0;
        double seconds_since_start = 0.0;
        std::string previous_value;
        std::string current_value;
        // Lets the history open a referenced object while it remains alive.
        ComponentInfo::LiveValues::Reference current_reference;
    };
    struct FieldWatch {
        std::uint64_t id = 0;
        int component_instance_id = 0;
        std::size_t field_index = 0;
        std::string component_type;
        std::string field_name;
        std::string field_type;
        bool active = false;
        bool value_available = false;
        std::string current_value;
        ComponentInfo::LiveValues::Reference current_reference;
        std::uint64_t change_count = 0;
        std::vector<FieldWatchEvent> events;
    };
    struct MethodResult {
        int component_instance_id = 0;
        std::size_t method_index = 0;
        // Zero is a component invocation; non-zero scopes a result to one tab.
        std::uint64_t object_inspector_token = 0;
        std::string return_type;
        std::string display;
        ComponentInfo::LiveValues::Reference reference;
    };
    std::shared_ptr<const HierarchyInfo> hierarchy;
    std::shared_ptr<const ComponentClassCatalog> component_class_catalog;
    std::shared_ptr<const ClassBrowserCatalog> class_browser_catalog;
    BrowserClassInfo class_browser_query;
    std::vector<ClassBrowserInstanceInfo> class_browser_instances;
    BrowserClassInfo class_browser_static_query;
    std::vector<ClassBrowserStaticFieldInfo> class_browser_static_fields;
    BrowserClassInfo class_browser_members_query;
    std::shared_ptr<const ComponentInfo::Metadata> class_browser_members;
    std::size_t class_browser_scanned_objects = 0;
    std::size_t class_browser_static_roots = 0;
    bool class_browser_scan_truncated = false;
    InspectorInfo inspector;
    ObjectInspectorInfo object_inspector;
    bool live_data = false;
    int selected_instance_id = 0;
    std::string status;
    std::vector<std::string> diagnostics;
    // The most recent result of each executed method.  Object results retain a
    // tracked reference so the UI can open them in the Object Inspector.
    std::unordered_map<std::uint64_t, MethodResult> method_results;
    std::vector<MethodTracer::Snapshot> method_traces;
    std::vector<FieldWatch> field_watches;
    std::unordered_set<std::uint64_t> locked_member_keys;
    std::size_t strong_handle_count = 0;
    std::size_t weak_handle_count = 0;
    std::uint64_t quarantined_handle_count = 0;
    bool hierarchy_census_active = false;
    std::size_t hierarchy_census_processed = 0;
    std::size_t hierarchy_census_candidates = 0;
    std::int64_t managed_used_bytes = 0;
    std::int64_t managed_heap_bytes = 0;
    std::uint64_t revision = 0;
};

enum class CommandKind {
    Select = 0,
    ClearSelection = 1,
    Refresh = 2,
    DeleteObject = 3,
    DuplicateObject = 4,
    Rename = 5,
    SetTag = 6,
    SetLayer = 7,
    SetStatic = 8,
    SetActive = 9,
    SetLocalPosition = 10,
    SetLocalRotation = 11,
    SetLocalScale = 12,
    AddComponent = 13,
    DeleteComponent = 14,
    SetComponentEnabled = 15,
    SetLiveData = 16,
    LoadComponentMetadata = 17,
    LoadComponentClassCatalog = 18,
    LoadClassBrowserCatalog = 19,
    FindClassInstances = 20,
    LoadClassBrowserStaticState = 21,
    LoadClassBrowserMembers = 22,
    SetFieldValue = 23,
    SetPropertyValue = 24,
    SampleMemberValue = 25,
    SetArrayPage = 26,
    InvokeMethod = 27,
    SetMethodTrace = 28,
    ClearMethodTrace = 29,
    CloseMethodTrace = 30,
    SetFieldWatch = 31,
    ClearFieldWatch = 32,
    CloseFieldWatch = 33,
    InspectReference = 34,
    InspectRawReference = 35,
    CloseObjectInspectorTab = 36,
    SceneHint = 37,
    ObjectDestroyRequested = 38,
    ClearDiagnostics = 39,
};

struct Command {
    CommandKind kind = CommandKind::Refresh;
    int instance_id = 0;
    int int_value = 0;
    int member_index = -1;
    std::uint64_t reference_token = 0;
    // Identifies the Object Inspector tab that originated a nested command.
    // It prevents a queued command from being applied after the user switches
    // to another inspected object.
    std::uint64_t object_inspector_token = 0;
    // Captured from the immutable UI snapshot. Zero means that the producer is
    // a lifecycle hook rather than a hierarchy/inspector interaction.
    std::uint64_t scene_generation = 0;
    std::uint64_t hierarchy_revision = 0;
    std::uintptr_t expected_object_address = 0;
    std::uint64_t sequence = 0;
    bool bool_value = false;
    bool object_inspector_target = false;
    bool lock_value = false;
    bool unlock_value = false;
    URK::Unity::Vector3 vector_value{};
    std::string text;
    std::string image;
    std::string namespc;
    std::string class_name;
    std::vector<std::string> method_arguments;
};

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
    using Clock = std::chrono::steady_clock;

    void process_commands();
    void process_command(const Command &command);
    bool refresh_hierarchy();
    void refresh_inspector(bool include_components);
    void load_component_metadata(int component_instance_id);
    void load_component_class_catalog();
    void load_class_browser_catalog();
    void find_class_instances(const Command &command);
    void load_class_browser_static_state(const Command &command);
    void load_class_browser_members(const Command &command);
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
    void clear_field_watch(std::uint64_t id);
    void close_field_watch(std::uint64_t id);
    void refresh_field_watches();
    bool has_active_field_watches() const;
    ComponentInfo::LiveValues::Reference watch_reference_for(const URK::Unity::Inspect::ValueInfo &value);
    void release_field_watch_references(Snapshot::FieldWatch &watch);
    void delete_component(int component_instance_id);
    void inspect_reference(std::uint64_t token);
    void inspect_raw_reference(std::uint64_t address);
    void close_object_inspector_tab(std::uint64_t token);
    void refresh_object_inspector_values(bool force = false);
    void release_reference_handle(std::uint64_t token);
    void update_highlight();
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
    // After an SEH fault, do not call back into IL2CPP to release handles:
    // one of those handles may be the invalid pointer that raised the fault.
    void discard_managed_state_after_native_fault();
    void set_status(std::string message);
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
    // Object Inspector tabs retain their own managed handles.
    std::unordered_map<std::uint64_t, URK::Unity::Inspect::ObjectHandle> object_inspector_history_;
    std::uint64_t next_reference_token_ = 1;
    std::uint64_t next_hierarchy_revision_ = 1;
    std::uint64_t scene_generation_ = 1;
    std::atomic<std::uint64_t> next_command_sequence_{1};
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
    std::unordered_map<std::uint64_t, URK::Unity::Inspect::ObjectHandle> class_browser_handles_;
    std::unordered_map<std::uint64_t, URK::Unity::Inspect::ObjectHandle> class_browser_static_handles_;
    std::unordered_set<std::uint64_t> sampled_component_members_;
    struct FieldWatchState {
        Snapshot::FieldWatch snapshot;
        URK::Unity::Inspect::ValueInfo last_value;
        bool has_baseline = false;
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
    int active_scene_handle_hint_ = 0;
    std::string active_scene_name_hint_;
    std::string logged_hierarchy_signature_;
    Clock::time_point next_inspector_refresh_{};
    // Highlight bounds refresh more often than reflective inspector data.
    Clock::time_point next_highlight_refresh_{};
    Clock::time_point next_highlight_camera_refresh_{};
    Clock::time_point next_member_value_refresh_{};
    Clock::time_point next_field_watch_refresh_{};
    Clock::time_point next_trace_publish_{};
    bool event_refresh_pending_ = false;
    bool live_data_ = false;
    std::atomic<bool> native_faulted_{false};
    std::atomic<std::uint32_t> native_fault_code_{0};
    std::atomic<std::uintptr_t> native_fault_address_{0};
    std::atomic<std::uintptr_t> native_fault_instruction_{0};
    Clock::time_point event_refresh_due_{};

};

} // namespace Explorer

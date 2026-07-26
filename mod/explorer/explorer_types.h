// Copyright (c) 2026 Jadis0x. All rights reserved.
#pragma once

#include "byte_data_decoder.h"
#include "method_tracer.h"
#include "sdk/unity/unity.h"
#include "sdk/unity/unity_inspect.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Explorer {

struct HierarchyNode {
    int instance_id = 0;
    std::uintptr_t object_address = 0;
    std::string name;
    std::string tag;
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

struct SceneLoadInfo {
    int build_index = -1;
    std::string name;
    std::string path;
    bool loaded = false;
    bool active = false;
};

struct HierarchyInfo {
    std::vector<SceneNode> scenes;
    std::vector<SceneLoadInfo> available_scenes;
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
    // Some games expose managed scripts through a runtime bridge component.
    // This describes capabilities that are safe to discover without assuming a
    // game-specific serialization format.
    struct DynamicScriptBridge {
        bool detected = false;
        std::string behaviour_type;
        std::string type_getter;
        int type_getter_method_index = -1;
        std::vector<int> serialized_data_method_indices;
        std::vector<int> object_reference_method_indices;
        std::string diagnostic;
    } dynamic_bridge;
    struct Field {
        std::string name;
        std::string type_name;
        std::string declaring_type;
        bool is_static = false;
        bool is_read_only = false;
        std::string pointer_text;
        bool is_value_type = false;
        bool is_enum = false;
        bool runtime_safe = true;
        std::string capability_reason;
    };
    struct Property {
        std::string name;
        std::string type_name;
        std::string declaring_type;
        bool can_read = false;
        bool can_write = false;
        std::string pointer_text;
        bool is_value_type = false;
        bool is_enum = false;
        bool runtime_safe = true;
        std::string capability_reason;
    };
    struct Method {
        std::string name;
        std::string return_type;
        std::string declaring_type;
        std::vector<std::string> parameter_types;
        std::vector<std::string> parameter_names;
        std::vector<bool> parameter_is_value_types;
        std::vector<bool> parameter_is_enums;
        bool is_static = false;
        std::string pointer_text;
        bool return_is_value_type = false;
        bool return_is_enum = false;
        bool uses_generic_parameter = false;
        bool runtime_callable = true;
        std::string capability_reason;
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
    std::size_t member_index = 0;
    std::string name;
    std::string type_name;
    std::string declaring_type;
    std::string display;
    std::string pointer_text;
    URK::Unity::Inspect::ValueInfo value;
    bool is_property = false;
    bool readable = false;
    bool writable = false;
    bool is_reference = false;
};

struct ObjectInspectorInfo {
    bool valid = false;
    bool is_value_type = false;
    bool is_array = false;
    std::uint64_t token = 0;
    int instance_id = 0;
    std::size_t array_length = 0;
    std::size_t array_offset = 0;
    std::string type_name;
    std::string assembly_name;
    std::string namespace_name;
    std::string class_name;
    std::string pointer_text;
    std::string array_element_type;
    struct ByteArrayInspection {
        // Native snapshot copied on the game thread. UI-side decoding never
        // reads a managed array or retains its raw managed address.
        std::vector<std::uint8_t> bytes;
        ByteData::DecodeResult decoded;
        std::string read_error;
        bool truncated = false;
    };
    std::shared_ptr<const ByteArrayInspection> byte_array;
    // For arrays, `fields`/`field_references` are element values/references.
    std::shared_ptr<const ComponentInfo::LiveValues> array_values;
    // Boxed values are copied back to their source member after editing.
    int value_origin_component_id = 0;
    int value_origin_member_index = -1;
    bool value_origin_property = false;
    ComponentInfo component;
};

struct ManagedReferenceInfo {
    std::uint64_t token = 0;
    std::string type_name;
    std::string display;
    std::string pointer_text;
    std::string source;
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
    float camera_distance = 0.0f;
    bool camera_distance_valid = false;
    std::vector<ComponentInfo> components;
};

struct Snapshot {
    struct FlightEvent {
        std::uint64_t sequence = 0;
        double seconds_since_start = 0.0;
        std::string stage;
        std::string operation;
        std::string detail;
    };
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
        std::uint64_t object_inspector_token = 0;
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
        // Non-zero scopes a result to an Object Inspector or Class Browser target.
        std::uint64_t object_inspector_token = 0;
        std::string return_type;
        bool succeeded = false;
        double elapsed_milliseconds = 0.0;
        std::string display;
        ComponentInfo::LiveValues::Reference reference;
    };
    // Strong references available to argument editors.
    std::vector<ManagedReferenceInfo> managed_references;
	struct MemberWriteResult {
		int component_instance_id = 0;
		std::size_t member_index = 0;
		bool property = false;
		std::uint64_t object_inspector_token = 0;
		bool succeeded = false;
		std::string display;
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
    bool highlight_enabled = true;
    // This must be configurable because a practical focus distance differs
    // widely between games and object scales.
    float camera_focus_distance = 8.0f;
    float camera_focus_tilt = 3.0f;
    URK::Unity::Vector3 camera_focus_offset{};
    bool camera_focus_top_down = false;
    bool camera_focus_active = false;
    int selected_instance_id = 0;
    std::string status;
    std::vector<std::string> diagnostics;
    // Native-only breadcrumb data: retained across an SEH recovery so the
    // operation that faulted is still visible after managed state is released.
    std::vector<FlightEvent> flight_recorder;
    // The most recent result of each executed method.  Object results retain a
    // tracked reference so the UI can open them in the Object Inspector.
    std::unordered_map<std::uint64_t, MethodResult> method_results;
	std::unordered_map<std::uint64_t, MemberWriteResult> member_write_results;
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

} // namespace Explorer

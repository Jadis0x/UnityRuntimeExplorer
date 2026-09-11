// Copyright (c) 2026 Jadis0x. All rights reserved.
#pragma once

// Helpers that more than one RuntimeModel domain file needs. Domains live in
// their own translation units (model_hierarchy.cpp, model_class_browser.cpp,
// model_object_inspector.cpp, model_members.cpp, model_reference_graph.cpp,
// model_field_watch.cpp, model_tracing.cpp, model_scene.cpp,
// model_camera_highlight.cpp); explorer_model.cpp keeps the top-level command
// dispatch and lifecycle shell. Anything more than one of those units needs is
// declared here and defined in model_shared.cpp.

#include "explorer_commands.h"
#include "explorer_model.h"
#include "explorer_types.h"
#include "sdk/unity/unity.h"
#include "sdk/unity/unity_inspect.h"

#include <chrono>
#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Explorer {

// RuntimeModel forward-declares these two as private nested types so the
// class declaration does not have to expose census/scan implementation
// details. Their complete definitions live here because more than one
// translation unit needs them: the owning domain file (model_hierarchy.cpp,
// model_class_browser.cpp) builds them incrementally across ticks, while
// explorer_model.cpp's destructor and publish() reset/inspect the
// unique_ptr<...> members directly and therefore need a complete type too.
// Flattened GameObject discovered by a hierarchy census slice. A plain,
// non-nested type so model_hierarchy.cpp's free-function helpers can name it
// without needing access to the (private) HierarchyCensus nested type.
struct FlatObject {
	URK::Unity::GameObject object{};
	int instance_id = 0;
	int parent_id = 0;
	std::string name;
	std::string tag;
	bool active = false;
	std::vector<std::string> component_types;
	std::vector<std::string> dynamic_behaviour_types;
	bool discovery_signature_complete = true;
};

struct RuntimeModel::HierarchyCensus {
	HierarchyInfo next;
	std::uint64_t scene_generation = 0;
	std::unordered_map<int, std::size_t> loaded_scene_indices;
	std::size_t ddol_index = 0;
	std::size_t hidden_index = 0;
	// Scene node that receives roots when GameObject.scene cannot be read,
	// which happens in IL2CPP builds whose managed stripping dropped the
	// getter. Points at the active scene, or the DontDestroyOnLoad node when
	// no scene is loaded.
	std::size_t fallback_scene_index = 0;
	bool scene_lookup_available = true;
	URK::Unity::detail::RootedObjectArray<URK::Unity::GameObject> candidates;
	std::size_t candidate_count = 0;
	std::size_t candidate_index = 0;
	std::vector<FlatObject> flat;
	std::unordered_map<int, std::size_t> flat_indices;
	Clock::time_point started{};
	std::chrono::microseconds max_slice{};
};

struct RuntimeModel::ClassInstanceScan {
	enum class Phase { StaticRoots, ReachableGraph };
	struct PendingObject {
		URK::Unity::Inspect::ObjectHandle handle;
		int depth = 0;
		bool array = false;
		std::string source;
	};
	Phase phase = Phase::StaticRoots;
	const URK::managed::Class* target = nullptr;
	const URK::managed::Class* unity_object_base = nullptr;
	std::size_t assembly_index = 0;
	std::size_t class_index = 0;
	const URK::managed::Class* static_owner = nullptr;
	std::string static_owner_name;
	std::vector<URK::Unity::Inspect::FieldInfo> static_fields;
	std::size_t static_field_index = 0;
	std::deque<PendingObject> pending;
	std::unordered_set<void*> seen;
	Clock::time_point started{};
};

// Walks every loaded assembly's classes and remembers each method's native
// entry point, so a trace record's raw return address can be named. Sliced the
// same way the instance scan is: a full IL2CPP image holds hundreds of
// thousands of methods and doing it in one go would stall the game.
struct RuntimeModel::CallerIndexScan {
	std::size_t assembly_index = 0;
	std::size_t class_index = 0;
	std::size_t indexed_methods = 0;
	std::size_t scanned_classes = 0;
	Clock::time_point started{};
	// Wall time is frame-bound -- one slice per tick -- so the completion log
	// separates the two: how much CPU the walk actually cost, and how many
	// ticks it was spread over. Without both, a slow index looks the same
	// whether the metadata walk is expensive or the game is rendering few
	// frames.
	std::chrono::microseconds slice_time{};
	std::size_t ticks = 0;
};

// Command queue processing debounces destroy-triggered refreshes so a burst
// of native lifecycle events collapses into a single hierarchy rescan.
inline constexpr auto kEventRefreshDebounce = std::chrono::milliseconds(180);

#if defined(_WIN32)
struct NativeFaultRecord {
	std::uint32_t code = 0;
	std::uintptr_t address = 0;
	std::uintptr_t instruction = 0;
};

// SEH filter shared by every __try/__except boundary across the domain files.
int capture_native_fault(void* raw_info);
// The fault most recently captured on the calling thread by capture_native_fault.
const NativeFaultRecord& last_native_fault();
#endif

std::string pointer_text(void* pointer);
bool readable_address(std::uintptr_t address);

// Destroyed Unity wrappers can keep a valid address and instance ID.
// UnityEngine.Object.op_Implicit is the backend-neutral Unity lifetime check.
// A property whose getter IL2CPP stripped is still readable when the compiler
// left its backing field behind, which it does for every auto-property. Returns
// null for hand-written properties and for engine types like AudioSource, whose
// values live in native code with no managed field to fall back on.
const URK::Unity::Inspect::FieldInfo *backing_field_for(
	const std::vector<URK::Unity::Inspect::FieldInfo> &fields, std::string_view property_name,
	std::string_view declaring_type);

bool safe_object_alive(URK::Unity::Object object);
std::string safe_runtime_class_name(URK::Unity::Object object);
URK::Unity::Inspect::TypeInfo safe_type_of(URK::Unity::Object object);

std::string normalized_type(std::string_view name);
std::string scene_display_name(std::string_view path, int build_index);

ComponentInfo::Field field_metadata(const URK::Unity::Inspect::FieldInfo& field);

// Keeps boxed values and returned strings alive until their Inspector tab closes.
URK::Unity::Inspect::ObjectHandle tracked_reference_handle(const URK::Unity::Inspect::ValueInfo& value);

bool values_equivalent(const URK::Unity::Inspect::ValueInfo& expected, const URK::Unity::Inspect::ValueInfo& actual);

bool command_value(std::string_view type_name, const Command& command, URK::Unity::Inspect::ValueInfo& value);
// Validate copied pointers through a short-lived GC handle before assignment.
bool reference_value_from_text(std::string_view type_name, const void* destination_type, std::string_view text,
	URK::Unity::Inspect::ValueInfo& value, URK::Unity::Inspect::ObjectHandle& rooted);
bool enum_value_from_text(std::string_view type_name, std::string_view text, URK::Unity::Inspect::ValueInfo& value);

std::uint64_t component_sample_token(int component_id, bool property, std::size_t index);
bool is_expected_empty_container_error(std::string_view message);

// Caller names collected from managed method compilation; several domains
// remember a method the first time they see it, and the flight recorder in
// explorer_model.cpp reads the same index back when publishing trace records.
void remember_managed_method(const URK::Unity::Inspect::MethodInfo& method);
// Indexes every declared method of a class straight from the metadata, without
// building the full signature description a MethodInfo carries. This is what
// the whole-domain caller index walks: it is an order of magnitude cheaper per
// method, and unlike Inspect::methods_from_class() it never drops a method
// whose parameter or return types cannot be described. Returns how many
// methods it recorded.
std::size_t remember_managed_class_methods(const URK::managed::Class* klass, std::string_view image = {});

struct ManagedMethodLocation {
	std::uintptr_t class_address = 0;
	std::string display;
	std::string image;
	std::string namespc;
	std::string class_name;
	std::string method_name;

	bool inspectable() const {
		return class_address != 0 && !class_name.empty() && !method_name.empty();
	}
};

ManagedMethodLocation managed_caller_method(std::uintptr_t address);
std::string managed_caller_location(std::uintptr_t address);
// Marks the caller index as covering every assembly, which is what lets an
// address with no entry be reported as native rather than as not yet indexed.
void mark_caller_index_complete();
// Names an address whose enclosing method is already known, instead of guessing
// from the nearest indexed entry below it.
std::string managed_method_location(std::uintptr_t function_start, std::uintptr_t address);
ManagedMethodLocation managed_method_details(std::uintptr_t function_start, std::uintptr_t address);

// Isolate broken managed references from the host callback.
//
// NOTE (real MSVC only -- not clang-cl, which also defines _MSC_VER for STL
// compatibility): cl.exe rejects a function containing __try/__except that
// constructs or returns a value of non-trivial destructor type -- not even
// transiently, and not even if the value is immediately discarded (verified
// directly against cl.exe: C2712 fires even for a fully-discarded call to a
// function returning std::string). clang does not enforce this and keeps
// the guard. ValueInfo is exactly such a type, and `read()` is an arbitrary
// caller-supplied callback (typically ReadProperty/ReadField/
// ReadArrayElement/InvokeMethod), so there is no way to keep __try here
// under real MSVC without also making every one of those call paths return
// trivial data through out-parameters first (as was done for ReadField and
// DescribeClass/TypeOf in unity_inspect.h). That is a larger, separate
// change; until it is done, this call is unguarded under real MSVC and
// relies on whatever __try exists further up the call stack (e.g. the guard
// around SetProperty/SetField in model_members.cpp, or
// process_command_guarded in explorer_model.cpp) to contain a fault raised
// while reading a value.
#if defined(_WIN32) && defined(_MSC_VER) && !defined(__clang__)
template <class Read>
URK::Unity::Inspect::ValueInfo guarded_managed_read(std::string_view, Read&& read) {
	return read();
}
#else
template <class Read>
URK::Unity::Inspect::ValueInfo guarded_managed_read(std::string_view type_name, Read&& read) {
#if defined(_WIN32)
	__try {
		return read();
	}
	__except (capture_native_fault(_exception_info())) {
		URK::Unity::detail::set_error("Managed member read raised a native access fault");
		return URK::Unity::Inspect::unavailable_value(std::string(type_name), "native access violation while reading value");
	}
#else
	return read();
#endif
}
#endif

} // namespace Explorer

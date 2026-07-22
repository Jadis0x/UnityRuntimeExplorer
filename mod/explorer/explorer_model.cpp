// Copyright (c) 2026 Jadis0x. All rights reserved.
#include "explorer_model.h"

#include "support/mod_log.h"
#include "ui/highlight.h"

#include "sdk/runtime_api.h"
#include "sdk/unity/unity_inspect.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <functional>
#include <limits>
#include <unordered_set>
#include <utility>

namespace Explorer {
	namespace {
		using namespace URK::Unity;

		constexpr auto kInspectorInterval = std::chrono::milliseconds(250);
		constexpr auto kHighlightInterval = std::chrono::milliseconds(66);
		constexpr auto kHighlightCameraRefreshInterval = std::chrono::seconds(1);
		constexpr auto kMemberValueInterval = std::chrono::milliseconds(500);
		constexpr auto kFieldWatchInterval = std::chrono::milliseconds(250);
		constexpr auto kTracePublishInterval = std::chrono::milliseconds(250);
		constexpr auto kEventRefreshDebounce = std::chrono::milliseconds(180);
		constexpr auto kCameraFocusTransition = std::chrono::milliseconds(320);
		constexpr int kMaxSceneCount = 128;
		constexpr int kMaxHierarchyDepth = 256;
		constexpr std::size_t kMaxCensusCandidates = 250000;
		constexpr std::size_t kArrayPageSize = 128;
		constexpr std::size_t kMaxHighlightRenderers = 48;
		constexpr int kHideAndDontSaveMask = 1 | 4 | 8 | 16 | 32;
		constexpr TypeRef kSceneType{ "", "UnityEngine.SceneManagement", "Scene" };

		float quaternion_dot(const Quaternion& left, const Quaternion& right) {
			return left.x * right.x + left.y * right.y + left.z * right.z + left.w * right.w;
		}

		Quaternion quaternion_normalize(Quaternion value) {
			const float length = std::sqrt(quaternion_dot(value, value));
			if (!std::isfinite(length) || length <= 0.000001f)
				return Quaternion{};
			const float inverse = 1.0f / length;
			value.x *= inverse;
			value.y *= inverse;
			value.z *= inverse;
			value.w *= inverse;
			return value;
		}

		Quaternion quaternion_slerp(Quaternion start, Quaternion end, float amount) {
			start = quaternion_normalize(start);
			end = quaternion_normalize(end);
			float cosine = quaternion_dot(start, end);
			if (cosine < 0.0f) {
				end.x = -end.x;
				end.y = -end.y;
				end.z = -end.z;
				end.w = -end.w;
				cosine = -cosine;
			}
			cosine = std::clamp(cosine, -1.0f, 1.0f);
			if (cosine > 0.9995f) {
				Quaternion result{
					start.x + amount * (end.x - start.x),
					start.y + amount * (end.y - start.y),
					start.z + amount * (end.z - start.z),
					start.w + amount * (end.w - start.w),
				};
				return quaternion_normalize(result);
			}
			const float angle = std::acos(cosine);
			const float sine = std::sin(angle);
			if (std::abs(sine) <= 0.000001f)
				return start;
			const float start_weight = std::sin((1.0f - amount) * angle) / sine;
			const float end_weight = std::sin(amount * angle) / sine;
			return quaternion_normalize(Quaternion{
				start.x * start_weight + end.x * end_weight,
				start.y * start_weight + end.y * end_weight,
				start.z * start_weight + end.z * end_weight,
				start.w * start_weight + end.w * end_weight,
			});
		}

#if defined(_WIN32)
		struct NativeFaultRecord {
			std::uint32_t code = 0;
			std::uintptr_t address = 0;
			std::uintptr_t instruction = 0;
		};
		thread_local NativeFaultRecord g_native_fault{};

		int capture_native_fault(void* raw_info) {
			auto* info = static_cast<EXCEPTION_POINTERS*>(raw_info);
			if (!info || !info->ExceptionRecord)
				return 0;
			const DWORD code = info->ExceptionRecord->ExceptionCode;
			if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_IN_PAGE_ERROR)
				return 0;
			g_native_fault.code = code;
			g_native_fault.instruction = reinterpret_cast<std::uintptr_t>(info->ExceptionRecord->ExceptionAddress);
			g_native_fault.address = info->ExceptionRecord->NumberParameters >= 2
				? static_cast<std::uintptr_t>(info->ExceptionRecord->ExceptionInformation[1])
				: 0;
			return 1;
		}
#endif

		struct FlatObject {
			GameObject object{};
			int instance_id = 0;
			int parent_id = 0;
			std::string name;
			bool active = false;
		};

		// Owns a temporary managed root while a queued GameObject command is running.
		// The root is transferred to RuntimeModel for a successful selection.
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

		bool is_transform_component(std::string_view name) {
			return name == "UnityEngine.Transform" || name == "UnityEngine.RectTransform" || name == "Transform" ||
				name == "RectTransform";
		}

		std::size_t count_nodes(const HierarchyNode& node) {
			std::size_t count = node.instance_id != 0 ? 1 : 0;
			for (const HierarchyNode& child : node.children)
				count += count_nodes(child);
			return count;
		}

		int boxed_scene_handle(Scene scene) {
			if (!scene || !scene.handle())
				return 0;

			// Read m_Handle directly; Scene methods require an unboxed value.
			clear_error();
			const int handle = Object{ scene.handle() }.GetField<int>("m_Handle");
			return last_error() ? 0 : handle;
		}

		std::string scene_name_from_handle(int handle) {
			if (handle == 0)
				return {};
			clear_error();
			return detail::InvokeStatic<std::string>(kSceneType, "GetNameInternal", handle);
		}

		bool is_hide_and_dont_save(GameObject object) {
			const int flags = object.hideFlags();
			return (flags & kHideAndDontSaveMask) == kHideAndDontSaveMask;
		}

		std::string normalized_type(std::string_view name) {
			return detail::normalized_type_name(name);
		}

		std::string pointer_text(void* pointer);

		bool command_value(std::string_view type_name, const Command& command, Inspect::ValueInfo& value) {
			const std::string type = normalized_type(type_name);
			value = {};
			value.type_name = std::string(type_name);
			if (type == "system.boolean") {
				value.kind = Inspect::ValueKind::Boolean;
				value.bool_value = command.bool_value;
				value.display = command.bool_value ? "true" : "false";
				return true;
			}
			if (type == "system.string") {
				value.kind = Inspect::ValueKind::String;
				value.display = command.text;
				return true;
			}
			const std::size_t component_count = Inspect::structured_component_count(type_name);
			if (component_count != 0) {
				value.kind = Inspect::ValueKind::Structured;
				value.component_count = component_count;
				const char* cursor = command.text.c_str();
				for (std::size_t index = 0; index < component_count; ++index) {
					while (*cursor != '\0' && (std::isspace(static_cast<unsigned char>(*cursor)) || *cursor == ',' ||
						*cursor == ';' || *cursor == '(' || *cursor == '['))
						++cursor;
					char* end = nullptr;
					errno = 0;
					value.components[index] = std::strtod(cursor, &end);
					if (errno != 0 || end == cursor)
						return false;
					cursor = end;
				}
				while (*cursor != '\0' &&
					(std::isspace(static_cast<unsigned char>(*cursor)) || *cursor == ')' || *cursor == ']'))
					++cursor;
				if (*cursor != '\0')
					return false;
				value.display = command.text;
				value.readable = true;
				return true;
			}
			char* end = nullptr;
			errno = 0;
			if (type == "system.single" || type == "system.double") {
				const double parsed = std::strtod(command.text.c_str(), &end);
				if (errno != 0 || end == command.text.c_str() || *end != '\0')
					return false;
				value.kind = Inspect::ValueKind::FloatingPoint;
				value.floating_value = parsed;
				value.display = command.text;
				return true;
			}
			const bool unsigned_type = type == "system.byte" || type == "system.uint16" || type == "system.uint32" ||
				type == "system.uint64" || type == "system.uintptr" || type == "system.char";
			const bool integral_type = unsigned_type || type == "system.sbyte" || type == "system.int16" ||
				type == "system.int32" || type == "system.int64" || type == "system.intptr";
			if (!integral_type)
				return false;
			if (unsigned_type) {
				const unsigned long long parsed = std::strtoull(command.text.c_str(), &end, 0);
				if (errno != 0 || end == command.text.c_str() || *end != '\0')
					return false;
				value.kind = Inspect::ValueKind::UnsignedInteger;
				value.unsigned_value = parsed;
			}
			else {
				const long long parsed = std::strtoll(command.text.c_str(), &end, 0);
				if (errno != 0 || end == command.text.c_str() || *end != '\0')
					return false;
				value.kind = Inspect::ValueKind::SignedInteger;
				value.signed_value = parsed;
			}
			value.display = command.text;
			return true;
		}

		// Validate copied pointers through a short-lived GC handle before assignment.
		bool reference_value_from_text(std::string_view type_name, const void* destination_type, std::string_view text,
			Inspect::ValueInfo& value, Inspect::ObjectHandle& rooted) {
			if (text == "null" || text == "NULL" || text == "0") {
				value = {};
				value.kind = Inspect::ValueKind::Null;
				value.type_name = std::string(type_name);
				value.display = "null";
				value.readable = true;
				return true;
			}
			std::string input(text);
			const std::size_t first = input.find_first_not_of(" \t\r\n");
			if (first == std::string::npos)
				return false;
			const std::size_t last = input.find_last_not_of(" \t\r\n");
			input = input.substr(first, last - first + 1);
			const Inspect::TypeInfo destination = Inspect::DescribeType(destination_type);
			if (input == "default" && destination_type && destination.is_value_type && !destination.is_enum) {
				std::uint32_t alignment = 0;
				const std::int32_t size = URK::il2cpp::class_value_size(destination.handle, &alignment);
				if (size <= 0)
					return false;
				std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size), 0);
				void* boxed = URK::il2cpp::value_box(static_cast<const URK::il2cpp::Class*>(destination.handle), bytes.data());
				rooted = Inspect::PinObject(Object{ boxed });
				const Object resolved = Inspect::ResolveObjectHandle(rooted);
				if (!rooted.handle || !resolved || !Inspect::IsBoxedValueOfType(resolved, destination_type)) {
					Inspect::FreeObjectHandle(rooted);
					return false;
				}
				value = {};
				value.kind = Inspect::ValueKind::ValueType;
				value.type_name = std::string(type_name);
				value.object = resolved.handle();
				value.display = "default(" + std::string(type_name) + ")";
				value.readable = true;
				return true;
			}
			const char* digits = input.c_str();
			if (input.size() > 2 && input[0] == '0' && (input[1] == 'x' || input[1] == 'X'))
				digits += 2;
			if (*digits == '\0')
				return false;
			char* end = nullptr;
			errno = 0;
			// Treat pointer input as hexadecimal so Copy Ptr values can be pasted directly.
			const unsigned long long address = std::strtoull(digits, &end, 16);
			if (!address || errno == ERANGE || end == digits || *end != '\0' ||
				address > static_cast<unsigned long long>(std::numeric_limits<std::uintptr_t>::max()))
				return false;
			bool valid = false;
#if defined(_WIN32)
			__try {
#endif
				rooted = Inspect::PinObject(Object{ reinterpret_cast<void*>(static_cast<std::uintptr_t>(address)) });
				const Object resolved = Inspect::ResolveObjectHandle(rooted);
				const bool destination_is_value_type = destination_type && destination.is_value_type;
				valid =
					rooted.handle && resolved &&
					(!destination_type || (destination_is_value_type ? Inspect::IsBoxedValueOfType(resolved, destination_type)
						: Inspect::IsAssignableTo(resolved, destination_type)));
				if (valid) {
					value = {};
					value.kind =
						destination_is_value_type ? Inspect::ValueKind::ValueType : Inspect::ValueKind::ObjectReference;
					value.type_name = std::string(type_name);
					value.object = resolved.handle();
					value.readable = true;
					value.display = pointer_text(resolved.handle());
				}
#if defined(_WIN32)
			}
			__except (capture_native_fault(_exception_info())) {
				detail::set_error("Reference conversion raised a native access fault");
				valid = false;
			}
#endif
			if (valid)
				return true;
			Inspect::FreeObjectHandle(rooted);
			return false;
		}

		bool enum_value_from_text(std::string_view type_name, std::string_view text, Inspect::ValueInfo& value) {
			std::string input(text);
			char* end = nullptr;
			errno = 0;
			const long long parsed = std::strtoll(input.c_str(), &end, 0);
			if (errno != 0 || end == input.c_str() || *end != '\0')
				return false;
			value = {};
			value.kind = Inspect::ValueKind::SignedInteger;
			value.type_name = std::string(type_name);
			value.signed_value = parsed;
			value.display = input;
			value.readable = true;
			return true;
		}

		// Isolate broken managed references from the host callback.
		template <class Read> Inspect::ValueInfo guarded_managed_read(std::string_view type_name, Read&& read) {
#if defined(_WIN32)
			__try {
				return read();
			}
			__except (capture_native_fault(_exception_info())) {
				detail::set_error("Managed member read raised a native access fault");
				return Inspect::unavailable_value(std::string(type_name), "native access violation while reading value");
			}
#else
			return read();
#endif
		}

		Inspect::TypeInfo safe_type_of(Object object);

		Inspect::ObjectHandle tracked_reference_handle(const Inspect::ValueInfo& value) {
			if (!value.object)
				return {};
			Inspect::ObjectHandle handle{};
#if defined(_WIN32)
			__try {
#endif
			const Object object{ value.object };
			const Inspect::TypeInfo type = safe_type_of(object);
			// Keep boxed values and returned strings alive until their Inspector tab closes.
			handle = type.is_value_type || value.kind == Inspect::ValueKind::String
				? Inspect::PinObject(object) : Inspect::WeakObject(object);
#if defined(_WIN32)
			}
			__except (capture_native_fault(_exception_info())) {
				detail::set_error("Tracked reference handle creation raised a native access fault");
				handle = {};
			}
#endif
			return handle;
		}

		bool values_equivalent(const Inspect::ValueInfo& expected, const Inspect::ValueInfo& actual) {
			using Inspect::ValueKind;
			if (!actual.readable)
				return false;
			if (expected.kind == ValueKind::Boolean)
				return actual.kind == ValueKind::Boolean && expected.bool_value == actual.bool_value;
			if (expected.kind == ValueKind::String)
				return actual.kind == ValueKind::String && expected.display == actual.display;
			if (expected.kind == ValueKind::FloatingPoint)
				return actual.kind == ValueKind::FloatingPoint && std::abs(expected.floating_value - actual.floating_value) <=
				std::max(1e-6, std::abs(expected.floating_value) * 1e-6);
			if (expected.kind == ValueKind::SignedInteger || expected.kind == ValueKind::Enum)
				return (actual.kind == ValueKind::SignedInteger || actual.kind == ValueKind::Enum) &&
				expected.signed_value == actual.signed_value;
			if (expected.kind == ValueKind::UnsignedInteger)
				return (actual.kind == ValueKind::UnsignedInteger || actual.kind == ValueKind::Enum) &&
				expected.unsigned_value == actual.unsigned_value;
			if (expected.kind == ValueKind::Null)
				return actual.kind == ValueKind::Null ||
				((actual.kind == ValueKind::ObjectReference || actual.kind == ValueKind::ArrayReference) &&
					!actual.object);
			if (expected.kind == ValueKind::ObjectReference || expected.kind == ValueKind::ArrayReference)
				return actual.object == expected.object;
			if (expected.kind == ValueKind::Structured && actual.kind == ValueKind::Structured &&
				expected.component_count == actual.component_count) {
				for (std::size_t index = 0; index < expected.component_count; ++index) {
					if (std::abs(expected.components[index] - actual.components[index]) > 0.0001)
						return false;
				}
				return true;
			}
			return expected.display == actual.display;
		}

		std::string watched_value_display(const Inspect::ValueInfo& value) {
			if (value.readable)
				return value.display.empty() ? "<empty>" : value.display;
			if (value.display.empty() || value.display == "Not sampled")
				return "<not available>";
			return "<unavailable: " + value.display + ">";
		}

		std::uint64_t component_sample_token(int component_id, bool property, std::size_t index) {
			return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(component_id)) << 32) |
				(property ? 0x80000000ull : 0ull) | static_cast<std::uint64_t>(index & 0x0fffffffu);
		}

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

		bool is_expected_empty_container_error(std::string_view message) {
			return message.find("System.InvalidOperationException: Map is empty") != std::string_view::npos ||
				message.find("Map is empty") != std::string_view::npos;
		}

		std::uint64_t member_reference_token(int component_id, bool property, std::size_t index) {
			return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(component_id)) << 32) |
				(property ? 0x80000000ull : 0ull) | static_cast<std::uint64_t>(index & 0x0fffffffu);
		}

		std::string pointer_text(void* pointer) {
			char text[32]{};
			std::snprintf(text, sizeof(text), "%p", pointer);
			return text;
		}

		bool readable_address(std::uintptr_t address) {
			if (address < 0x10000)
				return false;
			MEMORY_BASIC_INFORMATION memory{};
			if (VirtualQuery(reinterpret_cast<const void*>(address), &memory, sizeof(memory)) != sizeof(memory))
				return false;
			return memory.State == MEM_COMMIT && (memory.Protect & (PAGE_NOACCESS | PAGE_GUARD)) == 0;
		}

		bool copy_readable_c_string(const char* value, std::string& output) {
			output.clear();
			if (!value)
				return true;
			constexpr std::size_t kMaxMetadataString = 512;
			for (std::size_t index = 0; index < kMaxMetadataString; ++index) {
				if (!readable_address(reinterpret_cast<std::uintptr_t>(value + index)))
					return false;
				if (value[index] == '\0') {
					output.assign(value, index);
					return true;
				}
			}
			return false;
		}

		bool safe_class_display_name(const void* klass, std::string& output) {
			output.clear();
			if (!klass || !readable_address(reinterpret_cast<std::uintptr_t>(klass)))
				return false;
#if defined(_WIN32)
			__try {
#endif
			const char* namespc = URK::il2cpp::class_get_namespace(
				static_cast<const URK::il2cpp::Class*>(klass));
			const char* name = URK::il2cpp::class_get_name(
				static_cast<const URK::il2cpp::Class*>(klass));
			std::string namespace_text;
			std::string name_text;
			if (!copy_readable_c_string(namespc, namespace_text) ||
				!copy_readable_c_string(name, name_text) || name_text.empty())
				return false;
			output = namespace_text.empty() ? std::move(name_text)
				: namespace_text + "." + name_text;
			return true;
#if defined(_WIN32)
			} __except (capture_native_fault(_exception_info())) {
				clear_error();
				output.clear();
				return false;
			}
#endif
		}

		// Destroyed Unity wrappers can keep a valid address and instance ID.
		// op_Implicit is the reliable lifetime check on supported IL2CPP builds.
		bool safe_object_alive(Object object) {
			if (!object || !readable_address(reinterpret_cast<std::uintptr_t>(object.handle())))
				return false;
#if defined(_WIN32)
			__try {
				if (object.GetInstanceID() == 0)
					return false;
				clear_error();
				const bool alive = object.alive();
				if (const char* error = last_error(); error && error[0]) {
					clear_error();
					return false;
				}
				return alive;
			}
			__except (capture_native_fault(_exception_info())) {
				clear_error();
				return false;
			}
#else
			return object.GetInstanceID() != 0;
#endif
		}

		std::string safe_runtime_class_name(Object object) {
			if (!safe_object_alive(object))
				return {};
#if defined(_WIN32)
			__try {
				clear_error();
				const std::string name = object.runtime_class_name();
				if (const char* error = last_error(); error && error[0])
					clear_error();
				return name;
			}
			__except (capture_native_fault(_exception_info())) {
				clear_error();
				return {};
			}
#else
		return object.runtime_class_name();
#endif
		}

		Inspect::TypeInfo safe_type_of(Object object) {
			if (!object || !readable_address(reinterpret_cast<std::uintptr_t>(object.handle())))
				return {};
#if defined(_WIN32)
			__try {
				clear_error();
				return Inspect::TypeOf(object);
			}
			__except (capture_native_fault(_exception_info())) {
				clear_error();
				return {};
			}
#else
			return Inspect::TypeOf(object);
#endif
		}

		std::string module_location(std::uintptr_t address) {
			if (!address)
				return "<unknown caller>";
			HMODULE module = nullptr;
			if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				reinterpret_cast<LPCSTR>(address), &module) ||
				!module) {
				return pointer_text(reinterpret_cast<void*>(address));
			}
			char path[MAX_PATH]{};
			GetModuleFileNameA(module, path, static_cast<DWORD>(sizeof(path)));
			const char* name = std::strrchr(path, '\\');
			name = name ? name + 1 : path;
			char text[320]{};
			std::snprintf(text, sizeof(text), "%s+0x%llX", name[0] ? name : "<module>",
				static_cast<unsigned long long>(address - reinterpret_cast<std::uintptr_t>(module)));
			return text;
		}

		struct ManagedCallerIndex {
			std::unordered_map<std::uintptr_t, std::string> methods;
		};

		ManagedCallerIndex& managed_caller_index() {
			static ManagedCallerIndex index;
			return index;
		}


		void remember_managed_method(const Inspect::MethodInfo& method) {
			if (!method.handle || method.name.empty())
				return;
#if defined(_WIN32)
			__try {
#endif
				void* const pointer = URK::il2cpp::method_pointer(static_cast<const URK::il2cpp::Method*>(method.handle));
				if (!pointer)
					return;
				const std::string name = method.declaring_type.full_name.empty()
					? method.name
					: method.declaring_type.full_name + "." + method.name;
				auto& methods = managed_caller_index().methods;
				const auto [found, inserted] = methods.emplace(reinterpret_cast<std::uintptr_t>(pointer), name);
				if (!inserted && found->second != name)
					found->second = "<shared IL2CPP generic code>";
#if defined(_WIN32)
			}
			__except (capture_native_fault(_exception_info())) {
				// Caller names are optional. Do not fall back to a domain-wide scan here.
			}
#endif
		}

		std::string managed_caller_location(std::uintptr_t address) {
			DWORD64 image_base = 0;
			const PRUNTIME_FUNCTION function = RtlLookupFunctionEntry(static_cast<DWORD64>(address), &image_base, nullptr);
			if (!function)
				return module_location(address);
			const std::uintptr_t function_start = static_cast<std::uintptr_t>(image_base + function->BeginAddress);
			const auto found = managed_caller_index().methods.find(function_start);
			if (found == managed_caller_index().methods.end())
				return module_location(address);
			return found->second;
		}

		std::string describe_traced_reference(std::uint64_t raw, std::string_view declared_type) {
			const std::uintptr_t address = static_cast<std::uintptr_t>(raw);
			if (!address)
				return "null";
			const std::string fallback = std::string(declared_type.empty() ? "object" : declared_type) + " @ " +
				pointer_text(reinterpret_cast<void*>(address));
			if (!readable_address(address))
				return fallback + " (unavailable)";
			// Trace records are raw register values, not guaranteed managed objects.
			return fallback;
		}

		std::string describe_traced_string(std::uint64_t raw) {
			const std::uintptr_t address = static_cast<std::uintptr_t>(raw);
			const std::string fallback = "System.String @ " + pointer_text(reinterpret_cast<void*>(address));
			if (!address)
				return "null";
			if (!readable_address(address))
				return fallback + " (no longer readable)";

			// Decode strings on the Explorer thread, inside the normal native-fault guard.
#if defined(_WIN32)
			__try {
#endif
				auto* value = reinterpret_cast<URK::il2cpp::String*>(address);
				const std::int32_t length = URK::il2cpp::string_length(value);
				if (length < 0 || length > 4096)
					return fallback + " (invalid length)";
				std::vector<char> utf8(static_cast<std::size_t>(length) * 4u + 1u, '\0');
				if (!URK::il2cpp::string_to_utf8(value, utf8.data(), utf8.size()))
					return fallback + " (could not decode)";
				return "\"" + std::string(utf8.data()) + "\"";
#if defined(_WIN32)
			}
			__except (capture_native_fault(_exception_info())) {
				clear_error();
				return fallback + " (unavailable)";
			}
#endif
		}

		std::string assembly_name(const Inspect::TypeInfo& type) {
			if (!type.handle)
				return {};
			const char* name = URK::il2cpp::class_get_assemblyname(type.handle);
			return name ? name : "";
		}
	} // namespace

	struct RuntimeModel::HierarchyCensus {
		HierarchyInfo next;
		std::uint64_t scene_generation = 0;
		std::unordered_map<int, std::size_t> loaded_scene_indices;
		std::size_t ddol_index = 0;
		std::size_t hidden_index = 0;
		detail::RootedObjectArray<GameObject> candidates;
		std::size_t candidate_count = 0;
		std::size_t candidate_index = 0;
		std::vector<FlatObject> flat;
		std::unordered_map<int, std::size_t> flat_indices;
		Clock::time_point started{};
		std::chrono::microseconds max_slice{};
	};

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
		working_.status = "IL2CPP runtime ready";
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
			// An invalid managed pointer may also make gchandle_free unsafe.  Drop
			// the native copies first, then wait for the current Unity transition
			// to settle before asking IL2CPP for a new hierarchy.
			discard_managed_state_after_native_fault();
			{
				std::lock_guard<std::mutex> lock(command_mutex_);
				// Preserve navigation/lifecycle input received after the fault.
				// These commands re-resolve their targets and do not depend on the
				// discarded reflection pointers.
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
			record_flight("FAULT", "Native IL2CPP access", "code=" + std::to_string(fault_code));
			set_status("Explorer isolated a native IL2CPP access fault; stale managed state was released and a census retry is pending.");
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

		const Clock::time_point now = Clock::now();
		update_camera_transition(now);
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
		// Watches continue sampling while Live Data is paused.
		if (has_active_field_watches() && !event_refresh_pending_ && now >= next_field_watch_refresh_) {
			refresh_field_watches();
			next_field_watch_refresh_ = now + kFieldWatchInterval;
			publish();
		}
		if (MethodTracer::any_active() && now >= next_trace_publish_) {
			next_trace_publish_ = now + kTracePublishInterval;
			publish();
		}
	}

	void RuntimeModel::stop() {
		MethodTracer::stop_all();
		hierarchy_census_.reset();
		clear_selection();
		ModUI::Highlight::enqueue_clear();
		hierarchy_instance_ids_.clear();
		clear_component_cache();
		clear_object_inspector();
		for (auto& [_, handle] : class_browser_handles_)
			Inspect::FreeObjectHandle(handle);
		class_browser_handles_.clear();
		for (auto& [_, handle] : class_browser_static_handles_)
			Inspect::FreeObjectHandle(handle);
		class_browser_static_handles_.clear();
		clear_highlight_renderer_cache();
		clear_highlight_camera_cache();
		highlight_enabled_ = true;
		highlight_max_distance_ = 0.0f;
		camera_focus_distance_ = 8.0f;
		camera_focus_tilt_ = 3.0f;
		camera_focus_top_down_ = false;
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
				// A malformed member record belongs to one component. Keep the
				// rooted GameObject, hierarchy, and unrelated inspector state
				// intact, and expose an explicit retry instead of converting a
				// reflection failure into a workspace-wide recovery.
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
						g_native_fault.code, reinterpret_cast<void*>(g_native_fault.address),
						reinterpret_cast<void*>(g_native_fault.instruction));
					active_metadata_stage_.clear();
					publish_recovery_snapshot();
					continue;
				}
				clear_error();
				set_status("Explorer blocked a native access violation while processing a command.");
				ModLog::warn("Explorer command kind=%d sequence=%llu blocked a native access violation: code=0x%08X address=%p instruction=%p",
					static_cast<int>(command.kind), static_cast<unsigned long long>(command.sequence),
					g_native_fault.code, reinterpret_cast<void*>(g_native_fault.address),
					reinterpret_cast<void*>(g_native_fault.instruction));
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
				// Do not continue this tick: publishing, refreshing, or freeing a
				// stale handle can immediately repeat the same fault. The next
				// tick uses the native-only recovery path.
				notify_native_fault(g_native_fault.code, g_native_fault.address, g_native_fault.instruction);
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
		if (!lifecycle_command && command.scene_generation != 0 && command.scene_generation != scene_generation_) {
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
			command.kind == CommandKind::LoadClassBrowserMembers || command.kind == CommandKind::DeleteComponent ||
			command.kind == CommandKind::SetComponentEnabled || command.kind == CommandKind::SetFieldValue ||
			command.kind == CommandKind::SetPropertyValue || command.kind == CommandKind::SampleMemberValue ||
			command.kind == CommandKind::SetArrayPage || command.kind == CommandKind::InvokeMethod ||
			command.kind == CommandKind::SetMethodTrace || command.kind == CommandKind::ClearMethodTrace ||
			command.kind == CommandKind::CloseMethodTrace || command.kind == CommandKind::SetFieldWatch ||
			command.kind == CommandKind::ClearFieldWatch || command.kind == CommandKind::CloseFieldWatch ||
			command.kind == CommandKind::InspectReference || command.kind == CommandKind::InspectRawReference ||
			command.kind == CommandKind::CloseObjectInspectorTab ||
			command.kind == CommandKind::SetLiveData ||
			command.kind == CommandKind::SetHighlightDistance ||
			command.kind == CommandKind::SetHighlightEnabled ||
			command.kind == CommandKind::SetCameraFocusDistance ||
			command.kind == CommandKind::SetCameraFocusTopDown ||
			command.kind == CommandKind::SetCameraFocusTilt;
		const bool event_command = command.kind == CommandKind::ObjectDestroyRequested;
		ScopedObjectRoot command_root;
		GameObject object{};
		const bool needs_object = !component_command && !event_command && command.kind != CommandKind::Refresh &&
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
			!event_command) {
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
			// A hierarchy double-click is both a selection and a focus action.
			// Keeping the old Inspector selection made the highlight point at one
			// object while the camera followed another.
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
		case CommandKind::SetFieldWatch:
			set_field_watch(command);
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
			camera_focus_distance_ = std::clamp(command.float_value, 1.0f, 100.0f);
			// Apply zoom along the active focus direction.
			const float offset_length = camera_focus_offset.magnitude();
			if (camera_focus_active_) {
				if (camera_focus_top_down_)
					camera_focus_offset = camera_focus_heading * camera_focus_tilt_ +
						Vector3{ 0.0f, camera_focus_distance_, 0.0f };
				else if (std::isfinite(offset_length) && offset_length > 0.001f)
					camera_focus_offset = camera_focus_offset / offset_length * camera_focus_distance_;
				camera_transition_active_ = false;
				// Validate both rooted objects before changing a transform.
				update_camera_transition(Clock::now());
			}
			set_status(std::string(camera_focus_top_down_ ? "Camera focus height set to " : "Camera focus distance set to ") +
				std::to_string(camera_focus_distance_) + " units");
			publish();
			return;
		}
		case CommandKind::SetCameraFocusTopDown:
			camera_focus_top_down_ = command.bool_value;
			if (camera_focus_active_) {
				if (camera_focus_top_down_)
					camera_focus_offset = camera_focus_heading * camera_focus_tilt_ +
						Vector3{ 0.0f, camera_focus_distance_, 0.0f };
				else
					camera_focus_offset = camera_focus_view_direction * camera_focus_distance_;
				camera_transition_active_ = false;
				update_camera_transition(Clock::now());
			}
			set_status(camera_focus_top_down_ ? "Camera focus changed to top-down" : "Camera focus now preserves the game view angle");
			publish();
			return;
		case CommandKind::SetCameraFocusTilt:
			camera_focus_tilt_ = std::clamp(command.float_value, 0.0f, 100.0f);
			if (camera_focus_active_ && camera_focus_top_down_) {
				camera_focus_offset = camera_focus_heading * camera_focus_tilt_ +
					Vector3{ 0.0f, camera_focus_distance_, 0.0f };
				camera_transition_active_ = false;
				update_camera_transition(Clock::now());
			}
			set_status("Camera top-down tilt set to " + std::to_string(camera_focus_tilt_) + " units");
			publish();
			return;
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
			ModLog::info("Scene generation advanced: generation=%llu handle=%d name=%s",
				static_cast<unsigned long long>(scene_generation_), command.int_value,
				command.text.empty() ? "<unknown>" : command.text.c_str());
			active_scene_handle_hint_ = command.int_value;
			active_scene_name_hint_ = command.text;
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
		// A selection already validates and snapshots the GameObject.  Defer the
		// optional global camera/renderer census to a later refresh: it is the
		// broadest Unity traversal in this path and must not make a valid
		// hierarchy selection fail on games that destroy objects mid-frame.
		if (command.kind != CommandKind::Select)
			update_highlight();
		publish();
	}

	bool RuntimeModel::refresh_hierarchy() {
		const bool starting = !hierarchy_census_;
		if (starting)
			hierarchy_census_ = std::make_unique<HierarchyCensus>();
		HierarchyCensus& state = *hierarchy_census_;
		HierarchyInfo& next = state.next;

		if (starting) {
			state.started = Clock::now();
			state.scene_generation = scene_generation_;
			next.revision = next_hierarchy_revision_++;
			next.scene_generation = scene_generation_;
			next.source = "Event-driven Resources census / native adjacency / frame-budgeted";
			hierarchy_instance_ids_.clear();

			const Scene active_scene = SceneManager::GetActiveScene();
			int active_handle = boxed_scene_handle(active_scene);
			if (active_handle == 0)
				active_handle = active_scene_handle_hint_;

			const int scene_count = std::clamp(SceneManager::sceneCount(), 0, kMaxSceneCount);
			for (int index = 0; index < scene_count; ++index) {
				const Scene scene = SceneManager::GetSceneAt(index);
				const int handle = boxed_scene_handle(scene);
				if (handle == 0 || state.loaded_scene_indices.contains(handle))
					continue;
				SceneNode node{};
				node.handle = handle;
				node.name = scene_name_from_handle(handle);
				if (node.name.empty() && handle == active_scene_handle_hint_)
					node.name = active_scene_name_hint_;
				if (node.name.empty())
					node.name = "Scene " + std::to_string(handle);
				node.active = handle == active_handle;
				state.loaded_scene_indices.emplace(handle, next.scenes.size());
				next.scenes.push_back(std::move(node));
			}

			if (active_handle != 0 && !state.loaded_scene_indices.contains(active_handle)) {
				SceneNode node{};
				node.handle = active_handle;
				node.name = scene_name_from_handle(active_handle);
				if (node.name.empty())
					node.name = active_scene_name_hint_;
				if (node.name.empty())
					node.name = "Active Scene";
				node.active = true;
				next.scenes.insert(next.scenes.begin(), std::move(node));
				state.loaded_scene_indices.clear();
				for (std::size_t index = 0; index < next.scenes.size(); ++index)
					state.loaded_scene_indices[next.scenes[index].handle] = index;
			}

			state.ddol_index = next.scenes.size();
			SceneNode ddol{};
			ddol.name = "DontDestroyOnLoad";
			ddol.dont_destroy_on_load = true;
			next.scenes.push_back(std::move(ddol));

			state.hidden_index = next.scenes.size();
			SceneNode hidden{};
			hidden.name = "HideAndDontSave";
			hidden.hide_and_dont_save = true;
			next.scenes.push_back(std::move(hidden));

			state.candidates = Object::FindObjectsOfTypeAllRooted<GameObject>();
			state.candidate_count = std::min(state.candidates.size(), kMaxCensusCandidates);
			ModLog::info("Hierarchy census started: generation=%llu candidates=%zu budget_ms=2",
				static_cast<unsigned long long>(state.scene_generation), state.candidate_count);
			if (state.candidates.size() > kMaxCensusCandidates)
				next.source += " (candidate cap reached)";
			state.flat.reserve(state.candidate_count);
			state.flat_indices.reserve(state.candidate_count);
		}

		if (state.scene_generation != scene_generation_) {
			hierarchy_census_.reset();
			request_refresh();
			return false;
		}

		auto& flat = state.flat;
		auto& flat_indices = state.flat_indices;
		const Clock::time_point slice_started = Clock::now();
		const Clock::time_point deadline = slice_started + std::chrono::milliseconds(2);
		std::size_t processed_this_slice = 0;

		// Build parent-child relations natively after one managed census.
		for (; state.candidate_index < state.candidate_count; ++state.candidate_index) {
			if (processed_this_slice >= 64 && Clock::now() >= deadline) {
				state.max_slice = std::max(state.max_slice,
					std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - slice_started));
				return false;
			}
			++processed_this_slice;
			const GameObject& object = state.candidates[state.candidate_index];
			if (!object)
				continue;
			const int instance_id = object.GetInstanceID();
			if (instance_id == 0 || flat_indices.contains(instance_id))
				continue;
			const Transform transform = object.transform();
			if (!transform)
				continue;
			const Transform parent = transform.parent();
			int parent_id = 0;
			if (parent) {
				const GameObject parent_object = parent.gameObject();
				if (parent_object)
					parent_id = parent_object.GetInstanceID();
			}

			FlatObject record{};
			record.object = object;
			record.instance_id = instance_id;
			record.parent_id = parent_id;
			record.name = object.name();
			if (record.name.empty())
				record.name = "<unnamed>";
			record.active = object.activeSelf();
			flat_indices.emplace(instance_id, flat.size());
			flat.push_back(std::move(record));
		}
		state.max_slice = std::max(state.max_slice,
			std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - slice_started));

		// Wait for the complete, budgeted census before rebuilding the hierarchy.
		// Rebuilding partial results duplicates roots in busy scenes.
		if (state.candidate_index < state.candidate_count)
			return false;

		const std::size_t ddol_index = state.ddol_index;
		const std::size_t hidden_index = state.hidden_index;
		auto& loaded_scene_indices = state.loaded_scene_indices;

		std::vector<std::vector<std::size_t>> children(flat.size());
		std::vector<std::size_t> root_indices;
		root_indices.reserve(flat.size());
		for (std::size_t index = 0; index < flat.size(); ++index) {
			const auto parent = flat_indices.find(flat[index].parent_id);
			if (flat[index].parent_id != 0 && parent != flat_indices.end())
				children[parent->second].push_back(index);
			else
				root_indices.push_back(index);
		}

		std::unordered_set<int> visited;
		visited.reserve(flat.size());
		std::function<HierarchyNode(std::size_t, int)> build_node = [&](std::size_t index, int depth) -> HierarchyNode {
			if (index >= flat.size() || depth > kMaxHierarchyDepth)
				return {};
			const FlatObject& source = flat[index];
			if (!visited.insert(source.instance_id).second)
				return {};
			hierarchy_instance_ids_.insert(source.instance_id);
			HierarchyNode node{};
			node.instance_id = source.instance_id;
			node.object_address = reinterpret_cast<std::uintptr_t>(source.object.handle());
			node.name = source.name;
			node.pointer_text = pointer_text(source.object.handle());
			node.active = source.active;
			node.children.reserve(children[index].size());
			for (const std::size_t child_index : children[index]) {
				HierarchyNode child = build_node(child_index, depth + 1);
				if (child.instance_id != 0)
					node.children.push_back(std::move(child));
			}
			return node;
			};

		for (const std::size_t root_index : root_indices) {
			const FlatObject& root = flat[root_index];
			std::size_t group_index = ddol_index;
			if (is_hide_and_dont_save(root.object)) {
				group_index = hidden_index;
			}
			else {
				const int scene_handle = boxed_scene_handle(root.object.scene());
				if (scene_handle == 0)
					continue; // prefab/asset GameObject, not a live hierarchy object
				if (const auto loaded = loaded_scene_indices.find(scene_handle); loaded != loaded_scene_indices.end()) {
					group_index = loaded->second;
				}
			}

			HierarchyNode node = build_node(root_index, 0);
			if (node.instance_id != 0)
				next.scenes[group_index].roots.push_back(std::move(node));
		}

		for (const SceneNode& scene : next.scenes) {
			next.roots += scene.roots.size();
			for (const HierarchyNode& root : scene.roots)
				next.objects += count_nodes(root);
		}

		if (next.objects == 0) {
			const char* error = last_error();
			set_status(error && error[0] ? std::string("Hierarchy discovery failed: ") + error
				: "No live scene GameObjects were discovered");
		}

		const std::string signature = next.source + "|" + std::to_string(next.scenes.size()) + "|" +
			std::to_string(next.roots) + "|" + std::to_string(next.objects);
		if (signature != logged_hierarchy_signature_) {
			logged_hierarchy_signature_ = signature;
			ModLog::info("hierarchy refreshed: source=%s scenes=%zu roots=%zu objects=%zu", next.source.c_str(),
				next.scenes.size(), next.roots, next.objects);
		}

		const auto census_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - state.started);
		ModLog::info("hierarchy census timing: candidates=%zu total_ms=%lld max_slice_us=%lld",
			state.candidate_count, static_cast<long long>(census_elapsed.count()),
			static_cast<long long>(state.max_slice.count()));
		hierarchy_ = std::make_shared<const HierarchyInfo>(std::move(next));
		working_.hierarchy = hierarchy_;

		if (working_.selected_instance_id != 0) {
			// Do not replace the rooted selection with the hierarchy's raw wrapper.
			// The census array no longer roots those wrappers after this function
			// returns, which made high-churn games reuse freed managed addresses.
			if (!hierarchy_instance_ids_.contains(working_.selected_instance_id) || !resolve_selected_object())
				clear_selection();
		}

		hierarchy_census_.reset();
		return true;
	}

	void RuntimeModel::refresh_inspector(bool include_components) {
		selected_ = resolve_selected_object();
		if (!safe_object_alive(selected_)) {
			if (working_.selected_instance_id != 0)
				clear_selection();
			return;
		}

		InspectorInfo info = working_.inspector;
		const int instance_id = selected_.GetInstanceID();
		if (!info.valid || info.instance_id != instance_id)
			include_components = true;

		info.valid = true;
		info.instance_id = instance_id;
		info.pointer_text = pointer_text(selected_.handle());
		info.active = selected_.activeSelf();

		if (include_components) {
			info.name = selected_.name();
			info.tag = selected_.tag();
			info.layer = selected_.GetProperty<int>("layer");
			info.is_static = selected_.GetProperty<bool>("isStatic");
			const Inspect::TypeInfo object_type = safe_type_of(Object{ selected_.handle() });
			info.type_name = object_type.full_name.empty() ? "UnityEngine.GameObject" : object_type.full_name;
			info.assembly_name = assembly_name(object_type);
			info.namespace_name = object_type.namespc;
			info.class_name = object_type.name;
			info.components.clear();
			clear_component_cache();
			const auto components = selected_.GetComponentsRooted<Component>();
			for (const Component& candidate : components) {
				if (!candidate)
					continue;
				Inspect::ObjectHandle handle = Inspect::PinObject(Object{ candidate.handle() });
				const Object rooted = Inspect::ResolveObjectHandle(handle);
				const Component component{ rooted.handle() };
				if (!handle.handle || !safe_object_alive(component)) {
					Inspect::FreeObjectHandle(handle);
					continue;
				}
				ComponentInfo component_info{};
				component_info.instance_id = component.GetInstanceID();
				if (component_info.instance_id == 0) {
					Inspect::FreeObjectHandle(handle);
					continue;
				}
				const Inspect::TypeInfo component_type = safe_type_of(Object{ component.handle() });
				component_info.type_name = component_type.full_name;
				component_info.assembly_name = assembly_name(component_type);
				component_info.namespace_name = component_type.namespc;
				component_info.class_name = component_type.name;
				component_info.pointer_text = pointer_text(component.handle());
				if (component_info.type_name.empty())
					component_info.type_name = safe_runtime_class_name(component);
				if (component_info.type_name.empty())
					component_info.type_name = "<missing component>";
				if (!is_transform_component(component_info.type_name)) {
					clear_error();
					bool enabled = false;
#if defined(_WIN32)
					__try {
						enabled = component.GetProperty<bool>("enabled");
					}
					__except (capture_native_fault(_exception_info())) {
						// Record native faults from malformed third-party metadata.
						clear_error();
						ModLog::warn("Component enabled probe blocked a native access violation: id=%d type=%s",
							component_info.instance_id, component_info.type_name.c_str());
					}
#else
					enabled = component.GetProperty<bool>("enabled");
#endif
					if (!last_error()) {
						component_info.enabled_supported = true;
						component_info.enabled = enabled;
					}
					clear_error();
					component_handles_[component_info.instance_id] = handle;
					info.components.push_back(std::move(component_info));
				}
				else {
					Inspect::FreeObjectHandle(handle);
				}
			}
			// Prefer renderers nearest to the selected transform.
			struct HighlightRendererCandidate {
				Renderer renderer;
				float distance_squared = 0.0f;
			};
			std::vector<HighlightRendererCandidate> candidates;
			candidates.reserve(kMaxHighlightRenderers);
			const Transform highlight_transform = selected_.transform();
			const Vector3 highlight_position = highlight_transform ? highlight_transform.position() : Vector3{};
			const auto renderers = selected_.GetComponentsInChildrenRooted<Renderer>(true);
			for (const Renderer& renderer : renderers) {
				if (!safe_object_alive(renderer))
					continue;
				const Transform renderer_transform = renderer.transform();
				const Vector3 renderer_position = renderer_transform ? renderer_transform.position() : highlight_position;
				const float dx = renderer_position.x - highlight_position.x;
				const float dy = renderer_position.y - highlight_position.y;
				const float dz = renderer_position.z - highlight_position.z;
				const HighlightRendererCandidate candidate{ renderer, dx * dx + dy * dy + dz * dz };
				if (candidates.size() < kMaxHighlightRenderers) {
					candidates.push_back(candidate);
					continue;
				}
				const auto farthest = std::max_element(candidates.begin(), candidates.end(),
					[](const HighlightRendererCandidate& left, const HighlightRendererCandidate& right) {
						return left.distance_squared < right.distance_squared;
					});
				if (candidate.distance_squared < farthest->distance_squared)
					*farthest = candidate;
			}
			std::sort(candidates.begin(), candidates.end(),
				[](const HighlightRendererCandidate& left, const HighlightRendererCandidate& right) {
					return left.distance_squared < right.distance_squared;
				});
			clear_highlight_renderer_cache();
			highlight_renderers_.reserve(candidates.size());
			for (const HighlightRendererCandidate& candidate : candidates) {
				Inspect::ObjectHandle handle = Inspect::WeakObject(Object{ candidate.renderer.handle() });
				if (handle.handle)
					highlight_renderers_.push_back(handle);
			}
		}

		const Transform transform = selected_.transform();
		info.camera_distance_valid = false;
		if (transform) {
			info.local_position = transform.localPosition();
			info.local_rotation = transform.GetProperty<Vector3>("localEulerAngles");
			info.local_scale = transform.localScale();
			Camera distance_camera = Camera::main();
			if (!safe_object_alive(distance_camera) || !distance_camera.enabled())
				distance_camera = Camera::current();
			const Transform camera_transform = distance_camera ? distance_camera.transform() : Transform{};
			if (safe_object_alive(camera_transform)) {
				const float distance = Vector3::distance(transform.position(), camera_transform.position());
				if (std::isfinite(distance)) {
					info.camera_distance = distance;
					info.camera_distance_valid = true;
				}
			}
		}
		working_.inspector = std::move(info);
	}

	void RuntimeModel::clear_component_cache() {
		for (auto& [_, handle] : component_handles_)
			if (handle.handle) {
				Inspect::FreeObjectHandle(handle);
			}
		component_handles_.clear();
		for (auto& [_, handle] : reference_handles_)
			if (handle.handle) {
				Inspect::FreeObjectHandle(handle);
			}
		reference_handles_.clear();
		working_.method_results.clear();
		component_reflection_.clear();
		sampled_component_members_.clear();
		// Discard watches when their selected component changes.
		for (auto& [_, state] : field_watches_)
			release_field_watch_references(state.snapshot);
		field_watches_.clear();
	}

	void RuntimeModel::clear_object_inspector() {
		if (object_inspector_handle_.handle) {
			Inspect::FreeObjectHandle(object_inspector_handle_);
		}
		object_inspector_handle_ = {};
		for (auto& [_, handle] : object_inspector_history_)
			Inspect::FreeObjectHandle(handle);
		object_inspector_history_.clear();
		object_inspector_reflection_ = {};
		sampled_object_fields_.clear();
		sampled_object_properties_.clear();
		for (auto it = reference_handles_.begin(); it != reference_handles_.end();) {
			const std::uint64_t scope = it->first & 0xF000000000000000ull;
			if (scope != 0x3000000000000000ull && scope != 0x4000000000000000ull && scope != 0x5000000000000000ull) {
				++it;
				continue;
			}
			if (it->second.handle)
				Inspect::FreeObjectHandle(it->second);
			it = reference_handles_.erase(it);
		}
		for (auto it = working_.method_results.begin(); it != working_.method_results.end();) {
			if (it->second.object_inspector_token == 0) {
				++it;
				continue;
			}
			if (it->second.reference.token != 0)
				release_reference_handle(it->second.reference.token);
			it = working_.method_results.erase(it);
		}
		clear_locked_members(true);
		working_.object_inspector = {};
	}

	void RuntimeModel::close_object_inspector_tab(std::uint64_t token) {
		if (token == 0)
			return;

		if (const auto history = object_inspector_history_.find(token); history != object_inspector_history_.end()) {
			if (history->second.handle)
				Inspect::FreeObjectHandle(history->second);
			object_inspector_history_.erase(history);
		}

		for (auto it = working_.method_results.begin(); it != working_.method_results.end();) {
			if (it->second.object_inspector_token != token) {
				++it;
				continue;
			}
			if (it->second.reference.token != 0)
				release_reference_handle(it->second.reference.token);
			it = working_.method_results.erase(it);
		}

		if (!working_.object_inspector.valid || working_.object_inspector.token != token)
			return;

		if (object_inspector_handle_.handle)
			Inspect::FreeObjectHandle(object_inspector_handle_);
		object_inspector_handle_ = {};
		object_inspector_reflection_ = {};
		sampled_object_fields_.clear();
		sampled_object_properties_.clear();
		for (auto it = reference_handles_.begin(); it != reference_handles_.end();) {
			const std::uint64_t scope = it->first & 0xF000000000000000ull;
			if (scope != 0x3000000000000000ull && scope != 0x4000000000000000ull && scope != 0x5000000000000000ull) {
				++it;
				continue;
			}
			if (it->second.handle)
				Inspect::FreeObjectHandle(it->second);
			it = reference_handles_.erase(it);
		}
		clear_locked_members(true);
		working_.object_inspector = {};
		set_status("Object Inspector tab closed");
	}

	void RuntimeModel::release_reference_handle(std::uint64_t token) {
		const auto found = reference_handles_.find(token);
		if (found == reference_handles_.end())
			return;
		if (found->second.handle) {
			Inspect::FreeObjectHandle(found->second);
		}
		reference_handles_.erase(found);
	}

	void RuntimeModel::load_component_metadata(int component_instance_id) {
		const Object target = resolve_component(component_instance_id);
		if (!safe_object_alive(target) || target.GetInstanceID() != component_instance_id) {
			set_status("Component is no longer available");
			refresh_inspector(true);
			return;
		}

		auto component = std::find_if(
			working_.inspector.components.begin(), working_.inspector.components.end(),
			[component_instance_id](const ComponentInfo& info) { return info.instance_id == component_instance_id; });
		if (component == working_.inspector.components.end() ||
			(component->metadata && !component->metadata_unavailable))
			return;
		record_flight("TARGET", "Load component metadata", component->type_name + " #" + std::to_string(component_instance_id));
		const auto fail_metadata = [&](std::string message) {
			component_reflection_.erase(component_instance_id);
			component->metadata = std::make_shared<ComponentInfo::Metadata>();
			component->metadata_unavailable = true;
			component->metadata_error = std::move(message);
			set_status("Component metadata load failed for " + component->type_name + ": " + component->metadata_error);
			};

		const auto* klass = static_cast<const URK::il2cpp::Class*>(
			URK::il2cpp::object_get_class(static_cast<URK::il2cpp::Object*>(target.handle())));
		if (!klass) {
			fail_metadata("runtime class is unavailable");
			return;
		}
		const Inspect::TypeInfo current_type = Inspect::DescribeClass(klass);
		if (!current_type.handle || current_type.full_name.empty()) {
			fail_metadata("runtime class identity is unavailable");
			return;
		}
		if (!component->type_name.empty() && component->type_name != current_type.full_name) {
			set_status("Component changed while metadata was loading; inspector refreshed");
			refresh_inspector(true);
			return;
		}

		auto metadata = std::make_shared<ComponentInfo::Metadata>();
		ComponentReflection reflection{};
		std::vector<std::string> metadata_warnings;
		const auto enumerate = [&](std::string_view section, auto&& action) {
			active_metadata_stage_ = std::string(section);
			clear_error();
			action();
			if (const char* error = last_error(); error && error[0])
				metadata_warnings.push_back(std::string(section) + ": " + error);
			};
		// Capture the class once from the rooted component. Re-reading it before
		// each section allowed a destroyed/recreated wrapper to mix metadata from
		// different objects into one snapshot.
		enumerate("Fields", [&] { reflection.fields = Inspect::fields_from_class(klass, true); });
		enumerate("Properties", [&] { reflection.properties = Inspect::properties_from_class(klass, true); });
		enumerate("Methods", [&] { reflection.methods = Inspect::methods_from_class(klass, true); });
		active_metadata_stage_.clear();

		component->dynamic_bridge = {};
		for (std::size_t method_index = 0; method_index < reflection.methods.size(); ++method_index) {
			const Inspect::MethodInfo& method = reflection.methods[method_index];
			const bool parameterless_instance = !method.is_static && method.parameters.empty();
			if (parameterless_instance && method.return_type == "System.String" &&
				(method.name == "GetBehaviourType" || method.name == "GetBehaviorType" ||
					method.name == "GetScriptType")) {
				component->dynamic_bridge.detected = true;
				component->dynamic_bridge.type_getter = method.name + "()";
				component->dynamic_bridge.type_getter_method_index = static_cast<int>(method_index);
			}
			if (method.name == "GetSerializedData" || method.name == "GetSerializedDataRef" ||
				method.name == "GetSerializedDataInternal") {
				component->dynamic_bridge.detected = true;
				component->dynamic_bridge.serialized_data_methods.push_back(
					method.name + "(" + std::to_string(method.parameters.size()) + " arg" +
					(method.parameters.size() == 1 ? ")" : "s)"));
			}
			if (method.name == "GetObject" && !method.is_static) {
				component->dynamic_bridge.detected = true;
				component->dynamic_bridge.object_reference_methods.push_back(
					method.name + "(" + std::to_string(method.parameters.size()) + " arg" +
					(method.parameters.size() == 1 ? ")" : "s)"));
			}
		}

		metadata->fields.reserve(reflection.fields.size());
		for (const Inspect::FieldInfo& field : reflection.fields) {
			metadata->fields.push_back({ field.name, field.type_name, field.declaring_type.full_name, field.is_static,
				{}, field.is_value_type, field.is_enum, !field.type_is_opaque,
				field.type_is_opaque ? "Runtime-specific type; metadata is available but generic read/write is unsafe." : "" });
		}

		metadata->properties.reserve(reflection.properties.size());
		for (const Inspect::PropertyInfo& property : reflection.properties) {
			metadata->properties.push_back({ property.name, property.type_name, property.declaring_type.full_name,
									 property.can_read, property.can_write, {}, property.is_value_type, property.is_enum,
									 !property.type_is_opaque,
									 property.type_is_opaque ? "Runtime-specific type; metadata is available but generic read/write is unsafe." : "" });
		}

		metadata->methods.reserve(reflection.methods.size());
		for (const Inspect::MethodInfo& method : reflection.methods) {
			// Feed caller resolution only from metadata that was successfully
			// loaded for the current inspected component. Never enumerate the
			// whole domain merely to decorate a trace row.
			remember_managed_method(method);
			ComponentInfo::Method member{};
			member.name = method.name;
			member.return_type = method.return_type;
			member.declaring_type = method.declaring_type.full_name;
			member.is_static = method.is_static;
			member.return_is_value_type = method.return_is_value_type;
			member.return_is_enum = method.return_is_enum;
			member.runtime_callable = !method.return_type_is_opaque &&
				std::none_of(method.parameters.begin(), method.parameters.end(), [](const Inspect::MethodParamInfo& parameter) {
					return parameter.is_opaque;
					});
			if (!member.runtime_callable)
				member.capability_reason = "Runtime-specific by-ref/internal signature; generic invocation is disabled.";
			member.parameter_types.reserve(method.parameters.size());
			member.parameter_names.reserve(method.parameters.size());
			for (const Inspect::MethodParamInfo& parameter : method.parameters) {
				member.parameter_types.push_back(parameter.type_name);
				member.parameter_names.push_back(parameter.name);
				member.parameter_is_value_types.push_back(parameter.is_value_type);
				member.parameter_is_enums.push_back(parameter.is_enum);
			}
			metadata->methods.push_back(std::move(member));
		}

		// The UI consumes immutable data captured on the game thread.
		component_reflection_[component_instance_id] = std::move(reflection);
		component->metadata = std::move(metadata);
		component->metadata_unavailable = false;
		component->metadata_error.clear();
		if (!metadata_warnings.empty()) {
			for (std::size_t index = 0; index < metadata_warnings.size(); ++index) {
				if (index)
					component->metadata_error += " | ";
				component->metadata_error += metadata_warnings[index];
			}
		}
		refresh_live_member_values();
		if (component->metadata_error.empty())
			set_status("Loaded all metadata for " + component->type_name);
		else
			set_status("Loaded metadata for " + component->type_name + " with explicit member diagnostics");
	}

	void RuntimeModel::load_component_class_catalog() {
		// IL2CPP metadata is read only from the Unity main thread.
		const URK::il2cpp::Class* component_base =
			URK::il2cpp::find_class("UnityEngine.CoreModule.dll", "UnityEngine", "Component");
		if (!component_base)
			component_base = URK::il2cpp::find_class("UnityEngine.dll", "UnityEngine", "Component");
		if (!component_base) {
			set_status("Component browser is unavailable: UnityEngine.Component was not found");
			return;
		}

		auto catalog = std::make_shared<ComponentClassCatalog>();
		std::unordered_set<std::string> seen;
		constexpr std::uint32_t kTypeAttributeInterface = 0x20u;
		constexpr std::uint32_t kTypeAttributeAbstract = 0x80u;
		constexpr std::size_t kMaxComponentClasses = 20000;

		const std::size_t assembly_count = std::min<std::size_t>(URK::il2cpp::domain_get_assembly_count(), 4096);
		for (std::size_t assembly_index = 0; assembly_index < assembly_count; ++assembly_index) {
			const URK::il2cpp::Assembly* assembly = URK::il2cpp::domain_get_assembly(assembly_index);
			const URK::il2cpp::Image* image = assembly ? URK::il2cpp::assembly_get_image(assembly) : nullptr;
			const char* image_name = image ? URK::il2cpp::image_get_name(image) : nullptr;
			if (!image || !image_name || !image_name[0])
				continue;

			const std::size_t class_count = std::min<std::size_t>(URK::il2cpp::image_get_class_count(image), 1000000);
			for (std::size_t class_index = 0; class_index < class_count; ++class_index) {
				const URK::il2cpp::Class* klass = URK::il2cpp::image_get_class(image, class_index);
				if (!klass || klass == component_base)
					continue;
				const std::uint32_t flags = URK::il2cpp::class_get_flags(klass);
				if ((flags & (kTypeAttributeInterface | kTypeAttributeAbstract)) != 0)
					continue;

				bool is_component = URK::il2cpp::class_is_assignable_from(component_base, klass) != 0;
				if (!is_component) {
					std::unordered_set<const URK::il2cpp::Class*> visited_parents;
					std::size_t parent_depth = 0;
					for (const URK::il2cpp::Class* parent = URK::il2cpp::class_get_parent(klass);
						parent && parent_depth++ < Inspect::kMaxMetadataInheritanceDepth &&
						visited_parents.insert(parent).second;
						parent = URK::il2cpp::class_get_parent(parent)) {
						if (parent == component_base) {
							is_component = true;
							break;
						}
					}
				}
				if (!is_component)
					continue;

				const Inspect::TypeInfo type = Inspect::DescribeClass(klass);
				if (type.name.empty())
					continue;
				// Unity creates Transform components automatically.
				if (type.full_name == "UnityEngine.Transform" || type.full_name == "UnityEngine.RectTransform")
					continue;

				ComponentClassInfo entry{};
				entry.image = image_name;
				entry.namespc = type.namespc;
				entry.class_name = type.name;
				entry.full_name = type.full_name.empty() ? type.name : type.full_name;
				entry.pointer_text = pointer_text(const_cast<URK::il2cpp::Class*>(klass));
				const std::string key = entry.image + "\n" + entry.full_name;
				if (!seen.insert(key).second)
					continue;
				catalog->classes.push_back(std::move(entry));
				if (catalog->classes.size() >= kMaxComponentClasses)
					break;
			}
			if (catalog->classes.size() >= kMaxComponentClasses)
				break;
		}

		std::sort(catalog->classes.begin(), catalog->classes.end(),
			[](const ComponentClassInfo& left, const ComponentClassInfo& right) {
				return left.full_name == right.full_name ? left.image < right.image
					: left.full_name < right.full_name;
			});
		component_class_catalog_ = std::move(catalog);
		set_status("Component browser loaded " + std::to_string(component_class_catalog_->classes.size()) + " classes");
	}

	void RuntimeModel::load_class_browser_catalog() {
		const URK::il2cpp::Class* object_base =
			URK::il2cpp::find_class("UnityEngine.CoreModule.dll", "UnityEngine", "Object");
		const URK::il2cpp::Class* component_base =
			URK::il2cpp::find_class("UnityEngine.CoreModule.dll", "UnityEngine", "Component");
		if (!object_base)
			object_base = URK::il2cpp::find_class("UnityEngine.dll", "UnityEngine", "Object");
		if (!component_base)
			component_base = URK::il2cpp::find_class("UnityEngine.dll", "UnityEngine", "Component");
		auto catalog = std::make_shared<ClassBrowserCatalog>();
		std::unordered_set<std::string> seen;
		constexpr std::uint32_t kTypeAttributeInterface = 0x20u;
		constexpr std::uint32_t kTypeAttributeAbstract = 0x80u;
		constexpr std::uint32_t kTypeAttributeSealed = 0x100u;
		constexpr std::size_t kMaxBrowserClasses = 50000;
		const std::size_t assembly_count = std::min<std::size_t>(URK::il2cpp::domain_get_assembly_count(), 4096);
		for (std::size_t assembly_index = 0; assembly_index < assembly_count; ++assembly_index) {
			const URK::il2cpp::Assembly* assembly = URK::il2cpp::domain_get_assembly(assembly_index);
			const URK::il2cpp::Image* image = assembly ? URK::il2cpp::assembly_get_image(assembly) : nullptr;
			const char* image_name = image ? URK::il2cpp::image_get_name(image) : nullptr;
			if (!image || !image_name || !image_name[0])
				continue;
			const std::size_t class_count = std::min<std::size_t>(URK::il2cpp::image_get_class_count(image), 1000000);
			for (std::size_t class_index = 0; class_index < class_count; ++class_index) {
				const URK::il2cpp::Class* klass = URK::il2cpp::image_get_class(image, class_index);
				if (!klass)
					continue;
				const Inspect::TypeInfo type = Inspect::DescribeClass(klass);
				if (type.name.empty() || type.name == "<Module>")
					continue;
				BrowserClassInfo entry{};
				entry.image = image_name;
				entry.namespc = type.namespc;
				entry.class_name = type.name;
				entry.full_name = type.full_name.empty() ? type.name : type.full_name;
				entry.pointer_text = pointer_text(const_cast<URK::il2cpp::Class*>(klass));
				const std::uint32_t flags = URK::il2cpp::class_get_flags(klass);
				entry.parent_name = Inspect::DescribeClass(URK::il2cpp::class_get_parent(klass)).full_name;
				entry.is_interface = (flags & kTypeAttributeInterface) != 0;
				entry.is_abstract = (flags & kTypeAttributeAbstract) != 0;
				entry.is_static = entry.is_abstract && (flags & kTypeAttributeSealed) != 0;
				entry.is_value_type = type.is_value_type;
				entry.is_enum = type.is_enum;
				entry.is_unity_object = object_base && URK::il2cpp::class_is_assignable_from(object_base, klass) != 0;
				entry.is_component = component_base && (URK::il2cpp::class_is_assignable_from(component_base, klass) != 0 ||
					URK::il2cpp::class_has_parent(klass, component_base) != 0);
				void* interface_iterator = nullptr;
				std::unordered_set<const URK::il2cpp::Class*> visited_interfaces;
				std::size_t interface_count = 0;
				while (const URK::il2cpp::Class* interface_type =
					URK::il2cpp::class_get_interfaces(klass, &interface_iterator)) {
					if (++interface_count > 4096 || !visited_interfaces.insert(interface_type).second)
						break;
					const Inspect::TypeInfo interface_info = Inspect::DescribeClass(interface_type);
					if (!interface_info.full_name.empty())
						entry.interfaces.push_back(interface_info.full_name);
				}
				const std::string key = entry.image + "\n" + entry.full_name;
				if (!seen.insert(key).second)
					continue;
				catalog->classes.push_back(std::move(entry));
				if (catalog->classes.size() >= kMaxBrowserClasses)
					break;
			}
			if (catalog->classes.size() >= kMaxBrowserClasses)
				break;
		}
		std::sort(catalog->classes.begin(), catalog->classes.end(),
			[](const BrowserClassInfo& left, const BrowserClassInfo& right) {
				return left.full_name == right.full_name ? left.image < right.image
					: left.full_name < right.full_name;
			});
		class_browser_catalog_ = std::move(catalog);
		set_status("Class browser indexed " + std::to_string(class_browser_catalog_->classes.size()) + " loaded types");
	}

	void RuntimeModel::find_class_instances(const Command& command) {
		for (auto& [_, handle] : class_browser_handles_)
			Inspect::FreeObjectHandle(handle);
		class_browser_handles_.clear();
		working_.class_browser_instances.clear();
		working_.class_browser_scanned_objects = 0;
		working_.class_browser_static_roots = 0;
		working_.class_browser_scan_truncated = false;
		working_.class_browser_query = {};
		working_.class_browser_query.image = command.image;
		working_.class_browser_query.namespc = command.namespc;
		working_.class_browser_query.class_name = command.class_name;
		working_.class_browser_query.full_name =
			command.namespc.empty() ? command.class_name : command.namespc + "." + command.class_name;
		working_.class_browser_query.is_component = command.int_value != 0;
		if (command.class_name.empty()) {
			set_status("Choose a class before finding instances");
			return;
		}

		const URK::il2cpp::Class* target =
			URK::il2cpp::find_class(command.image.c_str(), command.namespc.c_str(), command.class_name.c_str());
		if (!target) {
			set_status("Selected type could no longer be resolved");
			return;
		}

		// Query instances through Unity's managed API; inspect statics separately.
		const auto direct_instances = command.bool_value
			? detail::FindObjectsUsingRooted<Object>(ResourcesType, "FindObjectsOfTypeAll", command.image,
				command.namespc, command.class_name)
			: detail::FindObjectsUsingRooted<Object>(UnityObjectType, "FindObjectsOfType", command.image,
				command.namespc, command.class_name);
		constexpr std::size_t kMaxDirectInstanceResults = 512;
		for (const Object& object : direct_instances) {
			if (working_.class_browser_instances.size() >= kMaxDirectInstanceResults)
				break;
			const Inspect::TypeInfo type = safe_type_of(object);
			Inspect::ObjectHandle handle = type.is_value_type ? Inspect::PinObject(object) : Inspect::WeakObject(object);
			if (!handle.handle)
				continue;
			const std::uint64_t token = 0x9000000000000000ull | (next_reference_token_++ & 0x0fffffffffffffffull);
			class_browser_handles_[token] = handle;
			ClassBrowserInstanceInfo result{};
			result.token = token;
			result.type_name = type.full_name.empty() ? working_.class_browser_query.full_name : type.full_name;
			result.pointer_text = pointer_text(object.handle());
			result.source = "Unity object query";
			result.name = object.name();
			if (working_.class_browser_query.is_component) {
				const GameObject owner = Component{ object.handle() }.gameObject();
				if (owner) {
					result.game_object_instance_id = owner.GetInstanceID();
					result.game_object_name = owner.name();
				}
			}
			if (result.name.empty())
				result.name = result.type_name;
			working_.class_browser_instances.push_back(std::move(result));
		}
		working_.class_browser_scanned_objects = direct_instances.size();
		set_status("Found " + std::to_string(working_.class_browser_instances.size()) + " Unity instance(s) of " +
			working_.class_browser_query.full_name);
		return;

		const URK::il2cpp::Class* object_base =
			URK::il2cpp::find_class("UnityEngine.CoreModule.dll", "UnityEngine", "Object");
		if (!object_base)
			object_base = URK::il2cpp::find_class("UnityEngine.dll", "UnityEngine", "Object");

		struct PendingObject {
			Object object;
			int depth = 0;
			bool array = false;
			std::string source;
		};
		constexpr std::size_t kMaxGraphNodes = 30000;
		constexpr std::size_t kMaxArrayElements = 256;
		constexpr int kMaxGraphDepth = 5;
		constexpr std::size_t kMaxInstanceResults = 512;
		std::deque<PendingObject> pending;
		std::unordered_set<void*> seen;
		const auto enqueue_object = [&](Object object, int depth, bool array, std::string source) {
			if (!object.handle() || seen.size() >= kMaxGraphNodes)
				return false;
			if (!seen.insert(object.handle()).second)
				return true;
			pending.push_back({ object, depth, array, std::move(source) });
			return true;
			};

		// Static references cover non-Unity managed singletons and interfaces.
		const std::size_t assembly_count = std::min<std::size_t>(URK::il2cpp::domain_get_assembly_count(), 4096);
		for (std::size_t assembly_index = 0; assembly_index < assembly_count && seen.size() < kMaxGraphNodes;
			++assembly_index) {
			const URK::il2cpp::Assembly* assembly = URK::il2cpp::domain_get_assembly(assembly_index);
			const URK::il2cpp::Image* image = assembly ? URK::il2cpp::assembly_get_image(assembly) : nullptr;
			if (!image)
				continue;
			const std::size_t class_count = std::min<std::size_t>(URK::il2cpp::image_get_class_count(image), 1000000);
			for (std::size_t class_index = 0; class_index < class_count && seen.size() < kMaxGraphNodes; ++class_index) {
				const URK::il2cpp::Class* klass = URK::il2cpp::image_get_class(image, class_index);
				if (!klass)
					continue;
				const Inspect::TypeInfo owner = Inspect::DescribeClass(klass);
				for (const Inspect::FieldInfo& field : Inspect::fields_from_class(klass, false)) {
					if (!field.is_static || field.is_value_type)
						continue;
					const Inspect::ValueInfo value = Inspect::ReadField({}, field);
					if ((value.kind != Inspect::ValueKind::ObjectReference &&
						value.kind != Inspect::ValueKind::ArrayReference) ||
						!value.object)
						continue;
					if (enqueue_object(Object{ value.object }, 0, value.kind == Inspect::ValueKind::ArrayReference,
						"static " + owner.full_name + "." + field.name))
						++working_.class_browser_static_roots;
				}
			}
		}

		const auto unity_roots = command.bool_value ? Object::FindObjectsOfTypeAllRooted<Object>()
			: Object::FindObjectsOfTypeRooted<Object>();
		for (const Object& root : unity_roots) {
			if (seen.size() >= kMaxGraphNodes)
				break;
			enqueue_object(root, 0, false, "Unity root");
		}

		while (!pending.empty()) {
			PendingObject current = std::move(pending.front());
			pending.pop_front();
			++working_.class_browser_scanned_objects;
			const URK::il2cpp::Class* actual =
				URK::il2cpp::object_get_class(static_cast<URK::il2cpp::Object*>(current.object.handle()));
			if (!actual)
				continue;
			if (URK::il2cpp::class_is_assignable_from(target, actual) != 0 &&
				working_.class_browser_instances.size() < kMaxInstanceResults) {
				const Inspect::TypeInfo type = Inspect::DescribeClass(actual);
				Inspect::ObjectHandle handle = Inspect::PinObject(current.object);
				if (handle.handle) {
					const std::uint64_t token = 0x9000000000000000ull | (next_reference_token_++ & 0x0fffffffffffffffull);
					class_browser_handles_[token] = handle;
					ClassBrowserInstanceInfo result{};
					result.token = token;
					result.type_name = type.full_name.empty() ? working_.class_browser_query.full_name : type.full_name;
					result.pointer_text = pointer_text(current.object.handle());
					result.source = current.source;
					result.name = result.type_name;
					if (object_base && URK::il2cpp::class_is_assignable_from(object_base, actual) != 0)
						result.name = current.object.name();
					if (current.depth == 0 && working_.class_browser_query.is_component) {
						const GameObject owner = Component{ current.object.handle() }.gameObject();
						if (owner) {
							result.game_object_instance_id = owner.GetInstanceID();
							result.game_object_name = owner.name();
						}
					}
					if (result.name.empty())
						result.name = result.type_name;
					working_.class_browser_instances.push_back(std::move(result));
				}
			}
			if (current.depth >= kMaxGraphDepth)
				continue;
			if (current.array) {
				Inspect::ValueInfo array{};
				array.kind = Inspect::ValueKind::ArrayReference;
				array.object = current.object.handle();
				const std::size_t array_length = std::min(Inspect::ArrayLength(array), kMaxArrayElements);
				for (std::size_t index = 0; index < array_length; ++index) {
					const Inspect::ValueInfo element = Inspect::ReadArrayElement(array, index);
					if ((element.kind == Inspect::ValueKind::ObjectReference ||
						element.kind == Inspect::ValueKind::ArrayReference) &&
						element.object)
						enqueue_object(Object{ element.object }, current.depth + 1,
							element.kind == Inspect::ValueKind::ArrayReference, current.source + "[]");
				}
				continue;
			}
			for (const Inspect::FieldInfo& field : Inspect::fields_from_class(actual, true)) {
				if (field.is_static || field.is_value_type)
					continue;
				const Inspect::ValueInfo value = Inspect::ReadField(current.object, field);
				if ((value.kind == Inspect::ValueKind::ObjectReference ||
					value.kind == Inspect::ValueKind::ArrayReference) &&
					value.object)
					enqueue_object(Object{ value.object }, current.depth + 1,
						value.kind == Inspect::ValueKind::ArrayReference, current.source + " -> " + field.name);
			}
		}
		working_.class_browser_scan_truncated = seen.size() >= kMaxGraphNodes;
		set_status("Found " + std::to_string(working_.class_browser_instances.size()) + " instance(s); scanned " +
			std::to_string(working_.class_browser_scanned_objects) + " reachable object(s)");
	}

	void RuntimeModel::load_class_browser_static_state(const Command& command) {
		for (auto& [_, handle] : class_browser_static_handles_)
			Inspect::FreeObjectHandle(handle);
		class_browser_static_handles_.clear();
		working_.class_browser_static_fields.clear();
		working_.class_browser_static_query = {};
		working_.class_browser_static_query.image = command.image;
		working_.class_browser_static_query.namespc = command.namespc;
		working_.class_browser_static_query.class_name = command.class_name;
		working_.class_browser_static_query.full_name =
			command.namespc.empty() ? command.class_name : command.namespc + "." + command.class_name;
		if (class_browser_catalog_) {
			const auto match = std::find_if(class_browser_catalog_->classes.begin(), class_browser_catalog_->classes.end(),
				[&command](const BrowserClassInfo& entry) {
					return entry.image == command.image && entry.namespc == command.namespc &&
						entry.class_name == command.class_name;
				});
			if (match != class_browser_catalog_->classes.end())
				working_.class_browser_static_query = *match;
		}
		const URK::il2cpp::Class* klass =
			URK::il2cpp::find_class(command.image.c_str(), command.namespc.c_str(), command.class_name.c_str());
		if (!klass) {
			set_status("Selected type could no longer be resolved");
			return;
		}
		for (const Inspect::FieldInfo& field : Inspect::fields_from_class(klass, true)) {
			if (!field.is_static)
				continue;
			const Inspect::ValueInfo value = Inspect::ReadField({}, field);
			ClassBrowserStaticFieldInfo result{};
			result.name = field.name;
			result.type_name = field.type_name;
			result.declaring_type = field.declaring_type.full_name;
			result.display = value.display.empty() ? "<unavailable>" : value.display;
			result.readable = value.readable;
			result.is_reference =
				value.kind == Inspect::ValueKind::ObjectReference || value.kind == Inspect::ValueKind::ArrayReference;
			if (result.is_reference && value.object) {
				Inspect::ObjectHandle handle = Inspect::PinObject(Object{ value.object });
				if (handle.handle) {
					result.token = 0xa000000000000000ull | (next_reference_token_++ & 0x0fffffffffffffffull);
					result.pointer_text = pointer_text(value.object);
					class_browser_static_handles_[result.token] = handle;
				}
			}
			working_.class_browser_static_fields.push_back(std::move(result));
		}
		set_status("Loaded " + std::to_string(working_.class_browser_static_fields.size()) + " static field(s) from " +
			working_.class_browser_static_query.full_name);
	}

	void RuntimeModel::load_class_browser_members(const Command& command) {
		working_.class_browser_members = {};
		working_.class_browser_members_query = {};
		working_.class_browser_members_query.image = command.image;
		working_.class_browser_members_query.namespc = command.namespc;
		working_.class_browser_members_query.class_name = command.class_name;
		working_.class_browser_members_query.full_name =
			command.namespc.empty() ? command.class_name : command.namespc + "." + command.class_name;
		if (class_browser_catalog_) {
			const auto match = std::find_if(class_browser_catalog_->classes.begin(), class_browser_catalog_->classes.end(),
				[&command](const BrowserClassInfo& entry) {
					return entry.image == command.image && entry.namespc == command.namespc &&
						entry.class_name == command.class_name;
				});
			if (match != class_browser_catalog_->classes.end())
				working_.class_browser_members_query = *match;
		}
		const URK::il2cpp::Class* klass =
			URK::il2cpp::find_class(command.image.c_str(), command.namespc.c_str(), command.class_name.c_str());
		if (!klass) {
			set_status("Selected type could no longer be resolved");
			return;
		}
		auto members = std::make_shared<ComponentInfo::Metadata>();
		for (const Inspect::FieldInfo& field : Inspect::fields_from_class(klass, true))
			members->fields.push_back({ field.name, field.type_name, field.declaring_type.full_name, field.is_static,
									   pointer_text(const_cast<void*>(field.handle)), field.is_value_type, field.is_enum,
									   !field.type_is_opaque,
									   field.type_is_opaque ? "Runtime-specific type; metadata only." : "" });
		for (const Inspect::PropertyInfo& property : Inspect::properties_from_class(klass, true))
			members->properties.push_back({ property.name, property.type_name, property.declaring_type.full_name,
									 property.can_read, property.can_write,
									 pointer_text(const_cast<void*>(property.handle)), property.is_value_type, property.is_enum,
									 !property.type_is_opaque,
									 property.type_is_opaque ? "Runtime-specific type; metadata only." : "" });
		for (const Inspect::MethodInfo& method : Inspect::methods_from_class(klass, true)) {
			ComponentInfo::Method entry{};
			entry.name = method.name;
			entry.return_type = method.return_type;
			entry.declaring_type = method.declaring_type.full_name;
			entry.is_static = method.is_static;
			entry.pointer_text = pointer_text(const_cast<void*>(method.handle));
			entry.return_is_value_type = method.return_is_value_type;
			entry.return_is_enum = method.return_is_enum;
			entry.runtime_callable = !method.return_type_is_opaque &&
				std::none_of(method.parameters.begin(), method.parameters.end(), [](const Inspect::MethodParamInfo& parameter) {
					return parameter.is_opaque;
					});
			if (!entry.runtime_callable)
				entry.capability_reason = "Runtime-specific by-ref/internal signature; generic invocation is disabled.";
			for (const Inspect::MethodParamInfo& parameter : method.parameters) {
				entry.parameter_types.push_back(parameter.type_name);
				entry.parameter_names.push_back(parameter.name);
				entry.parameter_is_value_types.push_back(parameter.is_value_type);
				entry.parameter_is_enums.push_back(parameter.is_enum);
			}
			members->methods.push_back(std::move(entry));
		}
		working_.class_browser_members = std::move(members);
		set_status("Loaded members for " + working_.class_browser_members_query.full_name);
	}

	void RuntimeModel::refresh_live_member_values(bool force) {
		if ((!force && event_refresh_pending_) || !safe_object_alive(selected_))
			return;

		for (ComponentInfo& component : working_.inspector.components) {
			if (!component.metadata)
				continue;
			const auto reflection = component_reflection_.find(component.instance_id);
			const Object target = resolve_component(component.instance_id);
			if (reflection == component_reflection_.end() || !safe_object_alive(target))
				continue;

			auto values = std::make_shared<ComponentInfo::LiveValues>();
			values->fields.resize(component.metadata->fields.size());
			values->properties.resize(component.metadata->properties.size());
			values->field_references.resize(component.metadata->fields.size());
			values->property_references.resize(component.metadata->properties.size());
			auto capture_reference = [&](Inspect::ValueInfo& value, std::uint64_t token,
				ComponentInfo::LiveValues::Reference& reference) {
					const bool is_reference =
						value.kind == Inspect::ValueKind::ObjectReference || value.kind == Inspect::ValueKind::ArrayReference;
					if (!is_reference || !value.object) {
						release_reference_handle(token);
						value.object = nullptr;
						return;
					}
					if (const auto previous = reference_handles_.find(token); previous != reference_handles_.end()) {
						const Object current = Inspect::ResolveObjectHandle(previous->second);
						if (current && current.handle() == value.object) {
							reference = { token, value.type_name, value.display, pointer_text(current.handle()), false };
							value.object = nullptr;
							return;
						}
					}
					Inspect::ObjectHandle rooted = tracked_reference_handle(value);
					if (!rooted.handle) {
						value = Inspect::unavailable_value(value.type_name, "could not track referenced object");
						return;
					}
					const Object tracked = Inspect::ResolveObjectHandle(rooted);
					if (!tracked) {
						Inspect::FreeObjectHandle(rooted);
						value = Inspect::unavailable_value(value.type_name, "referenced object was released");
						return;
					}
					release_reference_handle(token);
					reference_handles_[token] = rooted;
					reference.token = token;
					reference.type_name = value.type_name;
					reference.display = value.display;
					reference.pointer_text = pointer_text(tracked.handle());
					reference.is_null = false;
					value.object = nullptr;
				};
			for (std::size_t index = 0; index < values->fields.size(); ++index) {
				const bool sampled =
					sampled_component_members_.contains(component_sample_token(component.instance_id, false, index));
				if (!component.metadata->fields[index].runtime_safe)
					values->fields[index] = Inspect::unavailable_value(component.metadata->fields[index].type_name,
						"Metadata only: " + component.metadata->fields[index].capability_reason);
				else if (index < reflection->second.fields.size() && sampled)
					values->fields[index] = guarded_managed_read(component.metadata->fields[index].type_name, [&] {
					return Inspect::ReadField(target, reflection->second.fields[index]);
						});
				else
					values->fields[index] =
					Inspect::unavailable_value(component.metadata->fields[index].type_name, "Not sampled");
				record_value_error(component.type_name + "." + component.metadata->fields[index].name,
					values->fields[index]);
				capture_reference(values->fields[index], member_reference_token(component.instance_id, false, index),
					values->field_references[index]);
			}
			for (std::size_t index = 0; index < values->properties.size(); ++index) {
				const bool sampled =
					sampled_component_members_.contains(component_sample_token(component.instance_id, true, index));
				if (!component.metadata->properties[index].runtime_safe)
					values->properties[index] = Inspect::unavailable_value(component.metadata->properties[index].type_name,
						"Metadata only: " + component.metadata->properties[index].capability_reason);
				else if (index < reflection->second.properties.size() && sampled)
					values->properties[index] = guarded_managed_read(component.metadata->properties[index].type_name, [&] {
					return Inspect::ReadProperty(target, reflection->second.properties[index]);
						});
				else
					values->properties[index] =
					Inspect::unavailable_value(component.metadata->properties[index].type_name, "Not sampled");
				record_value_error(component.type_name + "." + component.metadata->properties[index].name,
					values->properties[index]);
				capture_reference(values->properties[index], member_reference_token(component.instance_id, true, index),
					values->property_references[index]);
			}
			component.live_values = std::move(values);
		}
	}

	void RuntimeModel::sample_member_value(const Command& command) {
		if (command.member_index < 0) {
			set_status("Cannot sample an invalid member index");
			return;
		}
		const std::size_t index = static_cast<std::size_t>(command.member_index);
		if (command.object_inspector_target) {
			if (!working_.object_inspector.valid || command.object_inspector_token == 0 ||
				command.object_inspector_token != working_.object_inspector.token) {
				set_status("Object Inspector tab changed before the value could be sampled");
				return;
			}
			(command.bool_value ? sampled_object_properties_ : sampled_object_fields_).insert(index);
			refresh_object_inspector_values();
		}
		else {
			sampled_component_members_.insert(component_sample_token(command.instance_id, command.bool_value, index));
			refresh_live_member_values();
		}
		set_status(std::string("Sampled ") + (command.bool_value ? "property" : "field") + " [" + std::to_string(index) +
			"]");
	}

	void RuntimeModel::set_member_value(const Command& command, bool property, const Inspect::ValueInfo* prepared,
		bool verify) {
		const std::uint64_t lock_key = command.reference_token;
		if (command.unlock_value) {
			const auto found = locked_members_.find(lock_key);
			if (found != locked_members_.end()) {
				Inspect::FreeObjectHandle(found->second.value_root);
				locked_members_.erase(found);
			}
			working_.locked_member_keys.erase(lock_key);
			set_status("Member unlocked");
			return;
		}

		const bool nested = command.object_inspector_target;
		if (nested && (!working_.object_inspector.valid || command.object_inspector_token == 0 ||
			command.object_inspector_token != working_.object_inspector.token)) {
			set_status("Object Inspector tab changed before the value could be applied");
			return;
		}
		const Object target =
			nested ? Inspect::ResolveObjectHandle(object_inspector_handle_) : resolve_component(command.instance_id);
		if (nested && working_.object_inspector.is_array && !property) {
			if (!target || command.member_index < 0 ||
				static_cast<std::size_t>(command.member_index) >= working_.object_inspector.array_length) {
				set_status("Array element is no longer available");
				return;
			}
			Inspect::ValueInfo value{};
			Inspect::ObjectHandle argument_root{};
			const auto* array_class = static_cast<const URK::il2cpp::Class*>(
				URK::il2cpp::object_get_class(static_cast<URK::il2cpp::Object*>(target.handle())));
			const auto* element_class = array_class ? URK::il2cpp::class_get_element_class(array_class) : nullptr;
			const void* element_type = element_class ? URK::il2cpp::class_get_type(element_class) : nullptr;
			const bool parsed = command_value(working_.object_inspector.array_element_type, command, value) ||
				reference_value_from_text(working_.object_inspector.array_element_type, element_type,
					command.text, value, argument_root);
			if (!parsed) {
				set_status("Unsupported array element type: " + working_.object_inspector.array_element_type);
				return;
			}
			if (value.object) {
				const void* actual_class = URK::il2cpp::object_get_class(value.object);
				const bool is_value_element = element_class && URK::il2cpp::class_is_valuetype(element_class);
				const bool matches =
					element_class && actual_class &&
					(is_value_element ? element_class == actual_class
						: URK::il2cpp::class_is_assignable_from(element_class, actual_class) != 0);
				if (!matches) {
					Inspect::FreeObjectHandle(argument_root);
					set_status("Array reference type mismatch: expected " + working_.object_inspector.array_element_type);
					return;
				}
			}
			Inspect::ValueInfo array{};
			array.kind = Inspect::ValueKind::ArrayReference;
			array.type_name = working_.object_inspector.type_name;
			array.object = target.handle();
			array.readable = true;
			if (Inspect::SetArrayElement(array, static_cast<std::size_t>(command.member_index), value)) {
				const Inspect::ValueInfo actual = guarded_managed_read(working_.object_inspector.array_element_type, [&] {
					return Inspect::ReadArrayElement(array, static_cast<std::size_t>(command.member_index));
					});
				if (values_equivalent(value, actual))
					set_status("Set array element [" + std::to_string(command.member_index) + "] (verified)");
				else
					set_status("Write verification failed for array element [" + std::to_string(command.member_index) +
						"]: requested " + value.display + ", read " + actual.display);
				refresh_object_inspector_values(true);
			}
			else {
				capture_last_error("Set array element");
			}
			Inspect::FreeObjectHandle(argument_root);
			return;
		}
		const ComponentReflection* reflection = nested ? &object_inspector_reflection_ : nullptr;
		if (!nested) {
			const auto found = component_reflection_.find(command.instance_id);
			if (found != component_reflection_.end())
				reflection = &found->second;
		}
		if (!reflection || !target || (!nested && !safe_object_alive(target)) || command.member_index < 0) {
			set_status("Member is no longer available");
			return;
		}

		Inspect::ValueInfo value{};
		Inspect::ObjectHandle argument_root{};
		bool written = false;
		const bool keep_locked = !prepared && lock_key != 0 && (command.lock_value || locked_members_.contains(lock_key));
		auto apply_member = [&](const auto& member) {
			constexpr bool is_property = requires { member.can_write; };
			if (member.type_is_opaque) {
				record_flight("BLOCKED", std::string(is_property ? "Write property " : "Write field ") + member.name,
					"runtime-specific type");
				set_status(std::string(is_property ? "Property " : "Field ") + member.name +
					" is metadata-only: " + member.type_name);
				return;
			}
			record_flight("TARGET", std::string(is_property ? "Write property " : "Write field ") + member.name,
				member.declaring_type.full_name.empty() ? member.type_name
					: member.declaring_type.full_name + " : " + member.type_name);
			if (prepared) {
				value = *prepared;
			}
			else {
				const bool parsed = command_value(member.type_name, command, value) ||
					(member.is_enum && enum_value_from_text(member.type_name, command.text, value)) ||
					(!member.is_enum && reference_value_from_text(member.type_name, member.type,
						command.text, value, argument_root));
				if (!parsed) {
					if (!member.is_enum) {
						set_status(std::string("Invalid reference for ") + (is_property ? "property " : "field ") +
							member.name + ": expected " + member.type_name +
							" (paste Copy Ptr; 0x prefix is optional)");
					}
					else {
						set_status(std::string("Unsupported ") + (is_property ? "property" : "field") +
							" type: " + member.type_name);
					}
					return;
				}
				if (keep_locked && value.kind == Inspect::ValueKind::String && !value.object) {
					value.object =
						URK::il2cpp::string_new_len(value.display.data(), static_cast<std::uint32_t>(value.display.size()));
					if (value.object)
						argument_root = Inspect::PinValue(value);
					if (!argument_root.handle) {
						capture_last_error("Lock string value");
						value.object = nullptr;
						return;
					}
				}
			}

#if defined(_WIN32)
			__try {
#endif
				if constexpr (is_property)
					written = Inspect::SetProperty(target, member, value);
				else
					written = Inspect::SetField(target, member, value);
#if defined(_WIN32)
			}
			__except (capture_native_fault(_exception_info())) {
				Inspect::FreeObjectHandle(argument_root);
				clear_error();
				set_status(std::string("Set ") + (is_property ? "property" : "field") +
					" blocked an invalid native access");
				return;
			}
#endif

			if (written && verify) {
				if constexpr (is_property) {
					if (member.can_read) {
						const Inspect::ValueInfo actual =
							guarded_managed_read(member.type_name, [&] { return Inspect::ReadProperty(target, member); });
						if (values_equivalent(value, actual))
							set_status("Set property " + member.name + " (verified)");
						else
							set_status("Write verification failed for property " + member.name + ": requested " +
								value.display + ", read " + actual.display);
					}
					else {
						set_status("Set write-only property " + member.name + " (not verifiable)");
					}
				}
				else {
					const Inspect::ValueInfo actual =
						guarded_managed_read(member.type_name, [&] { return Inspect::ReadField(target, member); });
					if (values_equivalent(value, actual))
						set_status("Set field " + member.name + " (verified)");
					else
						set_status("Write verification failed for field " + member.name + ": requested " + value.display +
							", read " + actual.display);
				}
			}

			if (written && keep_locked) {
				const auto old = locked_members_.find(lock_key);
				if (old != locked_members_.end())
					Inspect::FreeObjectHandle(old->second.value_root);
				LockedMember locked{};
				locked.command = command;
				locked.command.lock_value = false;
				locked.command.unlock_value = false;
				locked.value = value;
				locked.value_root = argument_root;
				argument_root = {};
				locked_members_[lock_key] = std::move(locked);
				working_.locked_member_keys.insert(lock_key);
				set_status(std::string(is_property ? "Property " : "Field ") + member.name + " locked");
			}
			};

		const std::size_t index = static_cast<std::size_t>(command.member_index);
		if (property) {
			if (index >= reflection->properties.size()) {
				set_status("Property is no longer available");
				return;
			}
			apply_member(reflection->properties[index]);
		}
		else {
			if (index >= reflection->fields.size()) {
				set_status("Field is no longer available");
				return;
			}
			apply_member(reflection->fields[index]);
		}
		Inspect::FreeObjectHandle(argument_root);
		if (written && nested && working_.object_inspector.is_value_type &&
			working_.object_inspector.value_origin_component_id != 0) {
			write_back_value_type_object_inspector();
		}
		if (written) {
			// Refresh immediately so the next snapshot includes the written value.
			if (nested)
				refresh_object_inspector_values(true);
			else
				refresh_live_member_values(true);
		}
		else {
			capture_last_error(property ? "Set property" : "Set field");
		}
	}

	void RuntimeModel::apply_locked_members() {
		clear_locked_members();
	}

	void RuntimeModel::clear_locked_members(bool nested_only) {
		for (auto it = locked_members_.begin(); it != locked_members_.end();) {
			if (nested_only && !it->second.command.object_inspector_target) {
				++it;
				continue;
			}
			Inspect::FreeObjectHandle(it->second.value_root);
			working_.locked_member_keys.erase(it->first);
			it = locked_members_.erase(it);
		}
	}

	void RuntimeModel::write_back_value_type_object_inspector() {
		const ObjectInspectorInfo& inspector = working_.object_inspector;
		const Object parent = resolve_component(inspector.value_origin_component_id);
		const Object boxed = Inspect::ResolveObjectHandle(object_inspector_handle_);
		const auto reflection = component_reflection_.find(inspector.value_origin_component_id);
		if (!parent || !boxed || reflection == component_reflection_.end() || inspector.value_origin_member_index < 0) {
			set_status("Value-type owner is no longer available");
			return;
		}
		bool written = false;
#if defined(_WIN32)
		__try {
#endif
			if (inspector.value_origin_property) {
				const std::size_t index = static_cast<std::size_t>(inspector.value_origin_member_index);
				if (index < reflection->second.properties.size())
					written = Inspect::SetPropertyFromBox(parent, reflection->second.properties[index], boxed);
			}
			else {
				const std::size_t index = static_cast<std::size_t>(inspector.value_origin_member_index);
				if (index < reflection->second.fields.size())
					written = Inspect::SetFieldFromBox(parent, reflection->second.fields[index], boxed);
			}
#if defined(_WIN32)
		}
		__except (capture_native_fault(_exception_info())) {
			clear_error();
			set_status("Value-type write-back blocked an invalid native access");
			return;
		}
#endif
		if (written)
			set_status("Applied value-type change to owner");
		else
			capture_last_error("Apply value-type change");
	}

	void RuntimeModel::invoke_method(const Command& command) {
		const bool nested = command.object_inspector_target;
		if (nested && (!working_.object_inspector.valid || command.object_inspector_token == 0 ||
			command.object_inspector_token != working_.object_inspector.token)) {
			set_status("Object Inspector tab changed before the method could be invoked");
			return;
		}
		const ComponentReflection* reflection = nested ? &object_inspector_reflection_ : nullptr;
		if (!nested) {
			const auto found = component_reflection_.find(command.instance_id);
			if (found != component_reflection_.end())
				reflection = &found->second;
		}
		if (!reflection || command.member_index < 0 ||
			static_cast<std::size_t>(command.member_index) >= reflection->methods.size()) {
			set_status("Method is no longer available");
			return;
		}
		const Inspect::MethodInfo& method = reflection->methods[command.member_index];
		if (method.return_type_is_opaque || std::any_of(method.parameters.begin(), method.parameters.end(),
			[](const Inspect::MethodParamInfo& parameter) { return parameter.is_opaque; })) {
			record_flight("BLOCKED", "Execute " + method.name, "runtime-specific signature");
			set_status("Method is metadata-only because its signature needs runtime-specific marshalling");
			return;
		}
		record_flight("TARGET", "Execute " + method.name,
			method.declaring_type.full_name.empty() ? method.return_type
				: method.declaring_type.full_name + " -> " + method.return_type);
		const Object target =
			nested ? Inspect::ResolveObjectHandle(object_inspector_handle_) : resolve_component(command.instance_id);
		if (!method.is_static && (!target || (!nested && !safe_object_alive(target)))) {
			set_status("Method target is no longer available");
			return;
		}
		if (command.method_arguments.size() != method.parameters.size()) {
			set_status("Method argument count does not match");
			return;
		}
		std::vector<Inspect::ValueInfo> arguments;
		std::vector<Inspect::ObjectHandle> roots;
		arguments.reserve(method.parameters.size());
		roots.reserve(method.parameters.size());
		const auto release_roots = [&] {
			for (Inspect::ObjectHandle& root : roots)
				Inspect::FreeObjectHandle(root);
			roots.clear();
			};
		for (std::size_t index = 0; index < method.parameters.size(); ++index) {
			const Inspect::MethodParamInfo& parameter = method.parameters[index];
			Command argument_command{};
			argument_command.text = command.method_arguments[index];
			const std::string normalized = normalized_type(parameter.type_name);
			argument_command.bool_value =
				normalized == "system.boolean" && (argument_command.text == "true" || argument_command.text == "1");
			Inspect::ValueInfo value{};
			if (command_value(parameter.type_name, argument_command, value)) {
				arguments.push_back(std::move(value));
				continue;
			}
			const Inspect::TypeInfo parameter_type = Inspect::DescribeType(parameter.type);
			if (parameter_type.is_enum && enum_value_from_text(parameter.type_name, argument_command.text, value)) {
				arguments.push_back(std::move(value));
				continue;
			}
			Inspect::ObjectHandle rooted{};
			if (!parameter_type.is_enum &&
				reference_value_from_text(parameter.type_name, parameter.type, argument_command.text, value, rooted)) {
				if (rooted.handle)
					roots.push_back(rooted);
				arguments.push_back(std::move(value));
				continue;
			}
			if (rooted.handle)
				Inspect::FreeObjectHandle(rooted);
			{
				release_roots();
				set_status("Invalid reference argument " + std::to_string(index + 1) + ": expected " + parameter.type_name +
					" (paste Copy Ptr; 0x prefix is optional)");
				return;
			}
		}
		const Inspect::ValueInfo result =
			guarded_managed_read(method.return_type, [&] { return Inspect::InvokeMethod(target, method, arguments); });
		release_roots();
		if (!result.readable) {
			const char* error = last_error();
			if (error && is_expected_empty_container_error(error)) {
				set_status("Method skipped: map is empty");
				return;
			}
			capture_last_error("Invoke method");
			return;
		}
		if (!nested) {
			const auto component = std::find_if(working_.inspector.components.begin(), working_.inspector.components.end(),
				[&command](const ComponentInfo& info) { return info.instance_id == command.instance_id; });
			if (component != working_.inspector.components.end() &&
				component->dynamic_bridge.type_getter_method_index == command.member_index) {
				if (result.kind == Inspect::ValueKind::String)
					component->dynamic_bridge.behaviour_type = result.display;
				else
					component->dynamic_bridge.diagnostic = "Type getter completed but did not return a managed string.";
			}
		}
		Snapshot::MethodResult record{};
		record.component_instance_id = command.instance_id;
		record.method_index = static_cast<std::size_t>(command.member_index);
		record.object_inspector_token = nested ? command.object_inspector_token : 0;
		record.return_type = method.return_type;
		record.display = result.display.empty() ? "<no display value>" : result.display;
		for (auto it = working_.method_results.begin(); it != working_.method_results.end();) {
			const Snapshot::MethodResult& previous = it->second;
			if (previous.component_instance_id != record.component_instance_id ||
				previous.method_index != record.method_index ||
				previous.object_inspector_token != record.object_inspector_token) {
				++it;
				continue;
			}
			if (previous.reference.token != 0)
				release_reference_handle(previous.reference.token);
			it = working_.method_results.erase(it);
		}
		const bool is_reference = result.kind == Inspect::ValueKind::ObjectReference ||
			result.kind == Inspect::ValueKind::ArrayReference || result.kind == Inspect::ValueKind::String;
		if (is_reference && result.object) {
			// Allocate a token that cannot clash with a component/member reference.
			std::uint64_t reference_token = 0;
			do {
				reference_token = 0x1000000000000000ull | (next_reference_token_++ & 0x0fffffffffffffffull);
			} while (reference_handles_.contains(reference_token));
			Inspect::ObjectHandle rooted = tracked_reference_handle(result);
			const Object returned = Inspect::ResolveObjectHandle(rooted);
			if (rooted.handle && returned) {
				reference_handles_[reference_token] = rooted;
				record.reference = { reference_token, result.type_name, result.display, pointer_text(returned.handle()),
									false };
			}
			else {
				Inspect::FreeObjectHandle(rooted);
				record.display += " (object could not be retained for inspection)";
			}
		}
		working_.method_results[next_method_result_id_++] = std::move(record);
		set_status("Invoked " + method.name + " -> " + result.display);
	}

	void RuntimeModel::delete_component(int component_instance_id) {
		const Object component = resolve_component(component_instance_id);
		if (!safe_object_alive(component)) {
			set_status("Component is no longer available");
			return;
		}
		Object::Destroy(component);
		capture_last_error("Delete component");
		request_refresh();
		event_refresh_pending_ = true;
		event_refresh_due_ = Clock::now() + kEventRefreshDebounce;
	}

	void RuntimeModel::set_method_trace(const Command& command) {
		if (!command.bool_value && command.reference_token != 0) {
			if (MethodTracer::stop(command.reference_token))
				set_status("Method tracing stopped");
			else
				set_status("Method trace is no longer available");
			return;
		}
		const bool nested = command.object_inspector_target;
		if (nested && (!working_.object_inspector.valid || command.object_inspector_token == 0 ||
			command.object_inspector_token != working_.object_inspector.token)) {
			set_status("Object Inspector tab changed before tracing could start");
			return;
		}
		const ComponentReflection* reflection = nested ? &object_inspector_reflection_ : nullptr;
		if (!nested) {
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
			if (MethodTracer::stop(static_cast<const URK::il2cpp::Method*>(method.handle)))
				set_status("Method tracing stopped");
			else
				set_status("Method trace is no longer active");
			return;
		}
		std::string error;
		remember_managed_method(method);
		if (!MethodTracer::start(method, error)) {
			set_status("Method tracing failed: " + error);
			return;
		}
		set_status("Tracing " + method.declaring_type.full_name + "." + method.name + " (all calls)");
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
			set_status("Cannot watch an invalid field");
			return;
		}
		const std::size_t field_index = static_cast<std::size_t>(command.member_index);
		const auto component = std::find_if(working_.inspector.components.begin(), working_.inspector.components.end(),
			[&command](const ComponentInfo& info) {
				return info.instance_id == command.instance_id;
			});
		const auto reflection = component_reflection_.find(command.instance_id);
		const Object target = resolve_component(command.instance_id);
		if (component == working_.inspector.components.end() || !component->metadata ||
			reflection == component_reflection_.end() || !target || field_index >= component->metadata->fields.size() ||
			field_index >= reflection->second.fields.size()) {
			set_status("Field is no longer available to watch");
			return;
		}

		auto found = std::find_if(field_watches_.begin(), field_watches_.end(), [&](const auto& entry) {
			return entry.second.snapshot.component_instance_id == command.instance_id &&
				entry.second.snapshot.field_index == field_index;
			});
		if (!command.bool_value) {
			if (found == field_watches_.end() || !found->second.snapshot.active) {
				set_status("Field watch is no longer active");
				return;
			}
			found->second.snapshot.active = false;
			set_status("Field watch stopped");
			return;
		}

		FieldWatchState* state = nullptr;
		if (found == field_watches_.end()) {
			FieldWatchState created{};
			created.snapshot.id = next_field_watch_id_++;
			created.snapshot.component_instance_id = command.instance_id;
			created.snapshot.field_index = field_index;
			created.snapshot.component_type = component->type_name;
			created.snapshot.field_name = component->metadata->fields[field_index].name;
			created.snapshot.field_type = component->metadata->fields[field_index].type_name;
			const auto inserted = field_watches_.emplace(created.snapshot.id, std::move(created));
			state = &inserted.first->second;
		}
		else {
			state = &found->second;
			state->snapshot.active = true;
			release_field_watch_references(state->snapshot);
			state->snapshot.events.clear();
			state->snapshot.change_count = 0;
		}

		const Inspect::ValueInfo value = guarded_managed_read(state->snapshot.field_type, [&] {
			return Inspect::ReadField(target, reflection->second.fields[field_index]);
			});
		state->started = Clock::now();
		state->last_value = value;
		state->has_baseline = value.readable;
		state->snapshot.active = true;
		state->snapshot.value_available = value.readable;
		state->snapshot.current_value = watched_value_display(value);
		state->snapshot.current_reference = watch_reference_for(value);
		sampled_component_members_.insert(component_sample_token(command.instance_id, false, field_index));
		set_status("Watching " + state->snapshot.component_type + "." + state->snapshot.field_name +
			" for value changes");
	}

	void RuntimeModel::clear_field_watch(std::uint64_t id) {
		const auto found = field_watches_.find(id);
		if (found == field_watches_.end()) {
			set_status("Field watch is no longer available");
			return;
		}
		release_field_watch_references(found->second.snapshot);
		found->second.snapshot.events.clear();
		found->second.snapshot.change_count = 0;
		found->second.has_baseline = false;
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
		release_field_watch_references(found->second.snapshot);
		field_watches_.erase(found);
		set_status("Closed field watch tab");
	}

	void RuntimeModel::refresh_field_watches() {
		constexpr std::size_t kMaxFieldWatchEvents = 256;
		const Clock::time_point now = Clock::now();
		for (auto& [_, state] : field_watches_) {
			Snapshot::FieldWatch& watch = state.snapshot;
			if (!watch.active)
				continue;
			const auto reflection = component_reflection_.find(watch.component_instance_id);
			const Object target = resolve_component(watch.component_instance_id);
			if (reflection == component_reflection_.end() || !target ||
				watch.field_index >= reflection->second.fields.size()) {
				watch.active = false;
				watch.value_available = false;
				watch.current_value = "<component is no longer available>";
				set_status("Field watch stopped because its component was released");
				continue;
			}
			const Inspect::ValueInfo value = guarded_managed_read(watch.field_type, [&] {
				return Inspect::ReadField(target, reflection->second.fields[watch.field_index]);
				});
			watch.value_available = value.readable;
			watch.current_value = watched_value_display(value);
			if (!value.readable) {
				record_value_error(watch.component_type + "." + watch.field_name, value);
				continue;
			}
			if (!state.has_baseline) {
				state.last_value = value;
				state.has_baseline = true;
				state.started = now;
				release_reference_handle(watch.current_reference.token);
				watch.current_reference = watch_reference_for(value);
				continue;
			}
			if (values_equivalent(state.last_value, value))
				continue;
			Snapshot::FieldWatchEvent event{};
			event.sequence = ++watch.change_count;
			event.seconds_since_start = std::chrono::duration<double>(now - state.started).count();
			event.previous_value = watched_value_display(state.last_value);
			event.current_value = watched_value_display(value);
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

	void RuntimeModel::inspect_raw_reference(std::uint64_t address) {
		if (address == 0) {
			set_status("Cannot inspect a null reference");
			return;
		}
		Inspect::ValueInfo value{};
		value.kind = Inspect::ValueKind::ObjectReference;
		value.object = reinterpret_cast<void*>(static_cast<std::uintptr_t>(address));
		const ComponentInfo::LiveValues::Reference reference = watch_reference_for(value);
		if (reference.is_null || reference.token == 0) {
			set_status("That native address is not a live managed object");
			return;
		}
		inspect_reference(reference.token);
	}

	void RuntimeModel::inspect_reference(std::uint64_t token) {
		const auto retained = object_inspector_history_.find(token);
		const auto reference = reference_handles_.find(token);
		const auto browser_reference = class_browser_handles_.find(token);
		const auto static_reference = class_browser_static_handles_.find(token);
		if (retained == object_inspector_history_.end() && reference == reference_handles_.end() &&
			browser_reference == class_browser_handles_.end() && static_reference == class_browser_static_handles_.end()) {
			set_status("Referenced object is no longer available");
			return;
		}
		const Inspect::ObjectHandle& source = retained != object_inspector_history_.end() ? retained->second
			: reference != reference_handles_.end() ? reference->second
			: browser_reference != class_browser_handles_.end()
			? browser_reference->second
			: static_reference->second;
		const Object object = Inspect::ResolveObjectHandle(source);
		if (!object) {
			set_status("Referenced object was released");
			return;
		}
		const Inspect::TypeInfo type = safe_type_of(object);
		if (type.full_name == "UnityEngine.GameObject") {
			// GameObjects have a dedicated hierarchy/Inspector flow.  Sending one
			// through the generic Object Inspector only reflects the GameObject
			// class itself, which hides the referenced instance's name, components,
			// and scene context.
			Inspect::ObjectHandle selection_root = Inspect::PinObject(object);
			if (!selection_root.handle) {
				set_status("Could not root referenced GameObject");
				return;
			}
			const GameObject game_object{ object.handle() };
			select_object(game_object, selection_root);
			if (selected_.handle() == object.handle()) {
				refresh_inspector(true);
				update_highlight();
				set_status("Selected referenced GameObject");
			}
			return;
		}
		Inspect::ObjectHandle next_handle = type.is_value_type ? Inspect::PinObject(object) : Inspect::WeakObject(object);
		if (!next_handle.handle) {
			set_status("Could not track referenced object");
			return;
		}
		if (retained == object_inspector_history_.end()) {
			Inspect::ObjectHandle history_handle =
				type.is_value_type ? Inspect::PinObject(object) : Inspect::WeakObject(object);
			if (!history_handle.handle) {
				Inspect::FreeObjectHandle(next_handle);
				set_status("Could not retain object for an inspector tab");
				return;
			}
			object_inspector_history_[token] = history_handle;
		}
		Inspect::FreeObjectHandle(object_inspector_handle_);
		object_inspector_handle_ = next_handle;
		sampled_object_fields_.clear();
		sampled_object_properties_.clear();
		const Object rooted = Inspect::ResolveObjectHandle(object_inspector_handle_);
		if (!rooted) {
			set_status("Referenced object could not be resolved");
			return;
		}

		ObjectInspectorInfo inspector{};
		inspector.valid = true;
		inspector.token = token;
		inspector.type_name = type.full_name.empty() ? "<object>" : type.full_name;
		inspector.assembly_name = assembly_name(type);
		inspector.namespace_name = type.namespc;
		inspector.class_name = type.name;
		inspector.pointer_text = pointer_text(rooted.handle());
		inspector.is_value_type = type.is_value_type;
		inspector.is_array = Inspect::type_name_looks_array(inspector.type_name);
		if (inspector.is_array)
			inspector.array_element_type = Inspect::array_element_type_name(inspector.type_name);
		const int origin_component_id = static_cast<int>(static_cast<std::uint32_t>(token >> 32));
		const bool origin_property = (token & 0x80000000ull) != 0;
		const int origin_member_index = static_cast<int>(token & 0x0fffffffull);
		if (origin_component_id != 0) {
			const auto origin_reflection = component_reflection_.find(origin_component_id);
			if (origin_reflection != component_reflection_.end()) {
				const bool value_origin =
					origin_property
					? origin_member_index >= 0 &&
					static_cast<std::size_t>(origin_member_index) < origin_reflection->second.properties.size() &&
					origin_reflection->second.properties[origin_member_index].is_value_type
					: origin_member_index >= 0 &&
					static_cast<std::size_t>(origin_member_index) < origin_reflection->second.fields.size() &&
					origin_reflection->second.fields[origin_member_index].is_value_type;
				if (value_origin) {
					inspector.value_origin_component_id = origin_component_id;
					inspector.value_origin_member_index = origin_member_index;
					inspector.value_origin_property = origin_property;
				}
			}
		}
		inspector.component.instance_id = 0;
		inspector.component.type_name = inspector.type_name;
		inspector.component.assembly_name = inspector.assembly_name;
		inspector.component.namespace_name = inspector.namespace_name;
		inspector.component.class_name = inspector.class_name;

		if (inspector.is_array) {
			working_.object_inspector = std::move(inspector);
			refresh_object_inspector_values();
			set_status("Inspecting array " + working_.object_inspector.type_name);
			return;
		}

		ComponentReflection reflection{};
		reflection.fields = Inspect::Fields(rooted, true);
		reflection.properties = Inspect::Properties(rooted, true);
		reflection.methods = Inspect::Methods(rooted, true);
		auto metadata = std::make_shared<ComponentInfo::Metadata>();
		metadata->fields.reserve(reflection.fields.size());
		for (const Inspect::FieldInfo& field : reflection.fields) {
			metadata->fields.push_back({ field.name, field.type_name, field.declaring_type.full_name, field.is_static,
				{}, field.is_value_type, field.is_enum, !field.type_is_opaque,
				field.type_is_opaque ? "Runtime-specific type; metadata is available but generic read/write is unsafe." : "" });
		}
		metadata->properties.reserve(reflection.properties.size());
		for (const Inspect::PropertyInfo& property : reflection.properties) {
			metadata->properties.push_back({ property.name, property.type_name, property.declaring_type.full_name,
									 property.can_read, property.can_write, {}, property.is_value_type, property.is_enum,
									 !property.type_is_opaque,
									 property.type_is_opaque ? "Runtime-specific type; metadata is available but generic read/write is unsafe." : "" });
		}
		metadata->methods.reserve(reflection.methods.size());
		for (const Inspect::MethodInfo& method : reflection.methods) {
			ComponentInfo::Method member{};
			member.name = method.name;
			member.return_type = method.return_type;
			member.declaring_type = method.declaring_type.full_name;
			member.is_static = method.is_static;
			member.return_is_value_type = method.return_is_value_type;
			member.return_is_enum = method.return_is_enum;
			member.runtime_callable = !method.return_type_is_opaque &&
				std::none_of(method.parameters.begin(), method.parameters.end(), [](const Inspect::MethodParamInfo& parameter) {
					return parameter.is_opaque;
					});
			if (!member.runtime_callable)
				member.capability_reason = "Runtime-specific by-ref/internal signature; generic invocation is disabled.";
			for (const Inspect::MethodParamInfo& parameter : method.parameters) {
				member.parameter_types.push_back(parameter.type_name);
				member.parameter_names.push_back(parameter.name);
				member.parameter_is_value_types.push_back(parameter.is_value_type);
				member.parameter_is_enums.push_back(parameter.is_enum);
			}
			metadata->methods.push_back(std::move(member));
		}
		inspector.component.metadata = std::move(metadata);
		object_inspector_reflection_ = std::move(reflection);
		working_.object_inspector = std::move(inspector);
		refresh_object_inspector_values();
		set_status("Inspecting " + working_.object_inspector.type_name);
	}

	void RuntimeModel::refresh_object_inspector_values(bool force) {
		if ((!force && event_refresh_pending_) || !working_.object_inspector.valid || !object_inspector_handle_.handle)
			return;
		const Object object = Inspect::ResolveObjectHandle(object_inspector_handle_);
		if (!object) {
			working_.object_inspector = {};
			set_status("Object Inspector target was released; inspector closed safely");
			return;
		}
		if (working_.object_inspector.is_array) {
			auto values = std::make_shared<ComponentInfo::LiveValues>();
			Inspect::ValueInfo array{};
			array.kind = Inspect::ValueKind::ArrayReference;
			array.type_name = working_.object_inspector.type_name;
			array.object = object.handle();
			array.readable = true;
			const std::size_t length = Inspect::ArrayLength(array);
			const std::size_t offset = length == 0 ? 0
				: std::min(working_.object_inspector.array_offset,
					((length - 1) / kArrayPageSize) * kArrayPageSize);
			const std::size_t sampled = std::min(kArrayPageSize, length - offset);
			working_.object_inspector.array_offset = offset;
			values->fields.resize(sampled);
			values->field_references.resize(sampled);
			for (auto it = reference_handles_.begin(); it != reference_handles_.end();) {
				if ((it->first & 0xF000000000000000ull) == 0x3000000000000000ull) {
					if (it->second.handle) {
						Inspect::FreeObjectHandle(it->second);
					}
					it = reference_handles_.erase(it);
				}
				else {
					++it;
				}
			}
			for (std::size_t row = 0; row < sampled; ++row) {
				const std::size_t index = offset + row;
				values->fields[row] = guarded_managed_read(working_.object_inspector.array_element_type,
					[&] { return Inspect::ReadArrayElement(array, index); });
				Inspect::ValueInfo& value = values->fields[row];
				record_value_error(working_.object_inspector.type_name + "[" + std::to_string(index) + "]", value);
				if ((value.kind == Inspect::ValueKind::ObjectReference ||
					value.kind == Inspect::ValueKind::ArrayReference) &&
					value.object) {
					const std::uint64_t token = 0x3000000000000000ull | index;
					Inspect::ObjectHandle rooted = tracked_reference_handle(value);
					if (rooted.handle) {
						const Object tracked = Inspect::ResolveObjectHandle(rooted);
						if (tracked) {
							reference_handles_[token] = rooted;
							values->field_references[row] = { token, value.type_name, value.display,
															 pointer_text(tracked.handle()), false };
							value.object = nullptr;
						}
						else {
							Inspect::FreeObjectHandle(rooted);
							value = Inspect::unavailable_value(value.type_name, "array element was released");
						}
					}
					else {
						value = Inspect::unavailable_value(value.type_name, "could not track array element");
					}
				}
			}
			working_.object_inspector.array_length = length;
			working_.object_inspector.array_values = std::move(values);
			return;
		}
		if (!working_.object_inspector.component.metadata)
			return;
		const ComponentInfo::Metadata& metadata = *working_.object_inspector.component.metadata;
		auto values = std::make_shared<ComponentInfo::LiveValues>();
		values->fields.resize(metadata.fields.size());
		values->properties.resize(metadata.properties.size());
		values->field_references.resize(metadata.fields.size());
		values->property_references.resize(metadata.properties.size());
		auto capture_reference = [&](Inspect::ValueInfo& value, std::uint64_t token,
			ComponentInfo::LiveValues::Reference& reference) {
				if ((value.kind != Inspect::ValueKind::ObjectReference && value.kind != Inspect::ValueKind::ArrayReference) ||
					!value.object) {
					release_reference_handle(token);
					value.object = nullptr;
					return;
				}
				if (const auto previous = reference_handles_.find(token); previous != reference_handles_.end()) {
					const Object current = Inspect::ResolveObjectHandle(previous->second);
					if (current && current.handle() == value.object) {
						reference = { token, value.type_name, value.display, pointer_text(current.handle()), false };
						value.object = nullptr;
						return;
					}
				}
				Inspect::ObjectHandle rooted = tracked_reference_handle(value);
				if (!rooted.handle) {
					value = Inspect::unavailable_value(value.type_name, "could not track referenced object");
					return;
				}
				const Object tracked = Inspect::ResolveObjectHandle(rooted);
				if (!tracked) {
					Inspect::FreeObjectHandle(rooted);
					value = Inspect::unavailable_value(value.type_name, "referenced object was released");
					return;
				}
				release_reference_handle(token);
				reference_handles_[token] = rooted;
				reference = { token, value.type_name, value.display, pointer_text(tracked.handle()), false };
				value.object = nullptr;
			};
		for (std::size_t index = 0; index < values->fields.size(); ++index) {
			const bool sampled = sampled_object_fields_.contains(index);
			values->fields[index] =
				index < object_inspector_reflection_.fields.size() && sampled
				? guarded_managed_read(
					metadata.fields[index].type_name,
					[&] { return Inspect::ReadField(object, object_inspector_reflection_.fields[index]); })
				: Inspect::unavailable_value(metadata.fields[index].type_name, "Not sampled");
			record_value_error(working_.object_inspector.type_name + "." + metadata.fields[index].name,
				values->fields[index]);
			capture_reference(values->fields[index], 0x4000000000000000ull | index, values->field_references[index]);
		}
		for (std::size_t index = 0; index < values->properties.size(); ++index) {
			const bool sampled = sampled_object_properties_.contains(index);
			values->properties[index] =
				index < object_inspector_reflection_.properties.size() && sampled
				? guarded_managed_read(
					metadata.properties[index].type_name,
					[&] { return Inspect::ReadProperty(object, object_inspector_reflection_.properties[index]); })
				: Inspect::unavailable_value(metadata.properties[index].type_name, "Not sampled");
			record_value_error(working_.object_inspector.type_name + "." + metadata.properties[index].name,
				values->properties[index]);
			capture_reference(values->properties[index], 0x5000000000000000ull | index, values->property_references[index]);
		}
		working_.object_inspector.component.live_values = std::move(values);
	}

	GameObject RuntimeModel::resolve_live_game_object(
		int instance_id,
		Inspect::ObjectHandle& root) const {

		if (instance_id == 0)
			return {};

		const auto candidate_instance_id =
			[](GameObject candidate) -> int {

			if (!candidate ||
				!readable_address(
					reinterpret_cast<std::uintptr_t>(
						candidate.handle()))) {
				return 0;
			}

#if defined(_WIN32)
			__try {
				return candidate.GetInstanceID();
			}
			__except (capture_native_fault(_exception_info())) {
				clear_error();
				return 0;
			}
#else
			return candidate.GetInstanceID();
#endif
			};

		const auto root_candidate =
			[&](GameObject candidate) -> GameObject {

			if (candidate_instance_id(candidate) != instance_id)
				return {};

			Inspect::ObjectHandle candidate_root{};

#if defined(_WIN32)
			__try {
				candidate_root =
					Inspect::PinObject(
						Object{ candidate.handle() });
			}
			__except (capture_native_fault(_exception_info())) {
				clear_error();
				return {};
			}
#else
			candidate_root =
				Inspect::PinObject(
					Object{ candidate.handle() });
#endif

			if (!candidate_root.handle)
				return {};

			root = candidate_root;
			return candidate;
			};

		// Do not retain the complete FindObjectsOfTypeAll result after a census.
		// Pin only the clicked object and release this temporary managed array.
		const auto candidates = Object::FindObjectsOfTypeAllRooted<GameObject>();
		for (const GameObject& candidate : candidates) {

			if (candidate_instance_id(candidate) == instance_id)
				return root_candidate(candidate);
		}

		return {};
	}

	GameObject RuntimeModel::resolve_selected_object() const {
		// selected_handle_ keeps selected_ alive; reuse the validated wrapper.
		if (!selected_handle_.handle || !selected_)
			return {};
		return selected_;
	}

	Object RuntimeModel::resolve_component(int instance_id) const {
		if (const auto pinned = component_handles_.find(instance_id);
			pinned != component_handles_.end() && pinned->second.handle) {
			return Inspect::ResolveObjectHandle(pinned->second);
		}
		return {};
	}

	void RuntimeModel::select_object(GameObject object, Inspect::ObjectHandle root) {
		if (!safe_object_alive(object)) {
			Inspect::FreeObjectHandle(root);
			clear_selection();
			return;
		}
		const int instance_id = object.GetInstanceID();
		if (working_.selected_instance_id == instance_id) {
			Inspect::FreeObjectHandle(root);
			return;
		}
		// A new selection ends a temporary camera focus so the previous camera
		// pose is not carried into an unrelated object.
		if (camera_focus_active_)
			restore_focused_camera();

		if (!root.handle) {
#if defined(_WIN32)
			__try {
				root = Inspect::PinObject(Object{ object.handle() });
			}
			__except (capture_native_fault(_exception_info())) {
				clear_error();
			}
#else
			root = Inspect::PinObject(Object{ object.handle() });
#endif
		}
		if (!root.handle) {
			set_status("Selection failed: GameObject could not be rooted");
			return;
		}

		if (highlight_id_ != 0)
			ModUI::Highlight::enqueue_remove(highlight_id_);
		if (highlight_locator_id_ != 0)
			ModUI::Highlight::enqueue_remove(highlight_locator_id_);
		// Clear IDs immediately; queued removal runs before the next draw.
		highlight_id_ = 0;
		highlight_locator_id_ = 0;

		Inspect::FreeObjectHandle(selected_handle_);
		selected_handle_ = root;
		selected_ = object;
		working_.selected_instance_id = instance_id;
		working_.inspector = {};
		clear_highlight_renderer_cache();
	}

	void RuntimeModel::update_camera_transition(Clock::time_point now) {
		if (!camera_focus_active_ || !focused_camera_handle_.handle)
			return;
		const Camera camera{ Inspect::ResolveObjectHandle(focused_camera_handle_).handle() };
		const Transform camera_transform = camera.transform();
		if (!safe_object_alive(camera_transform)) {
			camera_transition_active_ = false;
			return;
		}

		if (camera_transition_active_) {
			const float elapsed = std::chrono::duration<float>(now - camera_transition_started_).count();
			const float duration = std::chrono::duration<float>(kCameraFocusTransition).count();
			const float linear_amount = duration > 0.0f ? std::clamp(elapsed / duration, 0.0f, 1.0f) : 1.0f;
			const float amount = linear_amount * linear_amount * (3.0f - 2.0f * linear_amount);
			camera_transform.set_position(camera_transition_start_position * (1.0f - amount) +
				camera_transition_target_position * amount);
			camera_transform.set_localRotation(
				quaternion_slerp(camera_transition_start_rotation, camera_transition_target_rotation, amount));
			if (linear_amount < 1.0f)
				return;
			camera_transition_active_ = false;
		}

		// The game's camera controller may write its own local pose after the
		// initial transition (Heartophia does this for the player camera). Keep
		// the focus pose authoritative until the user explicitly restores the
		// camera. The target owns the focus even if the user double-clicked a
		// hierarchy row without changing the current Inspector selection.
		const GameObject target{ Inspect::ResolveObjectHandle(focused_target_handle_).handle() };
		if (!safe_object_alive(target)) {
			restore_focused_camera();
			return;
		}
		const Transform target_transform = target.transform();
		if (!safe_object_alive(target_transform)) {
			restore_focused_camera();
			return;
		}
		clear_error();
		const Vector3 target_position = target_transform.position();
		camera_transform.set_position(target_position + camera_focus_offset);
		camera_transform.LookAt(target_position);
		if (const char* error = last_error(); error && error[0]) {
			clear_error();
			ModLog::warn("Camera focus hold failed; retaining the previous camera pose: %s", error);
		}
	}

	void RuntimeModel::focus_selected_camera(GameObject object) {
		if (!safe_object_alive(object)) {
			set_status("Camera focus failed: object is no longer available");
			return;
		}
		const Transform target_transform = object.transform();
		if (!safe_object_alive(target_transform)) {
			set_status("Camera focus failed: object has no Transform");
			return;
		}
		const Vector3 target_position = target_transform.position();

		// Prefer the tagged gameplay camera. Untagged games frequently have no
		// useful Camera.current on this worker thread, so select the largest enabled
		// camera that can see the target before falling back to Camera.current.
		// Keep the discovery array alive until the chosen camera is pinned below.
		const auto focus_camera_candidates = Object::FindObjectsOfTypeRooted<Camera>();
		Camera camera = Camera::main();
		if (!safe_object_alive(camera) || !camera.enabled()) {
			camera = {};
			double best_score = -1.0;
			const double screen_area = static_cast<double>(std::max(1, Screen::width())) *
				static_cast<double>(std::max(1, Screen::height()));
			for (const Camera& candidate : focus_camera_candidates) {
				if (!safe_object_alive(candidate) || !candidate.enabled())
					continue;
				clear_error();
				const int pixel_width = candidate.pixelWidth();
				const int pixel_height = candidate.pixelHeight();
				const Vector3 viewport = candidate.WorldToViewportPoint(target_position);
				if (const char* error = last_error(); error && error[0]) {
					clear_error();
					continue;
				}
				const double pixel_area = static_cast<double>(std::max(1, pixel_width)) *
					static_cast<double>(std::max(1, pixel_height));
				const bool finite_projection = std::isfinite(viewport.x) && std::isfinite(viewport.y) &&
					std::isfinite(viewport.z);
				const bool in_front = finite_projection && viewport.z > 0.01f;
				const bool visible = in_front && viewport.x >= 0.0f && viewport.x <= 1.0f &&
					viewport.y >= 0.0f && viewport.y <= 1.0f;
				// Render area dominates so a minimap cannot steal focus merely because
				// the target happens to be visible in it.
				const double score = pixel_area * 10.0 + (in_front ? screen_area : 0.0) +
					(visible ? screen_area * 2.0 : 0.0);
				if (score > best_score) {
					best_score = score;
					camera = candidate;
				}
			}
			if (!safe_object_alive(camera) || !camera.enabled())
				camera = Camera::current();
		}
		if (!safe_object_alive(camera) || !camera.enabled()) {
			set_status("Camera focus failed: no enabled camera was found");
			return;
		}
		const Transform camera_transform = camera.transform();
		if (!safe_object_alive(camera_transform)) {
			set_status("Camera focus failed: camera Transform is unavailable");
			return;
		}

		if (camera_focus_active_)
			restore_focused_camera();
		focused_target_handle_ = Inspect::PinObject(Object{ object.handle() });
		if (!focused_target_handle_.handle) {
			set_status("Camera focus failed: target could not be rooted");
			return;
		}
		focused_camera_handle_ = Inspect::PinObject(Object{ camera.handle() });
		if (!focused_camera_handle_.handle) {
			Inspect::FreeObjectHandle(focused_target_handle_);
			focused_target_handle_ = {};
			set_status("Camera focus failed: camera could not be rooted");
			return;
		}
		const Camera rooted_camera{ Inspect::ResolveObjectHandle(focused_camera_handle_).handle() };
		const Transform rooted_transform = rooted_camera.transform();
		if (!safe_object_alive(rooted_transform)) {
			Inspect::FreeObjectHandle(focused_camera_handle_);
			focused_camera_handle_ = {};
			Inspect::FreeObjectHandle(focused_target_handle_);
			focused_target_handle_ = {};
			set_status("Camera focus failed: camera Transform is unavailable");
			return;
		}
		saved_camera_position = rooted_transform.position();
		saved_camera_rotation = rooted_transform.rotation();
		saved_camera_local_position = rooted_transform.localPosition();
		saved_camera_local_rotation = rooted_transform.localRotation();
		// The vector from an arbitrary target to the current camera says where
		// the target happens to be, not where the camera looks. Using it caused
		// side-on/sky views for off-screen objects. Preserve the camera's actual
		// viewing direction so FPS and isometric games both retain their angle.
		Vector3 view_direction = -rooted_transform.forward();
		float view_direction_length = view_direction.magnitude();
		if (!std::isfinite(view_direction_length) || view_direction_length <= 0.001f) {
			view_direction = saved_camera_position - target_position;
			view_direction_length = view_direction.magnitude();
		}
		if (!std::isfinite(view_direction_length) || view_direction_length <= 0.001f) {
			view_direction = { 0.0f, 0.0f, -1.0f };
			view_direction_length = 1.0f;
		}
		camera_focus_view_direction = view_direction / view_direction_length;
		Vector3 horizontal_offset{ camera_focus_view_direction.x, 0.0f, camera_focus_view_direction.z };
		float horizontal_length = horizontal_offset.magnitude();
		if (!std::isfinite(horizontal_length) || horizontal_length <= 0.001f) {
			const Vector3 forward = rooted_transform.forward();
			horizontal_offset = { -forward.x, 0.0f, -forward.z };
			horizontal_length = horizontal_offset.magnitude();
		}
		if (!std::isfinite(horizontal_length) || horizontal_length <= 0.001f) {
			horizontal_offset = { 0.0f, 0.0f, -1.0f };
			horizontal_length = 1.0f;
		}
		camera_focus_heading = horizontal_offset / horizontal_length;
		// Editor-like focus is deliberately above the target. Keeping this in
		// world-space prevents the former side-on pose from being pushed into
		// sloped terrain, oversized colliders, or world-space UI. A small
		// retained horizontal offset gives the view a useful perspective angle.
		camera_focus_offset = camera_focus_top_down_
			? camera_focus_heading * camera_focus_tilt_ + Vector3{ 0.0f, camera_focus_distance_, 0.0f }
			: camera_focus_view_direction * camera_focus_distance_;
		camera_transition_start_position = saved_camera_position;
		camera_transition_target_position = target_position + camera_focus_offset;
		camera_transition_start_rotation = saved_camera_local_rotation;
		rooted_transform.set_position(camera_transition_target_position);
		rooted_transform.LookAt(target_position);
		if (const char* error = last_error(); error && error[0]) {
			const std::string message = std::string("Camera focus failed: ") + error;
			clear_error();
			Inspect::FreeObjectHandle(focused_camera_handle_);
			focused_camera_handle_ = {};
			Inspect::FreeObjectHandle(focused_target_handle_);
			focused_target_handle_ = {};
			set_status(message);
			return;
		}
		camera_transition_target_rotation = rooted_transform.localRotation();
		rooted_transform.set_position(camera_transition_start_position);
		rooted_transform.set_localRotation(camera_transition_start_rotation);
		if (const char* error = last_error(); error && error[0]) {
			const std::string message = std::string("Camera focus transition failed: ") + error;
			clear_error();
			Inspect::FreeObjectHandle(focused_camera_handle_);
			focused_camera_handle_ = {};
			Inspect::FreeObjectHandle(focused_target_handle_);
			focused_target_handle_ = {};
			set_status(message);
			return;
		}
		camera_focus_active_ = true;
		working_.camera_focus_active = true;
		camera_transition_started_ = Clock::now();
		camera_transition_active_ = true;
		set_status("Camera moved to and following " + object.name() + " (use Return camera to restore it)");
	}

	void RuntimeModel::restore_focused_camera() {
		if (!camera_focus_active_ && !focused_camera_handle_.handle)
			return;
		camera_transition_active_ = false;
		bool restored = false;
		if (focused_camera_handle_.handle) {
			const Camera camera{ Inspect::ResolveObjectHandle(focused_camera_handle_).handle() };
			const Transform camera_transform = camera.transform();
			if (safe_object_alive(camera_transform)) {
				// Restore the rig-local pose. Restoring only world position/rotation
				// breaks cameras parented under a player or camera rig because their
				// controller drives localRotation every frame.
				camera_transform.set_localPosition(saved_camera_local_position);
				camera_transform.set_localRotation(saved_camera_local_rotation);
				const char* local_error = last_error();
				if (local_error && local_error[0]) {
					clear_error();
					camera_transform.set_position(saved_camera_position);
					camera_transform.set_rotation(saved_camera_rotation);
				}
				const char* restore_error = last_error();
				restored = !(restore_error && restore_error[0]);
				clear_error();
			}
			Inspect::FreeObjectHandle(focused_camera_handle_);
			focused_camera_handle_ = {};
		}
		Inspect::FreeObjectHandle(focused_target_handle_);
		focused_target_handle_ = {};
		camera_focus_active_ = false;
		working_.camera_focus_active = false;
		set_status(restored ? "Camera restored" : "Camera focus ended; the original camera was unavailable");
	}

	void RuntimeModel::update_highlight() {
		if (!highlight_enabled_) {
			if (highlight_id_ != 0) {
				ModUI::Highlight::enqueue_remove(highlight_id_);
				highlight_id_ = 0;
			}
			if (highlight_locator_id_ != 0) {
				ModUI::Highlight::enqueue_remove(highlight_locator_id_);
				highlight_locator_id_ = 0;
			}
			return;
		}
		if (!safe_object_alive(selected_))
			return;

		ModUI::Highlight::Style style{};
		style.color = IM_COL32(50, 235, 255, 255);
		style.fill_color = IM_COL32(40, 190, 255, 36);
		style.label_color = IM_COL32(255, 255, 255, 255);
		style.label_bg_color = IM_COL32(7, 16, 24, 238);
		style.label_border_color = IM_COL32(80, 220, 255, 255);
		style.shadow_color = IM_COL32(0, 0, 0, 220);
		style.corner_box = true;
		// An outline remains unambiguous without tinting the whole game when a
		// third-party renderer reports pathological bounds.
		style.filled = false;
		style.shadow = true;
		style.thickness = 3.5f;
		style.corner_length = 18.0f;
		style.draw_label = true;
		style.label_above_box = true;
		style.max_distance = highlight_max_distance_;
		// Use renderer bounds when available.
		style.offscreen_indicator = false;
		std::string label = selected_.name();

		const Transform selected_transform = selected_.transform();
		const std::string transform_type = selected_transform ? safe_runtime_class_name(selected_transform) : std::string{};
		const bool is_rect_transform = transform_type == "UnityEngine.RectTransform" ||
			transform_type == "RectTransform";
		// Canvas children require the canvas camera, not necessarily Camera.main.
		const Canvas canvas = selected_transform ? selected_transform.GetComponentInParent<Canvas>(true) : Canvas{};
		const bool is_overlay_canvas = canvas && canvas.renderMode() == CanvasRenderMode::ScreenSpaceOverlay;
		Camera camera = canvas && !is_overlay_canvas ? canvas.worldCamera() : Camera{};

		const Clock::time_point camera_now = Clock::now();
		if (camera_now >= next_highlight_camera_refresh_ || highlight_cameras_.empty()) {
			clear_highlight_camera_cache();
			const auto cameras = Object::FindObjectsOfTypeRooted<Camera>();
			for (const Camera& candidate : cameras) {
				if (safe_object_alive(candidate) && candidate.enabled()) {
					Inspect::ObjectHandle handle = Inspect::WeakObject(Object{ candidate.handle() });
					if (handle.handle)
						highlight_cameras_.push_back(handle);
				}
			}
			next_highlight_camera_refresh_ = camera_now + kHighlightCameraRefreshInterval;
		}

		Camera main_camera{};
		std::vector<Vector3> camera_samples;
		if (!camera && !is_overlay_canvas) {
			main_camera = Camera::main();
			if (!safe_object_alive(main_camera) || !main_camera.enabled())
				main_camera = Camera{};

			camera_samples.reserve(9);
			if (selected_transform)
				camera_samples.push_back(selected_transform.position());
			for (const Inspect::ObjectHandle& renderer_handle : highlight_renderers_) {
				const Renderer renderer{ Inspect::ResolveObjectHandle(renderer_handle).handle() };
				if (!safe_object_alive(renderer))
					continue;
				camera_samples.push_back(renderer.bounds().center);
				if (camera_samples.size() == 9)
					break;
			}

			// Camera.main is the camera that defines the player's screen in the
			// usual case.  Do not replace it merely because a scene/minimap camera
			// can see the object: that made off-screen locators jump to the wrong side.
			camera = main_camera;

			// Untagged games still need a fallback.  Only pick a camera that has a
			// real on-screen sample, so a hidden auxiliary camera cannot win a tie.
			if (!camera) {
				int best_visible_samples = 0;
				int best_front_samples = -1;
				float best_center_distance = std::numeric_limits<float>::max();
				for (const Inspect::ObjectHandle& camera_handle : highlight_cameras_) {
					const Camera candidate{ Inspect::ResolveObjectHandle(camera_handle).handle() };
					if (!safe_object_alive(candidate))
						continue;
					int visible_samples = 0;
					int front_samples = 0;
					float center_distance = 0.0f;
					for (const Vector3& sample : camera_samples) {
						const Vector3 viewport = candidate.WorldToViewportPoint(sample);
						if (!std::isfinite(viewport.x) || !std::isfinite(viewport.y) || !std::isfinite(viewport.z))
							continue;
						if (viewport.z > 0.01f) {
							++front_samples;
							const float dx = viewport.x - 0.5f;
							const float dy = viewport.y - 0.5f;
							center_distance += dx * dx + dy * dy;
							if (viewport.x >= 0.0f && viewport.x <= 1.0f &&
								viewport.y >= 0.0f && viewport.y <= 1.0f)
								++visible_samples;
						}
					}
					if (visible_samples > 0 && (visible_samples > best_visible_samples ||
						(visible_samples == best_visible_samples && front_samples > best_front_samples) ||
						(visible_samples == best_visible_samples && front_samples == best_front_samples &&
							center_distance < best_center_distance))) {
						camera = candidate;
						best_visible_samples = visible_samples;
						best_front_samples = front_samples;
						best_center_distance = center_distance;
					}
				}
			}
		}
		if (!camera)
			camera = main_camera ? main_camera : Camera::main();
		const int unity_screen_width = Screen::width();
		const int unity_screen_height = Screen::height();
		const float screen_width = static_cast<float>(unity_screen_width);
		const float screen_height = static_cast<float>(unity_screen_height);
		if ((!is_overlay_canvas && !camera) || unity_screen_width <= 0 || unity_screen_height <= 0 ||
			screen_width <= 0.0f || screen_height <= 0.0f)
			return;
		// Keep projection in Unity pixels; ImGui state belongs to the render hook.
		const float x_scale = 1.0f;
		const float y_scale = 1.0f;
		const auto to_draw_space = [=](const Vector3& screen) {
			return ImVec2(screen.x * x_scale, screen_height - screen.y * y_scale);
			};
		const auto valid_screen_point = [](const Vector3& point) {
			return point.z > 0.01f && std::isfinite(point.x) && std::isfinite(point.y) &&
				std::isfinite(point.z);
			};

		const Vector3 selected_position = selected_transform ? selected_transform.position() : Vector3{};
		float selected_distance = -1.0f;
		if (camera) {
			const Transform camera_transform = camera.transform();
			if (camera_transform) {
				selected_distance = Vector3::distance(selected_position, camera_transform.position());
				if (std::isfinite(selected_distance) && selected_distance >= 0.0f) {
					char distance_text[48]{};
					std::snprintf(distance_text, sizeof(distance_text), "  |  %.1f m", selected_distance);
					label += distance_text;
				}
				if (highlight_max_distance_ > 0.0f && std::isfinite(selected_distance) &&
					selected_distance > highlight_max_distance_) {
					if (highlight_id_ != 0) {
						ModUI::Highlight::enqueue_remove(highlight_id_);
						highlight_id_ = 0;
					}
					if (highlight_locator_id_ != 0) {
						ModUI::Highlight::enqueue_remove(highlight_locator_id_);
						highlight_locator_id_ = 0;
					}
					return;
				}
			}
		}
		float min_x = 0.0f;
		float min_y = 0.0f;
		float max_x = 0.0f;
		float max_y = 0.0f;
		bool projected = false;
		// Project RectTransform corners rather than its pivot position.
		const auto project_rect_transform = [&] {
			if (!is_rect_transform || !selected_transform)
				return false;
			const Rect rect = RectTransform{ selected_transform.handle() }.rect();
			if (!std::isfinite(rect.x) || !std::isfinite(rect.y) || !std::isfinite(rect.width) ||
				!std::isfinite(rect.height) || rect.width <= 0.0f || rect.height <= 0.0f)
				return false;
			const Vector3 local_corners[] = {
				{rect.x, rect.y, 0.0f},
				{rect.x, rect.y + rect.height, 0.0f},
				{rect.x + rect.width, rect.y, 0.0f},
				{rect.x + rect.width, rect.y + rect.height, 0.0f},
			};
			float rect_min_x = std::numeric_limits<float>::max();
			float rect_min_y = std::numeric_limits<float>::max();
			float rect_max_x = std::numeric_limits<float>::lowest();
			float rect_max_y = std::numeric_limits<float>::lowest();
			for (const Vector3& local_corner : local_corners) {
				const Vector3 world_corner = selected_transform.CallExact<Vector3>(
					"TransformPoint", { "UnityEngine.Vector3" }, local_corner);
				ImVec2 draw_corner{};
				if (is_overlay_canvas) {
					if (!std::isfinite(world_corner.x) || !std::isfinite(world_corner.y))
						return false;
					draw_corner = ImVec2(world_corner.x * x_scale, screen_height - world_corner.y * y_scale);
				}
				else {
					if (!camera)
						return false;
					const Vector3 screen_corner = camera.WorldToScreenPoint(world_corner);
					if (!valid_screen_point(screen_corner))
						return false;
					draw_corner = to_draw_space(screen_corner);
				}
				rect_min_x = std::min(rect_min_x, draw_corner.x);
				rect_max_x = std::max(rect_max_x, draw_corner.x);
				rect_min_y = std::min(rect_min_y, draw_corner.y);
				rect_max_y = std::max(rect_max_y, draw_corner.y);
			}
			min_x = rect_min_x;
			min_y = rect_min_y;
			max_x = rect_max_x;
			max_y = rect_max_y;
			return true;
			};
		const bool rect_projected = project_rect_transform();
		projected = rect_projected;
		// Union renderers that plausibly belong to the selected object. Some VFX
		// and UI renderers report a screen-sized bounds even when their GameObject
		// is a small child; accepting those turns a nearby selection into a giant
		// highlight rectangle.
		const float maximum_bounds_radius = selected_distance > 0.01f
			? std::max(2.5f, selected_distance * 1.5f)
			: 100.0f;
		for (const Inspect::ObjectHandle& renderer_handle : highlight_renderers_) {
			const Renderer renderer{ Inspect::ResolveObjectHandle(renderer_handle).handle() };
			if (rect_projected || !camera)
				break;
			if (!safe_object_alive(renderer))
				continue;
			const Bounds bounds = renderer.bounds();
			const float bounds_radius = bounds.extents.magnitude();
			if (!std::isfinite(bounds.center.x) || !std::isfinite(bounds.center.y) ||
				!std::isfinite(bounds.center.z) || !std::isfinite(bounds_radius) ||
				bounds_radius <= 0.0001f || bounds_radius > maximum_bounds_radius)
				continue;
			const Vector3 min = bounds.min();
			const Vector3 max = bounds.max();
			const Vector3 corners[] = {
				{min.x, min.y, min.z}, {min.x, min.y, max.z}, {min.x, max.y, min.z}, {min.x, max.y, max.z},
				{max.x, min.y, min.z}, {max.x, min.y, max.z}, {max.x, max.y, min.z}, {max.x, max.y, max.z},
			};
			float renderer_min_x = std::numeric_limits<float>::max();
			float renderer_min_y = std::numeric_limits<float>::max();
			float renderer_max_x = std::numeric_limits<float>::lowest();
			float renderer_max_y = std::numeric_limits<float>::lowest();
			bool renderer_projected = false;
			for (const Vector3& corner : corners) {
				const Vector3 screen = camera.WorldToScreenPoint(corner);
				if (!valid_screen_point(screen))
					continue;
				const ImVec2 draw_point = to_draw_space(screen);
				// Clamp near-plane samples before computing bounds.
				constexpr float kProjectionOverflow = 1.0f;
				const float x = std::clamp(draw_point.x, -screen_width * kProjectionOverflow,
					screen_width * (1.0f + kProjectionOverflow));
				const float y = std::clamp(draw_point.y, -screen_height * kProjectionOverflow,
					screen_height * (1.0f + kProjectionOverflow));
				renderer_min_x = std::min(renderer_min_x, x);
				renderer_max_x = std::max(renderer_max_x, x);
				renderer_min_y = std::min(renderer_min_y, y);
				renderer_max_y = std::max(renderer_max_y, y);
				renderer_projected = true;
			}
			if (!renderer_projected)
				continue;
			if (!projected) {
				min_x = renderer_min_x;
				min_y = renderer_min_y;
				max_x = renderer_max_x;
				max_y = renderer_max_y;
				projected = true;
			}
			else {
				min_x = std::min(min_x, renderer_min_x);
				min_y = std::min(min_y, renderer_min_y);
				max_x = std::max(max_x, renderer_max_x);
				max_y = std::max(max_y, renderer_max_y);
			}
		}

		constexpr float kFallbackWidth = 34.0f;
		constexpr float kFallbackHeight = 64.0f;
		const auto use_transform_anchor = [&] {
			if (!camera)
				return false;
			const Vector3 anchor = camera.WorldToScreenPoint(selected_position);
			if (!valid_screen_point(anchor))
				return false;
			const ImVec2 draw_anchor = to_draw_space(anchor);
			min_x = draw_anchor.x - kFallbackWidth * 0.5f;
			max_x = draw_anchor.x + kFallbackWidth * 0.5f;
			min_y = draw_anchor.y - kFallbackHeight * 0.5f;
			max_y = draw_anchor.y + kFallbackHeight * 0.5f;
			return true;
			};
		// Use an anchor when the object has no renderer.
		if (!projected && selected_transform) {
			if (is_rect_transform && is_overlay_canvas) {
				min_x = selected_position.x * x_scale - kFallbackWidth * 0.5f;
				max_x = selected_position.x * x_scale + kFallbackWidth * 0.5f;
				min_y = screen_height - selected_position.y * y_scale - kFallbackHeight * 0.5f;
				max_y = screen_height - selected_position.y * y_scale + kFallbackHeight * 0.5f;
				projected = true;
			}
			else {
				projected = use_transform_anchor();
			}
		}

		const auto clear_locator = [&] {
			if (highlight_locator_id_ != 0) {
				ModUI::Highlight::enqueue_remove(highlight_locator_id_);
				highlight_locator_id_ = 0;
			}
			};
		const auto update_offscreen_locator = [&] {
			if (!camera)
				return;
			const Vector3 target_screen = camera.WorldToScreenPoint(selected_position);
			if (!std::isfinite(target_screen.x) || !std::isfinite(target_screen.y) ||
				!std::isfinite(target_screen.z))
				return;
			float locator_x = target_screen.x;
			float locator_y = screen_height - target_screen.y;
			if (target_screen.z <= 0.01f) {
				// Use the camera basis for objects behind the camera.
				const Transform camera_transform = camera.transform();
				if (!camera_transform)
					return;
				const Vector3 offset = selected_position - camera_transform.position();
				const float distance = offset.magnitude();
				if (distance <= 0.0001f)
					return;
				const Vector3 direction = offset / distance;
				float horizontal = Vector3::dot(direction, camera_transform.right());
				float vertical = -Vector3::dot(direction, camera_transform.up());
				const float length = std::sqrt(horizontal * horizontal + vertical * vertical);
				if (length <= 0.0001f) {
					horizontal = 0.0f;
					// A target exactly behind the player has no left/right component.
					// Use the lower edge to communicate "turn around", not the top.
					vertical = 1.0f;
				}
				else {
					horizontal /= length;
					vertical /= length;
				}
				locator_x = screen_width * 0.5f + horizontal * screen_width;
				locator_y = screen_height * 0.5f + vertical * screen_height;
			}
			const float edge_padding = 24.0f;
			const float edge_x = std::clamp(locator_x, edge_padding, screen_width - edge_padding);
			const float edge_y = std::clamp(locator_y, edge_padding, screen_height - edge_padding);
			ModUI::Highlight::Style locator_style = style;
			locator_style.color = IM_COL32(255, 120, 48, 255);
			locator_style.fill_color = IM_COL32(255, 120, 48, 96);
			locator_style.corner_box = true;
			locator_style.filled = true;
			locator_style.thickness = 2.0f;
			locator_style.corner_length = 8.0f;
			locator_style.draw_label = true;
			locator_style.label_above_box = false;
			locator_style.offscreen_indicator = true;
			locator_style.indicator_color = IM_COL32(255, 145, 48, 255);
			locator_style.indicator_thickness = 3.5f;
			locator_style.indicator_head_size = 14.0f;
			locator_style.indicator_center_gap = 10.0f;
			// Zero keeps the arrow connected to the edge marker.
			locator_style.indicator_length = 0.0f;
			locator_style.indicator_center_dot_radius = 4.5f;
			const ImVec2 min(edge_x - 10.0f, edge_y - 10.0f);
			const ImVec2 max(edge_x + 10.0f, edge_y + 10.0f);
			const std::string locator_label = "OFFSCREEN  " + label;
			if (highlight_locator_id_ == 0) {
				highlight_locator_id_ = ModUI::Highlight::enqueue_add_screen_rect(
					min, max, locator_label.c_str(), locator_style);
			}
			else {
				ModUI::Highlight::enqueue_set_screen_rect(highlight_locator_id_, min, max);
				ModUI::Highlight::enqueue_set_label(highlight_locator_id_, locator_label.c_str());
			}
			};

		// Never carry a screen-dominating renderer rectangle into the draw list.
		// If the pivot is not projectable, discard the rectangle and use the
		// off-screen locator below. Previously the failed fallback left the old
		// giant bounds intact, producing the solid blue screen in the report.
		if (projected &&
			(max_x - min_x > screen_width * 0.65f || max_y - min_y > screen_height * 0.65f)) {
			projected = use_transform_anchor();
		}
		if (!projected || max_x < 0.0f || max_y < 0.0f || min_x > screen_width || min_y > screen_height) {
			if (highlight_id_ != 0) {
				ModUI::Highlight::enqueue_remove(highlight_id_);
				highlight_id_ = 0;
			}
			update_offscreen_locator();
			return;
		}
		clear_locator();

		constexpr float kPadding = 3.0f;
		constexpr float kMinimumSize = 14.0f;
		min_x -= kPadding;
		min_y -= kPadding;
		max_x += kPadding;
		max_y += kPadding;
		if (max_x - min_x < kMinimumSize) {
			const float center = (min_x + max_x) * 0.5f;
			min_x = center - kMinimumSize * 0.5f;
			max_x = center + kMinimumSize * 0.5f;
		}
		if (max_y - min_y < kMinimumSize) {
			const float center = (min_y + max_y) * 0.5f;
			min_y = center - kMinimumSize * 0.5f;
			max_y = center + kMinimumSize * 0.5f;
		}

		if (highlight_id_ == 0)
			highlight_id_ =
			ModUI::Highlight::enqueue_add_screen_rect(ImVec2(min_x, min_y), ImVec2(max_x, max_y), label.c_str(), style);
		else
			ModUI::Highlight::enqueue_set_screen_rect(highlight_id_, ImVec2(min_x, min_y), ImVec2(max_x, max_y));
		if (highlight_id_ != 0)
			ModUI::Highlight::enqueue_set_label(highlight_id_, label.c_str());
	}

	void RuntimeModel::clear_highlight_renderer_cache() {
		for (Inspect::ObjectHandle& handle : highlight_renderers_)
			Inspect::FreeObjectHandle(handle);
		highlight_renderers_.clear();
	}

	void RuntimeModel::clear_highlight_camera_cache() {
		for (Inspect::ObjectHandle& handle : highlight_cameras_)
			Inspect::FreeObjectHandle(handle);
		highlight_cameras_.clear();
	}

	void RuntimeModel::clear_selection() {
		if (camera_focus_active_)
			restore_focused_camera();
		if (highlight_id_ != 0) {
			ModUI::Highlight::enqueue_remove(highlight_id_);
			highlight_id_ = 0;
		}
		if (highlight_locator_id_ != 0) {
			ModUI::Highlight::enqueue_remove(highlight_locator_id_);
			highlight_locator_id_ = 0;
		}
		selected_ = {};
		Inspect::FreeObjectHandle(selected_handle_);
		clear_locked_members();
		clear_component_cache();
		clear_highlight_renderer_cache();
		working_.selected_instance_id = 0;
		working_.inspector = {};
		working_.camera_focus_active = false;
	}

	void RuntimeModel::discard_managed_state_after_native_fault() {
		const std::uint64_t quarantined_before = Inspect::QuarantinedObjectHandleCount();
		hierarchy_census_.reset();
		if (highlight_id_ != 0)
			ModUI::Highlight::enqueue_remove(highlight_id_);
		if (highlight_locator_id_ != 0)
			ModUI::Highlight::enqueue_remove(highlight_locator_id_);
		highlight_id_ = 0;
		highlight_locator_id_ = 0;
		Inspect::FreeObjectHandle(focused_camera_handle_);
		focused_camera_handle_ = {};
		Inspect::FreeObjectHandle(focused_target_handle_);
		focused_target_handle_ = {};
		camera_focus_active_ = false;
		camera_transition_active_ = false;

		selected_ = {};
		Inspect::FreeObjectHandle(selected_handle_);
		hierarchy_instance_ids_.clear();
		clear_locked_members();
		clear_component_cache();
		clear_object_inspector();
		for (auto& [_, handle] : class_browser_handles_)
			Inspect::FreeObjectHandle(handle);
		class_browser_handles_.clear();
		for (auto& [_, handle] : class_browser_static_handles_)
			Inspect::FreeObjectHandle(handle);
		class_browser_static_handles_.clear();
		component_reflection_.clear();
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
		working_.field_watches.clear();
		working_.locked_member_keys.clear();
		working_.class_browser_instances.clear();

		// HierarchyInfo and its strings are native snapshot data and remain safe
		// to render. Keeping them avoids a blank workspace while the next census
		// rebuilds the non-owning object index.
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
		case CommandKind::InspectReference: return "Inspect reference";
		case CommandKind::SetActive: return "Set GameObject active";
		case CommandKind::DeleteObject: return "Delete GameObject";
		case CommandKind::DeleteComponent: return "Delete component";
		case CommandKind::FocusSelected: return "Focus camera";
		case CommandKind::RestoreCamera: return "Restore camera";
		case CommandKind::SetCameraFocusDistance: return "Set camera focus distance";
		case CommandKind::SetCameraFocusTopDown: return "Set camera focus orientation";
		case CommandKind::SetCameraFocusTilt: return "Set camera focus tilt";
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
		working_.hierarchy = hierarchy_;
		working_.component_class_catalog = component_class_catalog_;
		working_.class_browser_catalog = class_browser_catalog_;
		working_.live_data = live_data_;
		working_.highlight_enabled = highlight_enabled_;
		working_.camera_focus_distance = camera_focus_distance_;
		working_.camera_focus_tilt = camera_focus_tilt_;
		working_.camera_focus_top_down = camera_focus_top_down_;
		working_.method_traces = MethodTracer::snapshots();
		working_.field_watches.clear();
		working_.field_watches.reserve(field_watches_.size());
		for (const auto& [_, state] : field_watches_)
			working_.field_watches.push_back(state.snapshot);
		std::sort(working_.field_watches.begin(), working_.field_watches.end(), [](const Snapshot::FieldWatch& left,
			const Snapshot::FieldWatch& right) {
				return left.id < right.id;
			});
		for (MethodTracer::Snapshot& trace : working_.method_traces) {
			for (MethodTracer::Record& record : trace.records) {
				record.caller_display = managed_caller_location(record.caller_address);
				if (record.return_captured && trace.return_type == "System.String")
					record.return_display = describe_traced_string(record.return_rax);
				if (trace.target_is_reference)
					record.target_display = describe_traced_reference(record.target_address, trace.declaring_type);
				record.argument_displays.resize(record.arguments.size());
				for (std::size_t index = 0; index < record.arguments.size(); ++index) {
					if (index < trace.parameter_is_reference.size() && trace.parameter_is_reference[index]) {
						const std::string_view type = index < trace.parameter_types.size()
							? std::string_view(trace.parameter_types[index])
							: std::string_view{};
						record.argument_displays[index] = describe_traced_reference(record.arguments[index], type);
					}
				}
			}
		}
		working_.strong_handle_count = component_handles_.size();
		working_.weak_handle_count = 0;
		if (selected_handle_.handle)
			++(selected_handle_.weak ? working_.weak_handle_count : working_.strong_handle_count);
		for (const auto& [_, handle] : reference_handles_)
			++(handle.weak ? working_.weak_handle_count : working_.strong_handle_count);
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
		working_.managed_used_bytes = URK::il2cpp::gc_get_used_size();
		working_.managed_heap_bytes = URK::il2cpp::gc_get_heap_size();
		++working_.revision;
		published_.store(std::make_shared<const Snapshot>(working_));
	}

	void RuntimeModel::publish_recovery_snapshot() {
		// Keep this strictly native-only.  In particular, gc_get_* and trace
		// display formatting call IL2CPP and are not safe immediately after SEH.
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

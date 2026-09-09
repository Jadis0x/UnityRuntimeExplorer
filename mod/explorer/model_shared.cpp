// Copyright (c) 2026 Jadis0x. All rights reserved.
#include "model_shared.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <unordered_map>
#include <vector>

using namespace URK::Unity;

namespace Explorer {
	namespace {

#if defined(_WIN32)
		thread_local NativeFaultRecord g_native_fault{};
#endif

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
			// unordered_map keeps element addresses stable across rehash, so the
			// sorted view can borrow the names instead of copying them.
			std::unordered_map<std::uintptr_t, std::string> methods;
			std::vector<std::pair<std::uintptr_t, const std::string*>> sorted;
			bool sorted_stale = true;
			// Set once every assembly has been walked. Until then a missing
			// entry means "not indexed yet", not "not a managed method".
			bool complete = false;
		};

		ManagedCallerIndex& managed_caller_index() {
			static ManagedCallerIndex index;
			return index;
		}

	} // namespace

#if defined(_WIN32)
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

	const NativeFaultRecord& last_native_fault() {
		return g_native_fault;
	}
#endif

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

	const URK::Unity::Inspect::FieldInfo *backing_field_for(
	const std::vector<URK::Unity::Inspect::FieldInfo> &fields, std::string_view property_name,
	std::string_view declaring_type) {
	if (property_name.empty())
		return nullptr;
	// The C# compiler names an auto-property's storage "<Name>k__BackingField".
	// Nothing else in a build is named that way, so the match is exact rather
	// than a guess at a convention.
	std::string generated;
	generated.reserve(property_name.size() + 18);
	generated += '<';
	generated += property_name;
	generated += ">k__BackingField";
	const URK::Unity::Inspect::FieldInfo *fallback = nullptr;
	for (const URK::Unity::Inspect::FieldInfo &field : fields) {
		if (field.name != generated)
			continue;
		// A derived class can inherit a same-named property from two levels;
		// prefer the field declared alongside the property.
		if (declaring_type.empty() || field.declaring_type.full_name == declaring_type)
			return &field;
		if (!fallback)
			fallback = &field;
	}
	return fallback;
}

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

#if defined(_WIN32)
	// A function containing __try cannot itself construct or return a value
	// of non-trivial destructor type (C2712), even transiently and even if
	// immediately discarded -- so this leaf only ever touches raw pointers,
	// and the caller (which has no __try) builds the std::string from them,
	// mirroring detail::class_display_name()'s own namespace + "." + name
	// logic.
	namespace {
		struct RawClassNameProbe {
			const char* namespc = nullptr;
			const char* name = nullptr;
		};
		bool runtime_class_name_probe_guarded(Object object, RawClassNameProbe& out) {
			__try {
				const void* klass = detail::Backend::object_get_class(object.handle());
				if (!klass)
					return true;
				out.namespc = detail::Backend::class_get_namespace(klass);
				out.name = detail::Backend::class_get_name(klass);
				return true;
			}
			__except (capture_native_fault(_exception_info())) {
				return false;
			}
		}
	} // namespace
#endif

	std::string safe_runtime_class_name(Object object) {
		if (!safe_object_alive(object))
			return {};
#if defined(_WIN32)
		clear_error();
		RawClassNameProbe probe;
		if (!runtime_class_name_probe_guarded(object, probe)) {
			clear_error();
			return {};
		}
		std::string name = probe.namespc && probe.namespc[0]
			? std::string(probe.namespc) + "." + (probe.name ? probe.name : "<unnamed>")
			: (probe.name && probe.name[0] ? std::string(probe.name) : std::string{});
		if (const char* error = last_error(); error && error[0])
			clear_error();
		return name;
#else
		return object.runtime_class_name();
#endif
	}

	Inspect::TypeInfo safe_type_of(Object object) {
		if (!object || !readable_address(reinterpret_cast<std::uintptr_t>(object.handle())))
			return {};
		// Inspect::TypeOf() already guards its own native reads internally.
		clear_error();
		return Inspect::TypeOf(object);
	}

	std::string normalized_type(std::string_view name) {
		return detail::normalized_type_name(name);
	}

	std::string scene_display_name(std::string_view path, int build_index) {
		if (!path.empty()) {
			const std::size_t slash = path.find_last_of("/\\");
			const std::size_t start = slash == std::string_view::npos ? 0 : slash + 1;
			std::string name(path.substr(start));
			if (const std::size_t extension = name.rfind('.'); extension != std::string::npos)
				name.resize(extension);
			if (!name.empty())
				return name;
		}
		return "Build scene " + std::to_string(build_index);
	}

	ComponentInfo::Field field_metadata(const Inspect::FieldInfo& field) {
		ComponentInfo::Field member{};
		member.name = field.name;
		member.type_name = field.type_name;
		member.declaring_type = field.declaring_type.full_name;
		member.is_static = field.is_static;
		member.is_read_only = !Inspect::FieldCanWrite(field);
		member.is_value_type = field.is_value_type;
		member.is_enum = field.is_enum;
		member.runtime_safe = !field.type_is_opaque;
		if (field.type_is_opaque)
			member.capability_reason = "Runtime-specific type; metadata is available but generic read/write is unsafe.";
		return member;
	}

#if defined(_WIN32)
	namespace {
		// safe_type_of() already guards itself, so only the pin/weak call
		// needs its own __try here; ObjectHandle is trivial (no std::string
		// members), which keeps this leaf function C2712-safe.
		Inspect::ObjectHandle pin_or_weak_guarded(Object object, bool use_pin, bool& faulted) {
			faulted = false;
			__try {
				return use_pin ? Inspect::PinObject(object) : Inspect::WeakObject(object);
			}
			__except (capture_native_fault(_exception_info())) {
				faulted = true;
				return {};
			}
		}
	} // namespace
#endif

	Inspect::ObjectHandle tracked_reference_handle(const Inspect::ValueInfo& value) {
		if (!value.object)
			return {};
		const Object object{ value.object };
		const Inspect::TypeInfo type = safe_type_of(object);
		// Keep boxed values and returned strings alive until their Inspector tab closes.
		const bool use_pin = type.is_value_type || value.kind == Inspect::ValueKind::String;
#if defined(_WIN32)
		bool faulted = false;
		Inspect::ObjectHandle handle = pin_or_weak_guarded(object, use_pin, faulted);
		if (faulted) {
			detail::set_error("Tracked reference handle creation raised a native access fault");
			return {};
		}
		return handle;
#else
		return use_pin ? Inspect::PinObject(object) : Inspect::WeakObject(object);
#endif
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
#if defined(_WIN32)
	namespace {
		// A function containing __try cannot itself construct or return a
		// value of non-trivial destructor type (C2712), even transiently and
		// even if immediately discarded. Only trivial-typed data (handles,
		// bools) comes out of this leaf; the caller (which has no __try)
		// builds the ValueInfo/error message from it afterward.
		struct ReferenceResolveResult {
			Inspect::ObjectHandle rooted{};
			void* resolved_handle = nullptr;
			bool valid = false;
		};
		bool reference_value_from_address_guarded(unsigned long long address, const void* destination_type,
				bool destination_is_value_type, ReferenceResolveResult& out) {
			__try {
				out.rooted = Inspect::PinObject(Object{ reinterpret_cast<void*>(static_cast<std::uintptr_t>(address)) });
				const Object resolved = Inspect::ResolveObjectHandle(out.rooted);
				out.resolved_handle = resolved.handle();
				out.valid =
					out.rooted.handle && resolved &&
					(!destination_type || (destination_is_value_type ? Inspect::IsBoxedValueOfType(resolved, destination_type)
						: Inspect::IsAssignableTo(resolved, destination_type)));
				return true;
			}
			__except (capture_native_fault(_exception_info())) {
				return false;
			}
		}
	} // namespace
#endif

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
			const std::int32_t size = URK::managed::class_value_size(destination.handle, &alignment);
			if (size <= 0)
				return false;
			std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size), 0);
			void* boxed = URK::managed::value_box(static_cast<const URK::managed::Class*>(destination.handle), bytes.data());
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
		const bool destination_is_value_type = destination_type && destination.is_value_type;
		bool valid = false;
		void* resolved_handle = nullptr;
#if defined(_WIN32)
		ReferenceResolveResult result;
		if (!reference_value_from_address_guarded(address, destination_type, destination_is_value_type, result)) {
			detail::set_error("Reference conversion raised a native access fault");
			rooted = result.rooted;
			return false;
		}
		rooted = result.rooted;
		resolved_handle = result.resolved_handle;
		valid = result.valid;
#else
		rooted = Inspect::PinObject(Object{ reinterpret_cast<void*>(static_cast<std::uintptr_t>(address)) });
		const Object resolved = Inspect::ResolveObjectHandle(rooted);
		resolved_handle = resolved.handle();
		valid =
			rooted.handle && resolved &&
			(!destination_type || (destination_is_value_type ? Inspect::IsBoxedValueOfType(resolved, destination_type)
				: Inspect::IsAssignableTo(resolved, destination_type)));
#endif
		if (valid) {
			value = {};
			value.kind =
				destination_is_value_type ? Inspect::ValueKind::ValueType : Inspect::ValueKind::ObjectReference;
			value.type_name = std::string(type_name);
			value.object = resolved_handle;
			value.readable = true;
			value.display = pointer_text(resolved_handle);
			return true;
		}
		if (rooted.handle && resolved_handle && destination_type) {
			const Inspect::ObjectRefInfo actual = Inspect::DescribeObject(Object{ resolved_handle });
			detail::set_error("Reference type mismatch: expected " + std::string(type_name) +
				", received " + (actual.type.full_name.empty() ? std::string("<unknown>") : actual.type.full_name));
		}
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

	std::uint64_t component_sample_token(int component_id, bool property, std::size_t index) {
		return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(component_id)) << 32) |
			(property ? 0x80000000ull : 0ull) | static_cast<std::uint64_t>(index & 0x0fffffffu);
	}

	bool is_expected_empty_container_error(std::string_view message) {
		return message.find("System.InvalidOperationException: Map is empty") != std::string_view::npos ||
			message.find("Map is empty") != std::string_view::npos;
	}

#if defined(_WIN32)
	namespace {
		// Only method_pointer() dereferences a possibly-stale native handle;
		// isolating just that call keeps this leaf function C2712-safe while
		// the display-name string and map insertion stay in the caller.
		void* method_pointer_guarded(const void* handle, bool& faulted) {
			faulted = false;
			__try {
				return URK::managed::method_pointer(static_cast<const URK::managed::Method*>(handle));
			}
			__except (capture_native_fault(_exception_info())) {
				// Caller names do not justify a domain-wide fallback scan.
				faulted = true;
				return nullptr;
			}
		}
	} // namespace
#endif

	void remember_managed_method(const Inspect::MethodInfo& method) {
		if (!method.handle || method.name.empty())
			return;
#if defined(URK_BACKEND_MONO)
		// Avoid JIT-compiling arbitrary metadata methods; that can raise native exceptions.
		return;
#else
		void* pointer = nullptr;
#if defined(_WIN32)
		bool faulted = false;
		pointer = method_pointer_guarded(method.handle, faulted);
		if (faulted || !pointer)
			return;
#else
		pointer = URK::managed::method_pointer(static_cast<const URK::managed::Method*>(method.handle));
		if (!pointer)
			return;
#endif
		const std::string name = method.declaring_type.full_name.empty()
			? method.name
			: method.declaring_type.full_name + "." + method.name;
		auto& index = managed_caller_index();
		const auto [found, inserted] = index.methods.emplace(reinterpret_cast<std::uintptr_t>(pointer), name);
		if (inserted)
			index.sorted_stale = true;
		else if (found->second != name)
			found->second = "<shared managed generic code>";
	#endif
	}

	namespace {

		const std::vector<std::pair<std::uintptr_t, const std::string*>>& sorted_caller_entries() {
			ManagedCallerIndex& index = managed_caller_index();
			if (!index.sorted_stale)
				return index.sorted;
			// Re-sorting every frame while the index is being built is too costly; throttle it.
			using SortClock = std::chrono::steady_clock;
			static SortClock::time_point last_sort{};
			const SortClock::time_point now = SortClock::now();
			if (!index.sorted.empty() && now - last_sort < std::chrono::milliseconds(500))
				return index.sorted;
			last_sort = now;
			index.sorted.clear();
			index.sorted.reserve(index.methods.size());
			for (const auto& entry : index.methods)
				index.sorted.emplace_back(entry.first, &entry.second);
			std::sort(index.sorted.begin(), index.sorted.end(),
				[](const auto& left, const auto& right) { return left.first < right.first; });
			index.sorted_stale = false;
			return index.sorted;
		}

		// Nearest indexed entry at or below the address. Only correct for a method
		// small enough that the compiler emitted no unwind data for it - anything
		// larger has a .pdata entry and is named exactly, and guessing across the
		// gap is how a caller inside the IL2CPP runtime ended up labelled with the
		// name of whichever managed method happened to sit below it.
		std::string enclosing_indexed_method(std::uintptr_t address) {
			const auto& entries = sorted_caller_entries();
			if (entries.empty())
				return {};
			const auto next = std::upper_bound(entries.begin(), entries.end(), address,
				[](std::uintptr_t value, const auto& entry) { return value < entry.first; });
			if (next == entries.begin() || next == entries.end())
				return {};
			const auto entry = std::prev(next);
			const std::uintptr_t offset = address - entry->first;
			// A leaf method with no frame to unwind. Past this it is another
			// method's code, not a deep offset into this one.
			constexpr std::uintptr_t kMaxUnwindlessMethodExtent = 256;
			if (offset > kMaxUnwindlessMethodExtent)
				return {};
			// Anything with unwind data between the two is a different function.
			DWORD64 unused_base = 0;
			if (RtlLookupFunctionEntry(static_cast<DWORD64>(entry->first), &unused_base, nullptr) != nullptr)
				return {};
			char suffix[32]{};
			std::snprintf(suffix, sizeof(suffix), " +0x%llX", static_cast<unsigned long long>(offset));
			return *entry->second + suffix;
		}

	} // namespace

	namespace {

		// The unwind fragment a split function chains back to, or 0. Kept free of
		// objects so the structured handler is allowed here: the unwind data is
		// read straight out of the image.
		DWORD64 chained_fragment_start(DWORD64 fragment) {
			DWORD64 image_base = 0;
			const PRUNTIME_FUNCTION entry = RtlLookupFunctionEntry(fragment, &image_base, nullptr);
			if (!entry)
				return 0;
			const auto *unwind =
				reinterpret_cast<const std::uint8_t *>(static_cast<std::uintptr_t>(image_base) + entry->UnwindInfoAddress);
			DWORD64 parent_start = 0;
#if defined(_WIN32)
			__try {
#endif
				if (((unwind[0] >> 3) & UNW_FLAG_CHAININFO) != 0) {
					const std::size_t codes = unwind[2];
					const std::size_t offset = 4 + ((codes + 1) & ~static_cast<std::size_t>(1)) * 2;
					RUNTIME_FUNCTION parent{};
					std::memcpy(&parent, unwind + offset, sizeof(parent));
					parent_start = image_base + parent.BeginAddress;
				}
#if defined(_WIN32)
			}
			__except (EXCEPTION_EXECUTE_HANDLER) {
				parent_start = 0;
			}
#endif
			return parent_start == fragment ? 0 : parent_start;
		}

	} // namespace

	void mark_caller_index_complete() { managed_caller_index().complete = true; }

	std::string managed_method_location(std::uintptr_t function_start, std::uintptr_t address) {
		const auto found = managed_caller_index().methods.find(function_start);
		if (found == managed_caller_index().methods.end())
			return module_location(address);
		if (address <= function_start)
			return found->second;
		char suffix[32]{};
		std::snprintf(suffix, sizeof(suffix), " +0x%llX", static_cast<unsigned long long>(address - function_start));
		return found->second + suffix;
	}

	std::string managed_caller_location(std::uintptr_t address) {
		if (!address)
			return module_location(address);
		DWORD64 image_base = 0;
		const PRUNTIME_FUNCTION function = RtlLookupFunctionEntry(static_cast<DWORD64>(address), &image_base, nullptr);
		if (function) {
			// MSVC splits a function into several unwind fragments, and only the
			// first carries the address metadata knows the method by, so a hit
			// inside a later fragment has to chain back to it.
			DWORD64 fragment = image_base + function->BeginAddress;
			for (int depth = 0; depth < 8 && fragment != 0; ++depth) {
				const auto found = managed_caller_index().methods.find(static_cast<std::uintptr_t>(fragment));
				if (found != managed_caller_index().methods.end())
					return managed_method_location(static_cast<std::uintptr_t>(fragment), address);
				fragment = chained_fragment_start(fragment);
			}
		}
		const std::string enclosing = enclosing_indexed_method(address);
		if (!enclosing.empty())
			return enclosing;
		// A frame the index cannot name because the address has unwind data and
		// its function is not a managed method: IL2CPP's own runtime, reached
		// through reflection, a delegate or a UI binding. Saying so stops the
		// panel from advising an index rebuild that cannot help.
		// Only once the whole index is in can a missing entry mean the enclosing
		// function is not managed at all - IL2CPP dispatches reflection,
		// delegates and UI bindings through its own code, and no index will ever
		// name those. Saying it earlier mislabels methods that are still being
		// indexed, which then appear to fix themselves.
		if (function && managed_caller_index().complete)
			return module_location(address) + " (native runtime code)";
		return module_location(address);
	}

} // namespace Explorer

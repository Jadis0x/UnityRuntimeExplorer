#pragma once
#include "unity_shortcuts.h"

#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <unordered_set>

namespace URK::Unity {
// URK_UNITY_INSPECT_BEGIN
namespace Inspect {
inline constexpr std::uint32_t kStaticMemberFlag = 0x0010u;
inline constexpr std::size_t kMaxMetadataInheritanceDepth = 128;
inline constexpr std::size_t kMaxMetadataMembers = 131072;
inline constexpr std::size_t kMaxMetadataMembersPerClass = 32768;
inline constexpr std::uint32_t kMaxMethodParameters = 256;

// IL2CPP metadata is normally UTF-8, but some obfuscators deliberately use
// invalid byte sequences for member names. Dear ImGui replaces those bytes with
// identical replacement glyphs, making distinct methods impossible to tell
// apart. Keep valid UTF-8 intact (including non-Latin scripts) and render only
// malformed names as stable hex IDs.
inline bool metadata_name_is_valid_utf8(std::string_view text) {
    for (std::size_t index = 0; index < text.size();) {
        const unsigned char first = static_cast<unsigned char>(text[index]);
        if (first <= 0x7Fu) {
            ++index;
            continue;
        }

        const auto continuation = [&](std::size_t offset) {
            return index + offset < text.size() &&
                   (static_cast<unsigned char>(text[index + offset]) & 0xC0u) == 0x80u;
        };
        if (first >= 0xC2u && first <= 0xDFu && continuation(1)) {
            index += 2;
            continue;
        }
        if (first >= 0xE0u && first <= 0xEFu && continuation(1) && continuation(2)) {
            const unsigned char second = static_cast<unsigned char>(text[index + 1]);
            if ((first != 0xE0u || second >= 0xA0u) && (first != 0xEDu || second <= 0x9Fu)) {
                index += 3;
                continue;
            }
        }
        if (first >= 0xF0u && first <= 0xF4u && continuation(1) && continuation(2) && continuation(3)) {
            const unsigned char second = static_cast<unsigned char>(text[index + 1]);
            if ((first != 0xF0u || second >= 0x90u) && (first != 0xF4u || second <= 0x8Fu)) {
                index += 4;
                continue;
            }
        }
        return false;
    }
    return true;
}

inline std::string metadata_display_name(const char* value) {
    if (!value || !value[0])
        return {};

    const std::string raw(value);
    if (metadata_name_is_valid_utf8(raw))
        return raw;

    constexpr char kHex[] = "0123456789ABCDEF";
    constexpr std::size_t kDisplayBytes = 24;
    std::string display = "obf_";
    const std::size_t byteCount = (std::min)(raw.size(), kDisplayBytes);
    display.reserve(display.size() + byteCount * 2 + 9 + (raw.size() > byteCount ? 3 : 0));
    for (std::size_t index = 0; index < byteCount; ++index) {
        const unsigned char byte = static_cast<unsigned char>(raw[index]);
        display.push_back(kHex[byte >> 4u]);
        display.push_back(kHex[byte & 0x0Fu]);
    }
    if (raw.size() > byteCount)
        display += "...";
    std::uint32_t hash = 2166136261u;
    for (const unsigned char byte : raw) {
        hash ^= byte;
        hash *= 16777619u;
    }
    display.push_back('_');
    for (int shift = 28; shift >= 0; shift -= 4)
        display.push_back(kHex[(hash >> static_cast<unsigned>(shift)) & 0x0Fu]);
    return display;
}

#if defined(_WIN32)
inline int metadata_exception_filter(unsigned long code) {
    // Access violation and in-page error are the only faults that can safely be
    // translated into an invalid-metadata result. Stack overflow, illegal
    // instruction and every unrelated exception must continue to the host.
    return code == 0xC0000005ul || code == 0xC0000006ul ? 1 : 0;
}
#endif
struct TypeInfo {
    const void* handle = nullptr;
    std::string namespc;
    std::string name;
    std::string full_name;
    std::uint32_t flags = 0;
    bool is_value_type = false;
    bool is_enum = false;
};
struct MemberTypeInfo {
    std::string name;
    bool readable = false;
    bool is_value_type = false;
    bool is_enum = false;
    bool is_opaque = false;
};
struct FieldInfo {
    const void* handle = nullptr;
    TypeInfo declaring_type;
    std::string name;
    const void* type = nullptr;
    std::string type_name;
    std::uint32_t flags = 0;
    bool is_static = false;
    bool is_value_type = false;
    bool is_enum = false;
    bool type_is_opaque = false;
};
struct MethodParamInfo {
    const void* type = nullptr;
    std::string name;
    std::string type_name;
    bool is_value_type = false;
    bool is_enum = false;
    bool is_opaque = false;
};
struct MethodInfo {
    const void* handle = nullptr;
    TypeInfo declaring_type;
    std::string name;
    const void* return_type_handle = nullptr;
    std::string return_type;
    std::vector<MethodParamInfo> parameters;
    std::uint32_t flags = 0;
    std::uint32_t iflags = 0;
    bool is_static = false;
    bool return_is_value_type = false;
    bool return_is_enum = false;
    bool return_type_is_opaque = false;
};
struct PropertyInfo {
    const void* handle = nullptr;
    TypeInfo declaring_type;
    std::string name;
    const void* type = nullptr;
    std::string type_name;
    const void* get_method = nullptr;
    const void* set_method = nullptr;
    std::uint32_t flags = 0;
    bool can_read = false;
    bool can_write = false;
    bool is_static = false;
    bool is_value_type = false;
    bool is_enum = false;
    bool type_is_opaque = false;
};
enum class ValueKind {
    Unavailable,
    Null,
    Boolean,
    SignedInteger,
    UnsignedInteger,
    FloatingPoint,
    String,
    ObjectReference,
    ArrayReference,
    Enum,
    Structured,
    ValueType,
};
struct ValueInfo {
    ValueKind kind = ValueKind::Unavailable;
    std::string type_name;
    std::string display;
    void* object = nullptr;
    bool bool_value = false;
    std::int64_t signed_value = 0;
    std::uint64_t unsigned_value = 0;
    double floating_value = 0.0;
    std::array<double, 8> components{};
    std::size_t component_count = 0;
    std::size_t array_length = 0;
    bool readable = false;
};
struct ObjectRefInfo {
    void* handle = nullptr;
    TypeInfo type;
    std::string display;
    bool is_null = true;
    bool expandable = false;
};
struct ObjectHandle {
    URK::il2cpp::GCHandle handle = 0;
    bool weak = false;
    bool pinned = false;
};
inline std::uint64_t QuarantinedObjectHandleCount() {
    return detail::quarantined_gchandle_counter().load(std::memory_order_relaxed);
}
inline void emit(DiagnosticSink sink, const char* text) { if (sink) sink(text); }
inline std::string type_name(const void* type) {
    if (!type) return {};
    char out[256]{};
#if defined(_WIN32)
    // Some IL2CPP builds expose transient type records while a generated
    // component is being initialized. A failed type name must not poison the
    // whole reflection pass (or escape the host's main-thread callback).
    __try {
        return URK::il2cpp::type_get_name(static_cast<const URK::il2cpp::Type*>(type), out, sizeof(out))
            ? std::string(out) : std::string{};
    } __except (metadata_exception_filter(_exception_code())) {
        detail::set_error("Unity Inspect type_get_name raised a native access fault for an invalid type record");
        return {};
    }
#else
    return URK::il2cpp::type_get_name(static_cast<const URK::il2cpp::Type*>(type), out, sizeof(out))
        ? std::string(out) : std::string{};
#endif
}
inline std::int32_t metadata_type_code(const void* type) {
    if (!type) return -1;
#if defined(_WIN32)
    // This is intentionally best-effort.  It is used only to improve value
    // rendering; a foreign type record must remain displayable even when it
    // does not implement IL2CPP's class-resolution contract.
    __try {
        return URK::il2cpp::type_get_type(static_cast<const URK::il2cpp::Type*>(type));
    } __except (metadata_exception_filter(_exception_code())) {
        return -1;
    }
#else
    return URK::il2cpp::type_get_type(static_cast<const URK::il2cpp::Type*>(type));
#endif
}
inline MemberTypeInfo describe_member_type(const void* type) {
    MemberTypeInfo out{};
    out.name = type_name(type);
    out.readable = !out.name.empty();
    if (!out.readable) return out;

    // ECMA-335 / Il2CppRGCTXDataType element kinds.  These answers come from
    // the Type record itself and do not require type_get_class_or_element_class.
    // In particular, do not resolve a Class just to learn traits for a method
    // signature: DynamicMonoBehaviour exposes foreign Mono signatures there.
    switch (metadata_type_code(type)) {
    case 0x02: // BOOLEAN
    case 0x03: // CHAR
    case 0x04: // I1
    case 0x05: // U1
    case 0x06: // I2
    case 0x07: // U2
    case 0x08: // I4
    case 0x09: // U4
    case 0x0A: // I8
    case 0x0B: // U8
    case 0x0C: // R4
    case 0x0D: // R8
    case 0x11: // VALUETYPE
    case 0x16: // TYPEDBYREF
    case 0x18: // I
    case 0x19: // U
        out.is_value_type = true;
        break;
    case 0x55: // ENUM
        out.is_value_type = true;
        out.is_enum = true;
        break;
    case 0x10: // BYREF
    case 0x13: // VAR
    case 0x1B: // FNPTR
    case 0x1E: // MVAR
    case 0x1F: // CMOD_REQD
    case 0x20: // CMOD_OPT
    case 0x21: // INTERNAL
        // These signatures require runtime-specific marshalling.  They are
        // valid metadata and remain visible/copyable, but must never be read
        // or invoked through the generic object/value path.
        out.is_opaque = true;
        break;
    default:
        break;
    }
    return out;
}
inline TypeInfo DescribeClass(const void* klass) {
    TypeInfo out{};
    out.handle = klass;
    if (!klass) return out;
    const auto* k = static_cast<const URK::il2cpp::Class*>(klass);
#if defined(_WIN32)
    __try {
#endif
    const char* ns = URK::il2cpp::class_get_namespace(k);
    const char* name = URK::il2cpp::class_get_name(k);
    out.namespc = ns ? ns : "";
    out.name = name ? name : "";
    out.full_name = out.namespc.empty() ? out.name : out.namespc + "." + out.name;
    out.flags = URK::il2cpp::class_get_flags(k);
    out.is_value_type = URK::il2cpp::class_is_valuetype(k);
    out.is_enum = URK::il2cpp::class_is_enum(k);
#if defined(_WIN32)
    } __except (metadata_exception_filter(_exception_code())) {
        detail::set_error("Unity Inspect DescribeClass raised a native access fault for an invalid class record");
        return {};
    }
#endif
    return out;
}
inline TypeInfo DescribeType(const void* type) {
    if (!type) return {};
#if defined(_WIN32)
    __try {
        return DescribeClass(URK::il2cpp::type_get_class_or_element_class(
            static_cast<const URK::il2cpp::Type*>(type)));
    } __except (metadata_exception_filter(_exception_code())) {
        detail::set_error("Unity Inspect type_get_class_or_element_class raised a native access fault for an invalid type record");
        return {};
    }
#else
    return DescribeClass(URK::il2cpp::type_get_class_or_element_class(
        static_cast<const URK::il2cpp::Type*>(type)));
#endif
}
inline TypeInfo TypeOf(Object object) {
    detail::clear_error();
    const void* klass = detail::Backend::object_get_class(object.handle());
    if (!klass) { detail::set_error("Unity Inspect::TypeOf failed: object_get_class failed"); detail::append_backend_error(); return {}; }
    return DescribeClass(klass);
}
inline ObjectRefInfo DescribeObject(Object object) {
    detail::clear_error();
    ObjectRefInfo out{};
    out.handle = object.handle();
    out.is_null = out.handle == nullptr;
    if (out.is_null) {
        out.display = "null";
        return out;
    }
    out.type = TypeOf(object);
    if (!out.type.handle) {
        out.display = "<unavailable object>";
        return out;
    }
    out.display = out.type.full_name.empty() ? "<object>" : out.type.full_name;
    out.expandable = true;
    return out;
}
inline bool IsAssignableTo(Object object, const void* targetType) {
    if (!object.handle() || !targetType) return false;
    const auto* target = URK::il2cpp::type_get_class_or_element_class(
        static_cast<const URK::il2cpp::Type*>(targetType));
    const auto* actual = static_cast<const URK::il2cpp::Class*>(detail::Backend::object_get_class(object.handle()));
    return target && actual && URK::il2cpp::class_is_assignable_from(target, actual) != 0;
}
// Value types are passed to IL2CPP as unboxed memory.  A boxed source must
// therefore match the exact value-type class; assignability is only valid for
// managed reference types.
inline bool IsBoxedValueOfType(Object object, const void* targetType) {
    if (!object.handle() || !targetType) return false;
    const auto* target = URK::il2cpp::type_get_class_or_element_class(
        static_cast<const URK::il2cpp::Type*>(targetType));
    const auto* actual = static_cast<const URK::il2cpp::Class*>(
        detail::Backend::object_get_class(object.handle()));
    return target && actual && target == actual && URK::il2cpp::class_is_valuetype(target);
}
inline ObjectRefInfo ExpandValue(const ValueInfo& value) {
    if (value.kind == ValueKind::Null) {
        ObjectRefInfo out{};
        out.display = "null";
        return out;
    }
    if ((value.kind != ValueKind::ObjectReference && value.kind != ValueKind::ArrayReference) || !value.object) {
        detail::set_error("Unity Inspect::ExpandValue failed: value is not an object reference");
        return {};
    }
    return DescribeObject(Object{value.object});
}
inline ObjectHandle PinObject(Object object, bool pinned = false) {
    detail::clear_error();
    ObjectHandle out{};
    out.pinned = pinned;
    if (!object.handle()) { detail::set_error("Unity Inspect::PinObject failed: object is null"); return out; }
    out.handle = detail::Backend::gchandle_new(object.handle(), pinned ? 1 : 0);
    if (!out.handle) { detail::set_error("Unity Inspect::PinObject failed: backend gchandle_new API is unavailable or failed"); detail::append_backend_error(); }
    return out;
}
inline ObjectHandle PinValue(const ValueInfo& value, bool pinned = false) {
    if ((value.kind != ValueKind::ObjectReference && value.kind != ValueKind::ArrayReference && value.kind != ValueKind::String) || !value.object) {
        detail::set_error("Unity Inspect::PinValue failed: value is not a managed object reference");
        return {};
    }
    return PinObject(Object{value.object}, pinned);
}
inline ObjectHandle WeakObject(Object object, bool trackResurrection = false) {
    detail::clear_error();
    ObjectHandle out{};
    out.weak = true;
    if (!object.handle()) { detail::set_error("Unity Inspect::WeakObject failed: object is null"); return out; }
    out.handle = detail::Backend::gchandle_new_weakref(object.handle(), trackResurrection ? 1 : 0);
    if (!out.handle) { detail::set_error("Unity Inspect::WeakObject failed: backend gchandle_new_weakref API is unavailable or failed"); detail::append_backend_error(); }
    return out;
}
inline Object ResolveObjectHandle(const ObjectHandle& handle) {
    detail::clear_error();
    if (!handle.handle) { detail::set_error("Unity Inspect::ResolveObjectHandle failed: handle is empty"); return {}; }
    void* object = detail::Backend::gchandle_get_target(handle.handle);
    if (!object) { detail::set_error("Unity Inspect::ResolveObjectHandle failed: backend gchandle_get_target API is unavailable, failed, or target was collected"); detail::append_backend_error(); return {}; }
    return Object{object};
}
inline void FreeObjectHandle(ObjectHandle& handle) {
    detail::clear_error();
    if (!handle.handle) return;
    const URK::il2cpp::GCHandle raw_handle = handle.handle;
    // Consume ownership before calling the backend. If a damaged runtime table
    // faults while freeing this one handle, retrying it every recovery tick is
    // worse than an observable, bounded one-handle leak.
    handle.handle = 0;
#if defined(_WIN32)
    __try {
        detail::Backend::gchandle_free(raw_handle);
    } __except (metadata_exception_filter(_exception_code())) {
        detail::quarantined_gchandle_counter().fetch_add(1, std::memory_order_relaxed);
        detail::set_error("Unity Inspect::FreeObjectHandle raised a native access fault; one GC handle was quarantined");
    }
#else
    detail::Backend::gchandle_free(raw_handle);
#endif
    handle.weak = false;
    handle.pinned = false;
}
inline std::vector<FieldInfo> fields_from_class(const URK::il2cpp::Class* klass, bool includeInherited) {
    std::vector<FieldInfo> out;
    std::unordered_set<const void*> visited;
    std::size_t depth = 0;
    for (const void* current = klass; current; current = includeInherited ? URK::il2cpp::class_get_parent(static_cast<const URK::il2cpp::Class*>(current)) : nullptr) {
        if (depth++ >= kMaxMetadataInheritanceDepth || !visited.insert(current).second) {
            detail::set_error("Unity Inspect::Fields rejected a cyclic or excessive inheritance chain");
            break;
        }
        const TypeInfo declaring = DescribeClass(current);
        if (!declaring.handle) break;
        void* it = nullptr;
        std::unordered_set<const void*> members;
        std::size_t member_count = 0;
        while (const auto* field = URK::il2cpp::class_get_fields(static_cast<const URK::il2cpp::Class*>(current), &it)) {
            if (++member_count > kMaxMetadataMembersPerClass || !members.insert(field).second) {
                detail::set_error("Unity Inspect::Fields rejected a cyclic or excessive field iterator");
                return out;
            }
            if (out.size() >= kMaxMetadataMembers) {
                detail::set_error("Unity Inspect::Fields rejected an excessive member count");
                return out;
            }
            FieldInfo info{};
            info.handle = field;
            info.declaring_type = declaring;
            const char* name = URK::il2cpp::field_get_name(field);
            info.name = metadata_display_name(name);
            const void* fieldType = URK::il2cpp::field_get_type(field);
            const MemberTypeInfo fieldTypeInfo = describe_member_type(fieldType);
            // Keep readable foreign signatures. They can be copied and shown,
            // but their value traits remain deliberately conservative.
            if (!fieldType || !fieldTypeInfo.readable)
                continue;
            info.type = fieldType;
            info.type_name = std::move(fieldTypeInfo.name);
            info.flags = URK::il2cpp::field_get_flags(field);
            info.is_static = (info.flags & kStaticMemberFlag) != 0;
            info.is_value_type = fieldTypeInfo.is_value_type;
            info.is_enum = fieldTypeInfo.is_enum;
            info.type_is_opaque = fieldTypeInfo.is_opaque;
            out.push_back(info);
        }
    }
    return out;
}
inline std::vector<FieldInfo> Fields(TypeRef type, bool includeInherited = true) {
    detail::clear_error();
    const void* klass = type.resolve_class();
    if (!klass) { detail::set_error("Unity Inspect::Fields failed: type lookup failure"); detail::append_backend_error(); return {}; }
    return fields_from_class(static_cast<const URK::il2cpp::Class*>(klass), includeInherited);
}
inline std::vector<FieldInfo> Fields(Object object, bool includeInherited = true) {
    detail::clear_error();
    const void* klass = detail::Backend::object_get_class(object.handle());
    if (!klass) { detail::set_error("Unity Inspect::Fields failed: object_get_class failed"); detail::append_backend_error(); return {}; }
    return fields_from_class(static_cast<const URK::il2cpp::Class*>(klass), includeInherited);
}
inline std::vector<FieldInfo> Fields(const ObjectRefInfo& object, bool includeInherited = true) {
    return object.handle ? Fields(Object{object.handle}, includeInherited) : std::vector<FieldInfo>{};
}
inline MethodInfo method_info(const URK::il2cpp::Method* method, TypeInfo declaring) {
    MethodInfo info{};
    info.handle = method;
    info.declaring_type = std::move(declaring);
    const char* name = URK::il2cpp::method_get_name(method);
    info.name = metadata_display_name(name);
    info.flags = URK::il2cpp::method_get_flags(method, &info.iflags);
    info.is_static = (info.flags & kStaticMemberFlag) != 0;
    info.return_type_handle = URK::il2cpp::method_get_return_type(method);
    const MemberTypeInfo return_type_info = describe_member_type(info.return_type_handle);
    info.return_type = return_type_info.name;
    info.return_is_value_type = return_type_info.is_value_type;
    info.return_is_enum = return_type_info.is_enum;
    info.return_type_is_opaque = return_type_info.is_opaque;
    const std::uint32_t count = URK::il2cpp::method_get_param_count(method);
    if (count > kMaxMethodParameters) {
        detail::set_error("Unity Inspect::Methods rejected an excessive parameter count");
        return {};
    }
    if (!info.return_type_handle)
        return {};
    info.parameters.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        const void* paramType = URK::il2cpp::method_get_param(method, i);
        if (!paramType)
            return {};
        const char* paramName = URK::il2cpp::method_get_param_name(method, i);
        const MemberTypeInfo parameter_type_info = describe_member_type(paramType);
        if (!parameter_type_info.readable)
            return {};
        info.parameters.push_back(MethodParamInfo{paramType, metadata_display_name(paramName), parameter_type_info.name,
                                                  parameter_type_info.is_value_type, parameter_type_info.is_enum,
                                                  parameter_type_info.is_opaque});
    }
    return info;
}
inline std::vector<MethodInfo> methods_from_class(const URK::il2cpp::Class* klass, bool includeInherited) {
    std::vector<MethodInfo> out;
    std::unordered_set<const void*> visited;
    std::size_t depth = 0;
    for (const void* current = klass; current; current = includeInherited ? URK::il2cpp::class_get_parent(static_cast<const URK::il2cpp::Class*>(current)) : nullptr) {
        if (depth++ >= kMaxMetadataInheritanceDepth || !visited.insert(current).second) {
            detail::set_error("Unity Inspect::Methods rejected a cyclic or excessive inheritance chain");
            break;
        }
        const TypeInfo declaring = DescribeClass(current);
        if (!declaring.handle) break;
        void* it = nullptr;
        std::unordered_set<const void*> members;
        std::size_t member_count = 0;
        while (const auto* method = URK::il2cpp::class_get_methods(static_cast<const URK::il2cpp::Class*>(current), &it)) {
            if (++member_count > kMaxMetadataMembersPerClass || !members.insert(method).second) {
                detail::set_error("Unity Inspect::Methods rejected a cyclic or excessive method iterator");
                return out;
            }
            if (out.size() >= kMaxMetadataMembers) {
                detail::set_error("Unity Inspect::Methods rejected an excessive member count");
                return out;
            }
            MethodInfo info = method_info(method, declaring);
            if (info.handle)
                out.push_back(std::move(info));
        }
    }
    return out;
}
inline std::vector<MethodInfo> Methods(TypeRef type, bool includeInherited = true) {
    detail::clear_error();
    const void* klass = type.resolve_class();
    if (!klass) { detail::set_error("Unity Inspect::Methods failed: type lookup failure"); detail::append_backend_error(); return {}; }
    return methods_from_class(static_cast<const URK::il2cpp::Class*>(klass), includeInherited);
}
inline std::vector<MethodInfo> Methods(Object object, bool includeInherited = true) {
    detail::clear_error();
    const void* klass = detail::Backend::object_get_class(object.handle());
    if (!klass) { detail::set_error("Unity Inspect::Methods failed: object_get_class failed"); detail::append_backend_error(); return {}; }
    return methods_from_class(static_cast<const URK::il2cpp::Class*>(klass), includeInherited);
}
inline std::vector<MethodInfo> Methods(const ObjectRefInfo& object, bool includeInherited = true) {
    return object.handle ? Methods(Object{object.handle}, includeInherited) : std::vector<MethodInfo>{};
}
inline PropertyInfo property_info(const URK::il2cpp::Property* property, TypeInfo declaring) {
    PropertyInfo info{};
    info.handle = property;
    info.declaring_type = std::move(declaring);
    const char* name = URK::il2cpp::property_get_name(property);
    info.name = metadata_display_name(name);
    info.flags = URK::il2cpp::property_get_flags(property);
    info.get_method = URK::il2cpp::property_get_get_method(property);
    info.set_method = URK::il2cpp::property_get_set_method(property);
    if (info.get_method && URK::il2cpp::method_get_param_count(static_cast<const URK::il2cpp::Method*>(info.get_method)) != 0)
        info.get_method = nullptr; // Indexers cannot be invoked without arguments by the explorer.
    if (info.set_method && URK::il2cpp::method_get_param_count(static_cast<const URK::il2cpp::Method*>(info.set_method)) != 1)
        info.set_method = nullptr;
    info.can_read = info.get_method != nullptr;
    info.can_write = info.set_method != nullptr;
    if (const void* accessor = info.get_method ? info.get_method : info.set_method) {
        std::uint32_t iflags = 0;
        info.is_static = (URK::il2cpp::method_get_flags(
            static_cast<const URK::il2cpp::Method*>(accessor), &iflags) & kStaticMemberFlag) != 0;
    }
    const void* propertyType = info.get_method ? URK::il2cpp::method_get_return_type(static_cast<const URK::il2cpp::Method*>(info.get_method))
                                               : (info.set_method ? URK::il2cpp::method_get_param(static_cast<const URK::il2cpp::Method*>(info.set_method), 0) : nullptr);
    const MemberTypeInfo propertyTypeInfo = describe_member_type(propertyType);
    if (!propertyType || !propertyTypeInfo.readable)
        return {};
    info.type = propertyType;
    info.type_name = propertyTypeInfo.name;
    info.is_value_type = propertyTypeInfo.is_value_type;
    info.is_enum = propertyTypeInfo.is_enum;
    info.type_is_opaque = propertyTypeInfo.is_opaque;
    return info;
}
inline std::vector<PropertyInfo> properties_from_class(const URK::il2cpp::Class* klass, bool includeInherited) {
    std::vector<PropertyInfo> out;
    std::unordered_set<const void*> visited;
    std::size_t depth = 0;
    for (const void* current = klass; current; current = includeInherited ? URK::il2cpp::class_get_parent(static_cast<const URK::il2cpp::Class*>(current)) : nullptr) {
        if (depth++ >= kMaxMetadataInheritanceDepth || !visited.insert(current).second) {
            detail::set_error("Unity Inspect::Properties rejected a cyclic or excessive inheritance chain");
            break;
        }
        const TypeInfo declaring = DescribeClass(current);
        if (!declaring.handle) break;
        void* it = nullptr;
        std::unordered_set<const void*> members;
        std::size_t member_count = 0;
        while (const auto* property = URK::il2cpp::class_get_properties(static_cast<const URK::il2cpp::Class*>(current), &it)) {
            if (++member_count > kMaxMetadataMembersPerClass || !members.insert(property).second) {
                detail::set_error("Unity Inspect::Properties rejected a cyclic or excessive property iterator");
                return out;
            }
            if (out.size() >= kMaxMetadataMembers) {
                detail::set_error("Unity Inspect::Properties rejected an excessive member count");
                return out;
            }
            PropertyInfo info = property_info(property, declaring);
            if (info.handle)
                out.push_back(std::move(info));
        }
    }
    return out;
}
inline std::vector<PropertyInfo> Properties(TypeRef type, bool includeInherited = true) {
    detail::clear_error();
    const void* klass = type.resolve_class();
    if (!klass) { detail::set_error("Unity Inspect::Properties failed: type lookup failure"); detail::append_backend_error(); return {}; }
    return properties_from_class(static_cast<const URK::il2cpp::Class*>(klass), includeInherited);
}
inline std::vector<PropertyInfo> Properties(Object object, bool includeInherited = true) {
    detail::clear_error();
    const void* klass = detail::Backend::object_get_class(object.handle());
    if (!klass) { detail::set_error("Unity Inspect::Properties failed: object_get_class failed"); detail::append_backend_error(); return {}; }
    return properties_from_class(static_cast<const URK::il2cpp::Class*>(klass), includeInherited);
}
inline std::vector<PropertyInfo> Properties(const ObjectRefInfo& object, bool includeInherited = true) {
    return object.handle ? Properties(Object{object.handle}, includeInherited) : std::vector<PropertyInfo>{};
}
inline ValueInfo unavailable_value(std::string typeName, std::string message) {
    ValueInfo out{};
    out.type_name = std::move(typeName);
    out.display = std::move(message);
    out.kind = ValueKind::Unavailable;
    out.readable = false;
    return out;
}
inline ValueInfo value_type_placeholder(std::string typeName) {
    ValueInfo out{};
    out.kind = ValueKind::ValueType;
    out.type_name = std::move(typeName);
    out.display = "<value type>";
    out.readable = false;
    return out;
}
inline ValueInfo enum_placeholder(std::string typeName) {
    ValueInfo out{};
    out.kind = ValueKind::Enum;
    out.type_name = std::move(typeName);
    out.display = "<enum>";
    out.readable = false;
    return out;
}
inline std::string enum_underlying_type_name(const void* type) {
    const void* klass = type ? URK::il2cpp::type_get_class_or_element_class(static_cast<const URK::il2cpp::Type*>(type)) : nullptr;
    if (!klass) return {};
    const void* underlying = URK::il2cpp::class_enum_basetype(static_cast<const URK::il2cpp::Class*>(klass));
    if (underlying) return type_name(underlying);
    void* it = nullptr;
    while (const auto* field = URK::il2cpp::class_get_fields(static_cast<const URK::il2cpp::Class*>(klass), &it)) {
        const char* name = URK::il2cpp::field_get_name(field);
        if (name && std::string_view{name} == "value__") return type_name(URK::il2cpp::field_get_type(field));
    }
    return {};
}
inline ValueInfo scalar_from_pointer(std::string typeName, void* data);
inline ValueInfo enum_from_pointer(std::string typeName, std::string underlyingTypeName, void* data) {
    ValueInfo out = scalar_from_pointer(underlyingTypeName, data);
    if (!out.readable) return unavailable_value(std::move(typeName), std::string("enum underlying type is unsupported: ") + underlyingTypeName);
    out.kind = ValueKind::Enum;
    out.type_name = std::move(typeName);
    return out;
}
inline bool type_name_looks_array(std::string_view typeName) {
    const std::string normalized = detail::normalized_type_name(typeName);
    if (normalized == "system.array") return true;
    if (normalized.size() >= 2 && normalized.compare(normalized.size() - 2, 2, "[]") == 0) return true;
    const std::size_t open = normalized.rfind('[');
    return open != std::string::npos && !normalized.empty() && normalized.back() == ']' && open + 1 < normalized.size() && normalized[open + 1] == ',';
}
inline ValueInfo array_reference_value(std::string typeName, void* object) {
    ValueInfo out{};
    out.type_name = std::move(typeName);
    out.object = object;
    out.readable = true;
    if (!object) {
        out.kind = ValueKind::Null;
        out.display = "null";
        return out;
    }
    out.kind = ValueKind::ArrayReference;
    if (detail::Backend::has_array_length()) {
        out.array_length = detail::Backend::array_length(object);
        out.display = "array[" + std::to_string(out.array_length) + "]";
    } else {
        out.display = "<array>";
    }
    return out;
}
inline ValueInfo object_reference_value(std::string typeName, void* object) {
    ValueInfo out{};
    out.type_name = std::move(typeName);
    out.object = object;
    out.readable = true;
    if (!object) {
        out.kind = ValueKind::Null;
        out.display = "null";
        return out;
    }
    if (type_name_looks_array(out.type_name)) return array_reference_value(out.type_name, object);
    out.kind = ValueKind::ObjectReference;
    out.display = detail::class_display_name(detail::Backend::object_get_class(object));
    if (out.display.empty()) out.display = "<object>";
    return out;
}
inline ValueInfo string_value(std::string typeName, void* object) {
    ValueInfo out{};
    out.type_name = std::move(typeName);
    out.object = object;
    out.readable = true;
    if (!object) {
        out.kind = ValueKind::Null;
        out.display = "null";
        return out;
    }
    out.kind = ValueKind::String;
    // Do not pass a transient field/property reference directly to the UTF-8
    // converter. A strong handle roots it through allocation/conversion and
    // avoids publishing a raw managed pointer into an inspector snapshot.
    ObjectHandle rooted = PinObject(Object{object});
    if (!rooted.handle) {
        out.kind = ValueKind::Unavailable;
        out.readable = false;
        out.object = nullptr;
        out.display = detail::fallback_error() ? detail::fallback_error()
                                               : "could not root managed string";
        return out;
    }
    const Object stable = ResolveObjectHandle(rooted);
    if (!stable) {
        FreeObjectHandle(rooted);
        out.kind = ValueKind::Unavailable;
        out.readable = false;
        out.object = nullptr;
        out.display = detail::fallback_error() ? detail::fallback_error()
                                               : "managed string was released";
        return out;
    }
    out.display = detail::managed_string_to_utf8(stable.handle());
    FreeObjectHandle(rooted);
    // `display` is native-owned after conversion; snapshots must never retain
    // the managed pointer now that its temporary handle has been released.
    out.object = nullptr;
    return out;
}
inline std::size_t structured_component_count(std::string_view typeName) {
    const std::string normalized = detail::normalized_type_name(typeName);
    if (normalized == "unityengine.vector2" || normalized == "unityengine.vector2int") return 2;
    if (normalized == "unityengine.vector3" || normalized == "unityengine.vector3int") return 3;
    if (normalized == "unityengine.vector4" || normalized == "unityengine.quaternion" ||
        normalized == "unityengine.color" || normalized == "unityengine.color32" ||
        normalized == "unityengine.rect" || normalized == "unityengine.rectint") return 4;
    if (normalized == "unityengine.bounds" || normalized == "unityengine.boundsint") return 6;
    return 0;
}
inline bool structured_integer_type(std::string_view typeName) {
    const std::string normalized = detail::normalized_type_name(typeName);
    return normalized == "unityengine.vector2int" || normalized == "unityengine.vector3int" ||
           normalized == "unityengine.rectint" || normalized == "unityengine.boundsint";
}
inline bool structured_byte_type(std::string_view typeName) {
    return detail::normalized_type_name(typeName) == "unityengine.color32";
}
inline ValueInfo structured_from_pointer(std::string typeName, void* data) {
    const std::size_t count = structured_component_count(typeName);
    if (!data || count == 0) return value_type_placeholder(std::move(typeName));
    ValueInfo out{};
    out.kind = ValueKind::Structured;
    out.type_name = std::move(typeName);
    out.component_count = count;
    out.readable = true;
    if (structured_byte_type(out.type_name)) {
        const auto* values = static_cast<const std::uint8_t*>(data);
        for (std::size_t i = 0; i < count; ++i) out.components[i] = values[i];
    } else if (structured_integer_type(out.type_name)) {
        const auto* values = static_cast<const std::int32_t*>(data);
        for (std::size_t i = 0; i < count; ++i) out.components[i] = values[i];
    } else {
        const auto* values = static_cast<const float*>(data);
        for (std::size_t i = 0; i < count; ++i) out.components[i] = values[i];
    }
    out.display = "(";
    for (std::size_t i = 0; i < count; ++i) {
        if (i) out.display += ", ";
        char text[48]{};
        if (structured_integer_type(out.type_name) || structured_byte_type(out.type_name))
            std::snprintf(text, sizeof(text), "%.0f", out.components[i]);
        else
            std::snprintf(text, sizeof(text), "%.5g", out.components[i]);
        out.display += text;
    }
    out.display += ")";
    return out;
}
inline ValueInfo scalar_from_pointer(std::string typeName, void* data) {
    if (!data) return unavailable_value(std::move(typeName), "value data is null");
    const std::string normalized = detail::normalized_type_name(typeName);
    ValueInfo out{};
    out.type_name = std::move(typeName);
    out.readable = true;
    if (normalized == "system.boolean") {
        out.kind = ValueKind::Boolean;
        out.bool_value = *static_cast<bool*>(data);
        out.display = out.bool_value ? "true" : "false";
        return out;
    }
    if (normalized == "system.int16") { out.kind = ValueKind::SignedInteger; out.signed_value = *static_cast<std::int16_t*>(data); out.display = std::to_string(out.signed_value); return out; }
    if (normalized == "system.int32") { out.kind = ValueKind::SignedInteger; out.signed_value = *static_cast<std::int32_t*>(data); out.display = std::to_string(out.signed_value); return out; }
    if (normalized == "system.int64") { out.kind = ValueKind::SignedInteger; out.signed_value = *static_cast<std::int64_t*>(data); out.display = std::to_string(out.signed_value); return out; }
    if (normalized == "system.intptr") { out.kind = ValueKind::SignedInteger; out.signed_value = static_cast<std::int64_t>(*static_cast<std::intptr_t*>(data)); char text[32]{}; std::snprintf(text, sizeof(text), "0x%llX", static_cast<unsigned long long>(static_cast<std::uintptr_t>(out.signed_value))); out.display = text; return out; }
    if (normalized == "system.sbyte") { out.kind = ValueKind::SignedInteger; out.signed_value = *static_cast<std::int8_t*>(data); out.display = std::to_string(out.signed_value); return out; }
    if (normalized == "system.uint16" || normalized == "system.char") { out.kind = ValueKind::UnsignedInteger; out.unsigned_value = *static_cast<std::uint16_t*>(data); out.display = std::to_string(out.unsigned_value); return out; }
    if (normalized == "system.uint32") { out.kind = ValueKind::UnsignedInteger; out.unsigned_value = *static_cast<std::uint32_t*>(data); out.display = std::to_string(out.unsigned_value); return out; }
    if (normalized == "system.uint64") { out.kind = ValueKind::UnsignedInteger; out.unsigned_value = *static_cast<std::uint64_t*>(data); out.display = std::to_string(out.unsigned_value); return out; }
    if (normalized == "system.uintptr") { out.kind = ValueKind::UnsignedInteger; out.unsigned_value = static_cast<std::uint64_t>(*static_cast<std::uintptr_t*>(data)); char text[32]{}; std::snprintf(text, sizeof(text), "0x%llX", static_cast<unsigned long long>(out.unsigned_value)); out.display = text; return out; }
    if (normalized == "system.byte") { out.kind = ValueKind::UnsignedInteger; out.unsigned_value = *static_cast<std::uint8_t*>(data); out.display = std::to_string(out.unsigned_value); return out; }
    if (normalized == "system.single") { out.kind = ValueKind::FloatingPoint; out.floating_value = *static_cast<float*>(data); char text[64]{}; std::snprintf(text, sizeof(text), "%.6g", out.floating_value); out.display = text; return out; }
    if (normalized == "system.double") { out.kind = ValueKind::FloatingPoint; out.floating_value = *static_cast<double*>(data); char text[64]{}; std::snprintf(text, sizeof(text), "%.12g", out.floating_value); out.display = text; return out; }
    if (structured_component_count(out.type_name) != 0)
        return structured_from_pointer(std::move(out.type_name), data);
    return value_type_placeholder(out.type_name);
}
inline bool read_field_raw(Object object, const FieldInfo& field, void* output) {
    if (!field.handle) { detail::set_error("Unity Inspect::ReadField failed: field handle is null"); return false; }
    if (field.is_static) {
        if (!field.declaring_type.handle) { detail::set_error("Unity Inspect::ReadField failed: static field declaring type is unavailable"); return false; }
        if (!detail::Backend::field_static_get_value(field.declaring_type.handle, field.handle, output)) { detail::set_error(std::string("Unity Inspect::ReadField failed: static field read failed: ") + field.name); detail::append_backend_error(); return false; }
        return true;
    }
    if (!object.handle()) { detail::set_error("Unity Inspect::ReadField failed: target object is null"); return false; }
    if (!detail::Backend::field_get_value(object.handle(), field.handle, output)) { detail::set_error(std::string("Unity Inspect::ReadField failed: field read failed: ") + field.name); detail::append_backend_error(); return false; }
    return true;
}
struct WriteStorage {
    bool b{};
    std::int8_t i8{};
    std::int16_t i16{};
    std::int32_t i32{};
    std::int64_t i64{};
    std::uint8_t u8{};
    std::uint16_t u16{};
    std::uint32_t u32{};
    std::uint64_t u64{};
    float f32{};
    double f64{};
    void* reference{};
    alignas(16) std::array<std::uint8_t, 64> structured{};
};
inline const char* value_kind_name(ValueKind kind) {
    switch (kind) {
    case ValueKind::Unavailable: return "Unavailable";
    case ValueKind::Null: return "Null";
    case ValueKind::Boolean: return "Boolean";
    case ValueKind::SignedInteger: return "SignedInteger";
    case ValueKind::UnsignedInteger: return "UnsignedInteger";
    case ValueKind::FloatingPoint: return "FloatingPoint";
    case ValueKind::String: return "String";
    case ValueKind::ObjectReference: return "ObjectReference";
    case ValueKind::ArrayReference: return "ArrayReference";
    case ValueKind::Enum: return "Enum";
    case ValueKind::Structured: return "Structured";
    case ValueKind::ValueType: return "ValueType";
    }
    return "<unknown>";
}
template<class T> inline bool assign_integral_value(const ValueInfo& value, T& output, std::string_view targetType, std::string_view context) {
    if (value.kind == ValueKind::UnsignedInteger || (value.kind == ValueKind::Enum && std::is_unsigned_v<T>)) {
        const std::uint64_t unsignedValue = value.unsigned_value;
        if constexpr (std::is_signed_v<T>) {
            if (unsignedValue > static_cast<std::uint64_t>(std::numeric_limits<T>::max())) {
                detail::set_error(std::string("Unity Inspect::") + std::string(context) + " failed: unsigned integer is out of range for " + std::string(targetType));
                return false;
            }
        } else {
            if (unsignedValue > static_cast<std::uint64_t>(std::numeric_limits<T>::max())) {
                detail::set_error(std::string("Unity Inspect::") + std::string(context) + " failed: unsigned integer is out of range for " + std::string(targetType));
                return false;
            }
        }
        output = static_cast<T>(unsignedValue);
        return true;
    }
    if (value.kind == ValueKind::SignedInteger || value.kind == ValueKind::Enum) {
        const std::int64_t signedValue = value.signed_value;
        if constexpr (std::is_unsigned_v<T>) {
            if (signedValue < 0 || static_cast<std::uint64_t>(signedValue) > static_cast<std::uint64_t>(std::numeric_limits<T>::max())) {
                detail::set_error(std::string("Unity Inspect::") + std::string(context) + " failed: signed integer is out of range for " + std::string(targetType));
                return false;
            }
        } else {
            if (signedValue < static_cast<std::int64_t>(std::numeric_limits<T>::min()) || signedValue > static_cast<std::int64_t>(std::numeric_limits<T>::max())) {
                detail::set_error(std::string("Unity Inspect::") + std::string(context) + " failed: signed integer is out of range for " + std::string(targetType));
                return false;
            }
        }
        output = static_cast<T>(signedValue);
        return true;
    }
    detail::set_error(std::string("Unity Inspect::") + std::string(context) + " failed: expected integer value for " + std::string(targetType) + ", got " + value_kind_name(value.kind));
    return false;
}
inline bool scalar_write_pointer(std::string_view typeName, const ValueInfo& value, WriteStorage& storage, void*& pointer, std::string_view context) {
    const std::string normalized = detail::normalized_type_name(typeName);
    if (normalized == "system.boolean") {
        if (value.kind != ValueKind::Boolean) { detail::set_error(std::string("Unity Inspect::") + std::string(context) + " failed: expected Boolean for " + std::string(typeName) + ", got " + value_kind_name(value.kind)); return false; }
        storage.b = value.bool_value; pointer = &storage.b; return true;
    }
    if (normalized == "system.sbyte") { if (!assign_integral_value(value, storage.i8, typeName, context)) return false; pointer = &storage.i8; return true; }
    if (normalized == "system.int16") { if (!assign_integral_value(value, storage.i16, typeName, context)) return false; pointer = &storage.i16; return true; }
    if (normalized == "system.int32") { if (!assign_integral_value(value, storage.i32, typeName, context)) return false; pointer = &storage.i32; return true; }
    if (normalized == "system.int64") { if (!assign_integral_value(value, storage.i64, typeName, context)) return false; pointer = &storage.i64; return true; }
    if (normalized == "system.intptr") { if (!assign_integral_value(value, storage.i64, typeName, context)) return false; pointer = &storage.i64; return true; }
    if (normalized == "system.byte") { if (!assign_integral_value(value, storage.u8, typeName, context)) return false; pointer = &storage.u8; return true; }
    if (normalized == "system.uint16" || normalized == "system.char") { if (!assign_integral_value(value, storage.u16, typeName, context)) return false; pointer = &storage.u16; return true; }
    if (normalized == "system.uint32") { if (!assign_integral_value(value, storage.u32, typeName, context)) return false; pointer = &storage.u32; return true; }
    if (normalized == "system.uint64") { if (!assign_integral_value(value, storage.u64, typeName, context)) return false; pointer = &storage.u64; return true; }
    if (normalized == "system.uintptr") { if (!assign_integral_value(value, storage.u64, typeName, context)) return false; pointer = &storage.u64; return true; }
    if (normalized == "system.single") {
        if (value.kind != ValueKind::FloatingPoint) { detail::set_error(std::string("Unity Inspect::") + std::string(context) + " failed: expected FloatingPoint for " + std::string(typeName) + ", got " + value_kind_name(value.kind)); return false; }
        if (value.floating_value > static_cast<double>(std::numeric_limits<float>::max()) || value.floating_value < -static_cast<double>(std::numeric_limits<float>::max())) { detail::set_error(std::string("Unity Inspect::") + std::string(context) + " failed: floating point value is out of range for " + std::string(typeName)); return false; }
        storage.f32 = static_cast<float>(value.floating_value); pointer = &storage.f32; return true;
    }
    if (normalized == "system.double") {
        if (value.kind != ValueKind::FloatingPoint) { detail::set_error(std::string("Unity Inspect::") + std::string(context) + " failed: expected FloatingPoint for " + std::string(typeName) + ", got " + value_kind_name(value.kind)); return false; }
        storage.f64 = value.floating_value; pointer = &storage.f64; return true;
    }
    const std::size_t componentCount = structured_component_count(typeName);
    if (componentCount != 0) {
        if (value.kind != ValueKind::Structured || value.component_count != componentCount) {
            detail::set_error(std::string("Unity Inspect::") + std::string(context) +
                              " failed: expected " + std::to_string(componentCount) +
                              " structured components for " + std::string(typeName));
            return false;
        }
        if (structured_byte_type(typeName)) {
            auto* values = reinterpret_cast<std::uint8_t*>(storage.structured.data());
            for (std::size_t i = 0; i < componentCount; ++i) {
                if (value.components[i] < 0.0 || value.components[i] > 255.0) {
                    detail::set_error(std::string("Unity Inspect::") + std::string(context) +
                                      " failed: Color32 component is out of range");
                    return false;
                }
                values[i] = static_cast<std::uint8_t>(value.components[i]);
            }
        } else if (structured_integer_type(typeName)) {
            auto* values = reinterpret_cast<std::int32_t*>(storage.structured.data());
            for (std::size_t i = 0; i < componentCount; ++i) {
                if (value.components[i] < static_cast<double>(std::numeric_limits<std::int32_t>::min()) ||
                    value.components[i] > static_cast<double>(std::numeric_limits<std::int32_t>::max())) {
                    detail::set_error(std::string("Unity Inspect::") + std::string(context) +
                                      " failed: integer struct component is out of range");
                    return false;
                }
                values[i] = static_cast<std::int32_t>(value.components[i]);
            }
        } else {
            auto* values = reinterpret_cast<float*>(storage.structured.data());
            for (std::size_t i = 0; i < componentCount; ++i) {
                if (value.components[i] > static_cast<double>(std::numeric_limits<float>::max()) ||
                    value.components[i] < -static_cast<double>(std::numeric_limits<float>::max())) {
                    detail::set_error(std::string("Unity Inspect::") + std::string(context) +
                                      " failed: floating struct component is out of range");
                    return false;
                }
                values[i] = static_cast<float>(value.components[i]);
            }
        }
        pointer = storage.structured.data();
        return true;
    }
    detail::set_error(std::string("Unity Inspect::") + std::string(context) + " failed: unsupported value type write: " + std::string(typeName));
    return false;
}
inline bool boxed_value_write_pointer(const void* targetType, const ValueInfo& value,
                                      void*& pointer, std::string_view context) {
    if (value.kind != ValueKind::ValueType || !value.object) {
        detail::set_error(std::string("Unity Inspect::") + std::string(context) +
                          " failed: expected a boxed value-type reference");
        return false;
    }
    const Object boxed{value.object};
    if (!IsBoxedValueOfType(boxed, targetType)) {
        detail::set_error(std::string("Unity Inspect::") + std::string(context) +
                          " failed: boxed value type does not match destination type");
        return false;
    }
    pointer = detail::Backend::object_unbox(value.object);
    if (!pointer) {
        detail::set_error(std::string("Unity Inspect::") + std::string(context) +
                          " failed: object_unbox failed for boxed value");
        detail::append_backend_error();
        return false;
    }
    return true;
}
inline bool reference_write_value(std::string_view typeName, const ValueInfo& value, WriteStorage& storage, std::string_view context) {
    const std::string normalized = detail::normalized_type_name(typeName);
    if (value.kind == ValueKind::Null) { storage.reference = nullptr; return true; }
    if (normalized == "system.string") {
        if (value.kind != ValueKind::String) { detail::set_error(std::string("Unity Inspect::") + std::string(context) + " failed: expected String or Null for " + std::string(typeName) + ", got " + value_kind_name(value.kind)); return false; }
        storage.reference = value.object ? value.object : detail::Backend::new_string(value.display);
        if (!storage.reference) { detail::set_error(std::string("Unity Inspect::") + std::string(context) + " failed: managed string allocation failed"); detail::append_backend_error(); return false; }
        return true;
    }
    if (value.kind != ValueKind::ObjectReference && value.kind != ValueKind::ArrayReference) { detail::set_error(std::string("Unity Inspect::") + std::string(context) + " failed: expected ObjectReference, ArrayReference, or Null for " + std::string(typeName) + ", got " + value_kind_name(value.kind)); return false; }
    if (!value.object) { detail::set_error(std::string("Unity Inspect::") + std::string(context) + " failed: reference value object is null but kind is " + value_kind_name(value.kind)); return false; }
    storage.reference = value.object;
    return true;
}
inline std::string array_element_type_name(std::string_view arrayTypeName) {
    if (arrayTypeName.size() >= 2 && arrayTypeName.substr(arrayTypeName.size() - 2) == "[]")
        return std::string(arrayTypeName.substr(0, arrayTypeName.size() - 2));
    return {};
}
inline int scalar_element_size(std::string_view typeName) {
    const std::string normalized = detail::normalized_type_name(typeName);
    if (normalized == "system.boolean" || normalized == "system.byte" || normalized == "system.sbyte") return 1;
    if (normalized == "system.int16" || normalized == "system.uint16" || normalized == "system.char") return 2;
    if (normalized == "system.int32" || normalized == "system.uint32" || normalized == "system.single") return 4;
    if (normalized == "system.int64" || normalized == "system.uint64" ||
        normalized == "system.intptr" || normalized == "system.uintptr" ||
        normalized == "system.double") return 8;
    return 0;
}
inline int structured_element_size(std::string_view typeName) {
    const std::size_t count = structured_component_count(typeName);
    if (count == 0) return 0;
    return static_cast<int>(count * (structured_byte_type(typeName) ? sizeof(std::uint8_t)
                                    : structured_integer_type(typeName) ? sizeof(std::int32_t)
                                                                       : sizeof(float)));
}
inline std::size_t ArrayLength(const ValueInfo& array) {
    detail::clear_error();
    if (array.kind != ValueKind::ArrayReference || !array.object) {
        detail::set_error("Unity Inspect array length failed: value is not an array reference");
        return 0;
    }
    if (!detail::Backend::has_array_length()) {
        detail::set_error("Unity Inspect array length failed: backend array_length API is unavailable");
        detail::append_backend_error();
        return 0;
    }
    return detail::Backend::array_length(array.object);
}
inline bool validate_array_element_access(const ValueInfo& array, std::size_t index, std::size_t& length) {
    if (array.kind != ValueKind::ArrayReference || !array.object) { detail::set_error("Unity Inspect array element access failed: value is not an array reference"); return false; }
    if (!detail::Backend::has_array_length()) { detail::set_error("Unity Inspect array element access failed: backend array_length API is unavailable"); detail::append_backend_error(); return false; }
    length = ArrayLength(array);
    if (index >= length) { detail::set_error("Unity Inspect array element access failed: index out of range"); return false; }
    return true;
}
inline ValueInfo ReadArrayElement(const ValueInfo& array, std::size_t index) {
    detail::clear_error();
    std::size_t length = 0;
    if (!validate_array_element_access(array, index, length)) return unavailable_value(array.type_name, detail::fallback_error() ? detail::fallback_error() : "array element access failed");
    (void)length;
    const std::string elementType = array_element_type_name(array.type_name);
    if (elementType.empty()) return unavailable_value(array.type_name, std::string("array element type unsupported or not single-dimensional: ") + array.type_name);
    int elementSize = scalar_element_size(elementType);
    if (elementSize == 0) elementSize = structured_element_size(elementType);
    const auto* arrayClass = static_cast<const URK::il2cpp::Class*>(
        detail::Backend::object_get_class(array.object));
    const auto* elementClass = arrayClass ? URK::il2cpp::class_get_element_class(arrayClass) : nullptr;
    if (elementSize == 0 && elementClass && URK::il2cpp::class_is_valuetype(elementClass)) {
        std::uint32_t alignment = 0;
        elementSize = detail::Backend::class_value_size(elementClass, &alignment);
        if (elementSize <= 0) {
            detail::set_error(std::string("Unity Inspect::ReadArrayElement failed: value size unavailable for ") + elementType);
            return unavailable_value(elementType, detail::fallback_error());
        }
        void* slot = detail::Backend::array_addr_with_size(array.object, elementSize, index);
        if (!slot) { detail::set_error(std::string("Unity Inspect::ReadArrayElement failed: array_addr_with_size failed for ") + elementType); detail::append_backend_error(); return unavailable_value(elementType, detail::fallback_error() ? detail::fallback_error() : "array element address failed"); }
        void* boxed = detail::Backend::value_box(elementClass, slot);
        if (!boxed) { detail::set_error(std::string("Unity Inspect::ReadArrayElement failed: value_box failed for ") + elementType); detail::append_backend_error(); return unavailable_value(elementType, detail::fallback_error() ? detail::fallback_error() : "array element boxing failed"); }
        return object_reference_value(elementType, boxed);
    }
    if (elementSize > 0) {
        void* slot = detail::Backend::array_addr_with_size(array.object, elementSize, index);
        if (!slot) { detail::set_error(std::string("Unity Inspect::ReadArrayElement failed: array_addr_with_size failed for ") + elementType); detail::append_backend_error(); return unavailable_value(elementType, detail::fallback_error() ? detail::fallback_error() : "array element address failed"); }
        return structured_component_count(elementType) != 0
            ? structured_from_pointer(elementType, slot)
            : scalar_from_pointer(elementType, slot);
    }
    if (!detail::Backend::has_array_ref_at()) { detail::set_error("Unity Inspect::ReadArrayElement failed: backend array_ref_at API is unavailable"); detail::append_backend_error(); return unavailable_value(elementType, detail::fallback_error() ? detail::fallback_error() : "array reference element access failed"); }
    void* ref = detail::Backend::array_ref_at(array.object, index);
    return detail::normalized_type_name(elementType) == "system.string" ? string_value(elementType, ref) : object_reference_value(elementType, ref);
}
inline bool SetArrayElement(const ValueInfo& array, std::size_t index, const ValueInfo& value) {
    detail::clear_error();
    std::size_t length = 0;
    if (!validate_array_element_access(array, index, length)) return false;
    (void)length;
    const std::string elementType = array_element_type_name(array.type_name);
    if (elementType.empty()) { detail::set_error(std::string("Unity Inspect::SetArrayElement failed: array element type unsupported or not single-dimensional: ") + array.type_name); return false; }
    WriteStorage storage{};
    int elementSize = scalar_element_size(elementType);
    if (elementSize == 0) elementSize = structured_element_size(elementType);
    const auto* arrayClass = static_cast<const URK::il2cpp::Class*>(
        detail::Backend::object_get_class(array.object));
    const auto* elementClass = arrayClass ? URK::il2cpp::class_get_element_class(arrayClass) : nullptr;
    if (elementSize == 0 && elementClass && URK::il2cpp::class_is_valuetype(elementClass)) {
        if (value.kind != ValueKind::ValueType || !value.object ||
            detail::Backend::object_get_class(value.object) != elementClass) {
            detail::set_error(std::string("Unity Inspect::SetArrayElement failed: expected boxed ") + elementType);
            return false;
        }
        std::uint32_t alignment = 0;
        elementSize = detail::Backend::class_value_size(elementClass, &alignment);
        if (elementSize <= 0) { detail::set_error(std::string("Unity Inspect::SetArrayElement failed: value size unavailable for ") + elementType); return false; }
        void* source = detail::Backend::object_unbox(value.object);
        void* slot = detail::Backend::array_addr_with_size(array.object, elementSize, index);
        if (!source || !slot) { detail::set_error(std::string("Unity Inspect::SetArrayElement failed: could not access ") + elementType); detail::append_backend_error(); return false; }
        std::memcpy(slot, source, static_cast<std::size_t>(elementSize));
        return true;
    }
    if (elementSize <= 0) {
        if (!detail::Backend::has_array_set_ref()) { detail::set_error(std::string("Unity Inspect::SetArrayElement failed: backend array_set_ref API is unavailable for ") + array.type_name); detail::append_backend_error(); return false; }
        if (!reference_write_value(elementType, value, storage, "SetArrayElement")) return false;
        if (!detail::Backend::array_set_ref(array.object, index, storage.reference)) { detail::set_error(std::string("Unity Inspect::SetArrayElement failed: reference array write failed for ") + array.type_name); detail::append_backend_error(); return false; }
        return true;
    }
    void* source = nullptr;
    if (!scalar_write_pointer(elementType, value, storage, source, "SetArrayElement")) return false;
    void* slot = detail::Backend::array_addr_with_size(array.object, elementSize, index);
    if (!slot) { detail::set_error(std::string("Unity Inspect::SetArrayElement failed: array_addr_with_size failed for ") + elementType); detail::append_backend_error(); return false; }
    std::memcpy(slot, source, static_cast<std::size_t>(elementSize));
    return true;
}
inline bool method_argument_pointer(const MethodParamInfo& parameter, const ValueInfo& value, WriteStorage& storage, void*& pointer) {
    if (parameter.is_opaque) {
        detail::set_error(std::string("Unity Inspect::InvokeMethod cannot marshal runtime-specific parameter type: ") +
                          parameter.type_name);
        return false;
    }
    const TypeInfo type = DescribeType(parameter.type);
    if (!type.handle && parameter.type_name.empty()) { detail::set_error("Unity Inspect::InvokeMethod failed: parameter type metadata is unavailable"); return false; }
    const std::string normalized = detail::normalized_type_name(parameter.type_name);
    if (type.is_enum) {
        const std::string underlying = enum_underlying_type_name(parameter.type);
        if (underlying.empty()) { detail::set_error(std::string("Unity Inspect::InvokeMethod failed: enum parameter underlying type unavailable: ") + parameter.name); return false; }
        return scalar_write_pointer(underlying, value, storage, pointer, "InvokeMethod");
    }
    if (normalized == "system.string" || !type.is_value_type)
        return reference_write_value(parameter.type_name, value, storage, "InvokeMethod") ? (pointer = storage.reference, true) : false;
    return value.kind == ValueKind::ValueType
        ? boxed_value_write_pointer(parameter.type, value, pointer, "InvokeMethod")
        : scalar_write_pointer(parameter.type_name, value, storage, pointer, "InvokeMethod");
}
inline ValueInfo void_value() {
    ValueInfo out{};
    out.kind = ValueKind::Null;
    out.type_name = "System.Void";
    out.display = "void";
    out.readable = true;
    return out;
}
inline ValueInfo invoke_result_value(std::string typeName, const void* type, void* result, std::string_view methodName) {
    const std::string normalized = detail::normalized_type_name(typeName);
    if (normalized == "system.void" || typeName.empty()) return void_value();
    if (normalized == "system.string") return string_value(std::move(typeName), result);
    const TypeInfo resultType = DescribeType(type);
    if (!resultType.is_value_type) return object_reference_value(std::move(typeName), result);
    if (!result) return unavailable_value(std::move(typeName), std::string("Unity Inspect::InvokeMethod failed: value-type result is null: ") + std::string(methodName));
    void* raw = detail::Backend::object_unbox(result);
    if (!raw) { detail::set_error(std::string("Unity Inspect::InvokeMethod failed: object_unbox failed for result: ") + std::string(methodName)); detail::append_backend_error(); return unavailable_value(std::move(typeName), detail::fallback_error() ? detail::fallback_error() : "method result unbox failed"); }
    if (resultType.is_enum) {
        const std::string underlying = enum_underlying_type_name(type);
        if (underlying.empty()) return unavailable_value(std::move(typeName), std::string("enum result underlying type unavailable: ") + std::string(methodName));
        return enum_from_pointer(std::move(typeName), underlying, raw);
    }
    ValueInfo scalar = scalar_from_pointer(typeName, raw);
    // `runtime_invoke` already returned a boxed custom value type. Keep that
    // box inspectable instead of dropping it as an opaque placeholder.
    return scalar.kind == ValueKind::ValueType
        ? object_reference_value(std::move(typeName), result)
        : scalar;
}
inline ValueInfo InvokeMethod(Object object, const MethodInfo& method, const std::vector<ValueInfo>& arguments = {}) {
    detail::clear_error();
    if (!method.handle) return unavailable_value(method.return_type, "Unity Inspect::InvokeMethod failed: method handle is null");
    if (method.return_type_is_opaque) {
        return unavailable_value(method.return_type,
                                 std::string("Unity Inspect::InvokeMethod cannot safely inspect runtime-specific return type: ") +
                                     method.return_type);
    }
    if (arguments.size() != method.parameters.size()) return unavailable_value(method.return_type, std::string("Unity Inspect::InvokeMethod failed: argument count mismatch for ") + method.name);
    if (!method.is_static && !object.handle()) return unavailable_value(method.return_type, std::string("Unity Inspect::InvokeMethod failed: target object is null for instance method: ") + method.name);
    std::vector<WriteStorage> storage(arguments.size());
    std::vector<void*> argv(arguments.size(), nullptr);
    for (std::size_t i = 0; i < arguments.size(); ++i)
        if (!method_argument_pointer(method.parameters[i], arguments[i], storage[i], argv[i]))
            return unavailable_value(method.return_type, detail::fallback_error() ? detail::fallback_error() : "method argument conversion failed");
    void* result = nullptr;
    void* ex = nullptr;
    if (!detail::Backend::runtime_invoke(method.handle, method.is_static ? nullptr : object.handle(), argv.empty() ? nullptr : argv.data(), &result, &ex) || ex) { detail::set_error(std::string("Unity Inspect::InvokeMethod failed: runtime_invoke threw or failed: ") + method.name); detail::append_backend_error(); return unavailable_value(method.return_type, detail::fallback_error() ? detail::fallback_error() : "method invocation failed"); }
    return invoke_result_value(method.return_type, method.return_type_handle, result, method.name);
}
inline bool write_field_raw(Object object, const FieldInfo& field, void* value) {
    if (!field.handle) { detail::set_error("Unity Inspect::SetField failed: field handle is null"); return false; }
    if (field.is_static) {
        if (!field.declaring_type.handle) { detail::set_error("Unity Inspect::SetField failed: static field declaring type is unavailable"); return false; }
        if (!detail::Backend::field_static_set_value(field.declaring_type.handle, field.handle, value)) { detail::set_error(std::string("Unity Inspect::SetField failed: static field write failed: ") + field.name); detail::append_backend_error(); return false; }
        return true;
    }
    if (!object.handle()) { detail::set_error("Unity Inspect::SetField failed: target object is null"); return false; }
    if (!detail::Backend::field_set_value(object.handle(), field.handle, value)) { detail::set_error(std::string("Unity Inspect::SetField failed: field write failed: ") + field.name); detail::append_backend_error(); return false; }
    return true;
}
inline bool read_field_scalar_pointer(Object object, const FieldInfo& field, std::string_view typeName, WriteStorage& storage, void*& pointer) {
    const std::string normalized = detail::normalized_type_name(typeName);
    if (normalized == "system.boolean") { pointer = &storage.b; return read_field_raw(object, field, pointer); }
    if (normalized == "system.sbyte") { pointer = &storage.i8; return read_field_raw(object, field, pointer); }
    if (normalized == "system.int16") { pointer = &storage.i16; return read_field_raw(object, field, pointer); }
    if (normalized == "system.int32") { pointer = &storage.i32; return read_field_raw(object, field, pointer); }
    if (normalized == "system.int64") { pointer = &storage.i64; return read_field_raw(object, field, pointer); }
    if (normalized == "system.byte") { pointer = &storage.u8; return read_field_raw(object, field, pointer); }
    if (normalized == "system.uint16" || normalized == "system.char") { pointer = &storage.u16; return read_field_raw(object, field, pointer); }
    if (normalized == "system.uint32") { pointer = &storage.u32; return read_field_raw(object, field, pointer); }
    if (normalized == "system.uint64") { pointer = &storage.u64; return read_field_raw(object, field, pointer); }
    detail::set_error(std::string("Unity Inspect::ReadField failed: unsupported scalar field type: ") + std::string(typeName));
    return false;
}
inline ValueInfo ReadField(Object object, const FieldInfo& field) {
    detail::clear_error();
    if (field.type_is_opaque)
        return unavailable_value(field.type_name, "field type requires runtime-specific marshalling");
    const std::string normalized = detail::normalized_type_name(field.type_name);
    if (normalized == "system.string") { void* ref = nullptr; return read_field_raw(object, field, &ref) ? string_value(field.type_name, ref) : unavailable_value(field.type_name, detail::fallback_error() ? detail::fallback_error() : "field read failed"); }
    if (!field.is_value_type) { void* ref = nullptr; return read_field_raw(object, field, &ref) ? object_reference_value(field.type_name, ref) : unavailable_value(field.type_name, detail::fallback_error() ? detail::fallback_error() : "field read failed"); }
    if (field.is_enum) {
        const std::string underlying = enum_underlying_type_name(field.type);
        if (underlying.empty()) return unavailable_value(field.type_name, std::string("enum underlying type unavailable: ") + field.name);
        WriteStorage storage{};
        void* raw = nullptr;
        return read_field_scalar_pointer(object, field, underlying, storage, raw) ? enum_from_pointer(field.type_name, underlying, raw) : unavailable_value(field.type_name, detail::fallback_error() ? detail::fallback_error() : "enum field read failed");
    }
    if (normalized == "system.boolean") { bool value{}; return read_field_raw(object, field, &value) ? scalar_from_pointer(field.type_name, &value) : unavailable_value(field.type_name, detail::fallback_error() ? detail::fallback_error() : "field read failed"); }
    if (normalized == "system.int16") { std::int16_t value{}; return read_field_raw(object, field, &value) ? scalar_from_pointer(field.type_name, &value) : unavailable_value(field.type_name, detail::fallback_error() ? detail::fallback_error() : "field read failed"); }
    if (normalized == "system.int32") { std::int32_t value{}; return read_field_raw(object, field, &value) ? scalar_from_pointer(field.type_name, &value) : unavailable_value(field.type_name, detail::fallback_error() ? detail::fallback_error() : "field read failed"); }
    if (normalized == "system.int64") { std::int64_t value{}; return read_field_raw(object, field, &value) ? scalar_from_pointer(field.type_name, &value) : unavailable_value(field.type_name, detail::fallback_error() ? detail::fallback_error() : "field read failed"); }
    if (normalized == "system.intptr") { std::intptr_t value{}; return read_field_raw(object, field, &value) ? scalar_from_pointer(field.type_name, &value) : unavailable_value(field.type_name, detail::fallback_error() ? detail::fallback_error() : "field read failed"); }
    if (normalized == "system.sbyte") { std::int8_t value{}; return read_field_raw(object, field, &value) ? scalar_from_pointer(field.type_name, &value) : unavailable_value(field.type_name, detail::fallback_error() ? detail::fallback_error() : "field read failed"); }
    if (normalized == "system.uint16" || normalized == "system.char") { std::uint16_t value{}; return read_field_raw(object, field, &value) ? scalar_from_pointer(field.type_name, &value) : unavailable_value(field.type_name, detail::fallback_error() ? detail::fallback_error() : "field read failed"); }
    if (normalized == "system.uint32") { std::uint32_t value{}; return read_field_raw(object, field, &value) ? scalar_from_pointer(field.type_name, &value) : unavailable_value(field.type_name, detail::fallback_error() ? detail::fallback_error() : "field read failed"); }
    if (normalized == "system.uint64") { std::uint64_t value{}; return read_field_raw(object, field, &value) ? scalar_from_pointer(field.type_name, &value) : unavailable_value(field.type_name, detail::fallback_error() ? detail::fallback_error() : "field read failed"); }
    if (normalized == "system.uintptr") { std::uintptr_t value{}; return read_field_raw(object, field, &value) ? scalar_from_pointer(field.type_name, &value) : unavailable_value(field.type_name, detail::fallback_error() ? detail::fallback_error() : "field read failed"); }
    if (normalized == "system.byte") { std::uint8_t value{}; return read_field_raw(object, field, &value) ? scalar_from_pointer(field.type_name, &value) : unavailable_value(field.type_name, detail::fallback_error() ? detail::fallback_error() : "field read failed"); }
    if (normalized == "system.single") { float value{}; return read_field_raw(object, field, &value) ? scalar_from_pointer(field.type_name, &value) : unavailable_value(field.type_name, detail::fallback_error() ? detail::fallback_error() : "field read failed"); }
    if (normalized == "system.double") { double value{}; return read_field_raw(object, field, &value) ? scalar_from_pointer(field.type_name, &value) : unavailable_value(field.type_name, detail::fallback_error() ? detail::fallback_error() : "field read failed"); }
    if (structured_component_count(field.type_name) != 0) {
        alignas(16) std::array<std::uint8_t, 64> value{};
        return read_field_raw(object, field, value.data())
            ? structured_from_pointer(field.type_name, value.data())
            : unavailable_value(field.type_name, detail::fallback_error() ? detail::fallback_error() : "field read failed");
    }
    // Box non-scalar structs so the explorer can follow them into the Object
    // Inspector (Vector3, Color, custom structs, etc.) without raw layouts.
    void* boxed = detail::Backend::field_get_value_object(field.handle, object.handle());
    if (boxed) return object_reference_value(field.type_name, boxed);
    return value_type_placeholder(field.type_name);
}
inline ValueInfo ReadProperty(Object object, const PropertyInfo& property) {
    detail::clear_error();
    if (property.type_is_opaque)
        return unavailable_value(property.type_name, "property type requires runtime-specific marshalling");
    if (!property.can_read || !property.get_method) return unavailable_value(property.type_name, std::string("property is not readable: ") + property.name);
    void* result = nullptr;
    void* ex = nullptr;
    if (!detail::Backend::runtime_invoke(property.get_method, property.is_static ? nullptr : object.handle(), nullptr, &result, &ex) || ex) { detail::set_error(std::string("Unity Inspect::ReadProperty failed: getter threw or could not be invoked: ") + property.name); detail::append_backend_error(); return unavailable_value(property.type_name, detail::fallback_error() ? detail::fallback_error() : "property read failed"); }
    const std::string normalized = detail::normalized_type_name(property.type_name);
    if (normalized == "system.string") return string_value(property.type_name, result);
    if (!property.is_value_type) return object_reference_value(property.type_name, result);
    if (property.is_enum) {
        const std::string underlying = enum_underlying_type_name(property.type);
        if (underlying.empty()) return unavailable_value(property.type_name, std::string("enum underlying type unavailable: ") + property.name);
        void* raw = detail::Backend::object_unbox(result);
        if (!raw) { detail::set_error(std::string("Unity Inspect::ReadProperty failed: enum object_unbox failed: ") + property.name); detail::append_backend_error(); return unavailable_value(property.type_name, detail::fallback_error() ? detail::fallback_error() : "enum property unbox failed"); }
        return enum_from_pointer(property.type_name, underlying, raw);
    }
    void* raw = detail::Backend::object_unbox(result);
    if (!raw) { detail::set_error(std::string("Unity Inspect::ReadProperty failed: object_unbox failed: ") + property.name); detail::append_backend_error(); return unavailable_value(property.type_name, detail::fallback_error() ? detail::fallback_error() : "property unbox failed"); }
    ValueInfo scalar = scalar_from_pointer(property.type_name, raw);
    // A getter already returns a boxed value type. Preserve that box as an
    // inspectable reference when the type is not one of the scalar helpers.
    return scalar.kind == ValueKind::ValueType
        ? object_reference_value(property.type_name, result)
        : scalar;
}
inline bool SetField(Object object, const FieldInfo& field, const ValueInfo& value) {
    detail::clear_error();
    if (field.type_is_opaque) {
        detail::set_error(std::string("Unity Inspect::SetField cannot marshal runtime-specific type: ") + field.type_name);
        return false;
    }
    if (!field.handle) { detail::set_error("Unity Inspect::SetField failed: field handle is null"); return false; }
    WriteStorage storage{};
    const std::string normalized = detail::normalized_type_name(field.type_name);
    if (field.is_enum) {
        const std::string underlying = enum_underlying_type_name(field.type);
        if (underlying.empty()) { detail::set_error(std::string("Unity Inspect::SetField failed: enum underlying type unavailable: ") + field.type_name); return false; }
        void* pointer = nullptr;
        if (!scalar_write_pointer(underlying, value, storage, pointer, "SetField")) return false;
        return write_field_raw(object, field, pointer);
    }
    if (normalized == "system.string" || !field.is_value_type) {
        if (!reference_write_value(field.type_name, value, storage, "SetField")) return false;
        // A native local pointer is not a managed GC root. `field_set_value`
        // can allocate/run a collection, so root every supplied reference
        // until the write barrier has installed it in the destination field.
        ObjectHandle rooted{};
        if (storage.reference) {
            rooted = PinObject(Object{storage.reference});
            if (!rooted.handle) return false;
        }
        const bool written = write_field_raw(
            object, field, detail::Backend::field_reference_write_pointer(storage.reference));
        FreeObjectHandle(rooted);
        return written;
    }
    void* pointer = nullptr;
    if (value.kind == ValueKind::ValueType) {
        if (!boxed_value_write_pointer(field.type, value, pointer, "SetField")) return false;
    } else if (!scalar_write_pointer(field.type_name, value, storage, pointer, "SetField")) return false;
    return write_field_raw(object, field, pointer);
}
inline bool SetProperty(Object object, const PropertyInfo& property, const ValueInfo& value) {
    detail::clear_error();
    if (property.type_is_opaque) {
        detail::set_error(std::string("Unity Inspect::SetProperty cannot marshal runtime-specific type: ") + property.type_name);
        return false;
    }
    if (!property.can_write || !property.set_method) { detail::set_error(std::string("Unity Inspect::SetProperty failed: property is not writable: ") + property.name); return false; }
    WriteStorage storage{};
    void* arg = nullptr;
    const std::string normalized = detail::normalized_type_name(property.type_name);
    if (property.is_enum) {
        const std::string underlying = enum_underlying_type_name(property.type);
        if (underlying.empty()) { detail::set_error(std::string("Unity Inspect::SetProperty failed: enum underlying type unavailable: ") + property.type_name); return false; }
        if (!scalar_write_pointer(underlying, value, storage, arg, "SetProperty")) return false;
    } else
    if (normalized == "system.string" || !property.is_value_type) {
        if (!reference_write_value(property.type_name, value, storage, "SetProperty")) return false;
        arg = storage.reference;
    } else if (value.kind == ValueKind::ValueType) {
        if (!boxed_value_write_pointer(property.type, value, arg, "SetProperty")) return false;
    } else if (!scalar_write_pointer(property.type_name, value, storage, arg, "SetProperty")) {
        return false;
    }
    void* args[] = {arg};
    void* ex = nullptr;
    ObjectHandle rooted{};
    // Runtime setters can allocate and collect too.  Do not leave a string
    // or object reference reachable only through a native stack local.
    if (storage.reference) {
        rooted = PinObject(Object{storage.reference});
        if (!rooted.handle) return false;
    }
    const bool invoked = detail::Backend::runtime_invoke(property.set_method, property.is_static ? nullptr : object.handle(), args, nullptr, &ex) && !ex;
    FreeObjectHandle(rooted);
    if (!invoked) { detail::set_error(std::string("Unity Inspect::SetProperty failed: setter threw or could not be invoked: ") + property.name); detail::append_backend_error(); return false; }
    return true;
}
inline bool SetFieldFromBox(Object object, const FieldInfo& field, Object boxedValue) {
    detail::clear_error();
    if (!field.is_value_type || !boxedValue.handle()) {
        detail::set_error("Unity Inspect::SetFieldFromBox requires a value-type field and boxed value");
        return false;
    }
    void* raw = detail::Backend::object_unbox(boxedValue.handle());
    if (!raw) {
        detail::set_error(std::string("Unity Inspect::SetFieldFromBox failed: object_unbox failed for ") + field.name);
        detail::append_backend_error();
        return false;
    }
    return write_field_raw(object, field, raw);
}
inline bool SetPropertyFromBox(Object object, const PropertyInfo& property, Object boxedValue) {
    detail::clear_error();
    if (!property.is_value_type || !property.can_write || !property.set_method || !boxedValue.handle()) {
        detail::set_error("Unity Inspect::SetPropertyFromBox requires a writable value-type property and boxed value");
        return false;
    }
    void* raw = detail::Backend::object_unbox(boxedValue.handle());
    if (!raw) {
        detail::set_error(std::string("Unity Inspect::SetPropertyFromBox failed: object_unbox failed for ") + property.name);
        detail::append_backend_error();
        return false;
    }
    void* args[] = {raw};
    void* ex = nullptr;
    if (!detail::Backend::runtime_invoke(property.set_method, property.is_static ? nullptr : object.handle(), args, nullptr, &ex) || ex) {
        detail::set_error(std::string("Unity Inspect::SetPropertyFromBox failed: setter threw or could not be invoked: ") + property.name);
        detail::append_backend_error();
        return false;
    }
    return true;
}
inline void DumpFields(TypeRef type, DiagnosticSink sink = nullptr) { for (const auto& f : Fields(type)) { char line[512]{}; std::snprintf(line, sizeof(line), "field %s : %s flags=0x%08x", f.name.c_str(), f.type_name.c_str(), f.flags); emit(sink, line); } }
inline void DumpMethods(TypeRef type, DiagnosticSink sink = nullptr) { for (const auto& m : Methods(type)) { std::string line = "method " + m.name + "("; for (std::size_t i = 0; i < m.parameters.size(); ++i) { if (i) line += ", "; line += m.parameters[i].type_name.empty() ? "<unavailable>" : m.parameters[i].type_name; } line += ") -> "; line += m.return_type.empty() ? "<unavailable>" : m.return_type; emit(sink, line.c_str()); } }
inline void DumpProperties(TypeRef type, DiagnosticSink sink = nullptr) { for (const auto& p : Properties(type)) { char line[512]{}; std::snprintf(line, sizeof(line), "property %s : %s flags=0x%08x", p.name.c_str(), p.type_name.c_str(), p.flags); emit(sink, line); } }
}

}
namespace Unity {
// URK_UNITY_INSPECT_ALIASES_BEGIN
namespace Inspect {
using FieldInfo = URK::Unity::Inspect::FieldInfo;
using MethodInfo = URK::Unity::Inspect::MethodInfo;
using MethodParamInfo = URK::Unity::Inspect::MethodParamInfo;
using ObjectRefInfo = URK::Unity::Inspect::ObjectRefInfo;
using ObjectHandle = URK::Unity::Inspect::ObjectHandle;
using PropertyInfo = URK::Unity::Inspect::PropertyInfo;
using TypeInfo = URK::Unity::Inspect::TypeInfo;
using ValueInfo = URK::Unity::Inspect::ValueInfo;
using ValueKind = URK::Unity::Inspect::ValueKind;
inline TypeInfo DescribeClass(const void* klass) {
    return URK::Unity::Inspect::DescribeClass(klass);
}
inline TypeInfo TypeOf(Object object) {
    return URK::Unity::Inspect::TypeOf(object);
}
inline ObjectRefInfo DescribeObject(Object object) {
    return URK::Unity::Inspect::DescribeObject(object);
}
inline ObjectRefInfo ExpandValue(const ValueInfo& value) {
    return URK::Unity::Inspect::ExpandValue(value);
}
inline ObjectHandle PinObject(Object object, bool pinned = false) {
    return URK::Unity::Inspect::PinObject(object, pinned);
}
inline ObjectHandle PinValue(const ValueInfo& value, bool pinned = false) {
    return URK::Unity::Inspect::PinValue(value, pinned);
}
inline ObjectHandle WeakObject(Object object, bool trackResurrection = false) {
    return URK::Unity::Inspect::WeakObject(object, trackResurrection);
}
inline Object ResolveObjectHandle(const ObjectHandle& handle) {
    return URK::Unity::Inspect::ResolveObjectHandle(handle);
}
inline void FreeObjectHandle(ObjectHandle& handle) {
    URK::Unity::Inspect::FreeObjectHandle(handle);
}
inline std::vector<FieldInfo> Fields(TypeRef type, bool includeInherited = true) {
    return URK::Unity::Inspect::Fields(type, includeInherited);
}
inline std::vector<FieldInfo> Fields(Object object, bool includeInherited = true) {
    return URK::Unity::Inspect::Fields(object, includeInherited);
}
inline std::vector<FieldInfo> Fields(const ObjectRefInfo& object, bool includeInherited = true) {
    return URK::Unity::Inspect::Fields(object, includeInherited);
}
inline std::vector<MethodInfo> Methods(TypeRef type, bool includeInherited = true) {
    return URK::Unity::Inspect::Methods(type, includeInherited);
}
inline std::vector<MethodInfo> Methods(Object object, bool includeInherited = true) {
    return URK::Unity::Inspect::Methods(object, includeInherited);
}
inline std::vector<MethodInfo> Methods(const ObjectRefInfo& object, bool includeInherited = true) {
    return URK::Unity::Inspect::Methods(object, includeInherited);
}
inline std::vector<PropertyInfo> Properties(TypeRef type, bool includeInherited = true) {
    return URK::Unity::Inspect::Properties(type, includeInherited);
}
inline std::vector<PropertyInfo> Properties(Object object, bool includeInherited = true) {
    return URK::Unity::Inspect::Properties(object, includeInherited);
}
inline std::vector<PropertyInfo> Properties(const ObjectRefInfo& object, bool includeInherited = true) {
    return URK::Unity::Inspect::Properties(object, includeInherited);
}
inline ValueInfo ReadField(Object object, const FieldInfo& field) {
    return URK::Unity::Inspect::ReadField(object, field);
}
inline ValueInfo ReadProperty(Object object, const PropertyInfo& property) {
    return URK::Unity::Inspect::ReadProperty(object, property);
}
inline ValueInfo ReadArrayElement(const ValueInfo& array, std::size_t index) {
    return URK::Unity::Inspect::ReadArrayElement(array, index);
}
inline ValueInfo InvokeMethod(Object object, const MethodInfo& method, const std::vector<ValueInfo>& arguments = {}) {
    return URK::Unity::Inspect::InvokeMethod(object, method, arguments);
}
inline bool SetField(Object object, const FieldInfo& field, const ValueInfo& value) {
    return URK::Unity::Inspect::SetField(object, field, value);
}
inline bool SetProperty(Object object, const PropertyInfo& property, const ValueInfo& value) {
    return URK::Unity::Inspect::SetProperty(object, property, value);
}
inline bool SetArrayElement(const ValueInfo& array, std::size_t index, const ValueInfo& value) {
    return URK::Unity::Inspect::SetArrayElement(array, index, value);
}
inline void DumpFields(TypeRef type, DiagnosticSink sink = nullptr) {
    URK::Unity::Inspect::DumpFields(type, sink);
}
inline void DumpMethods(TypeRef type, DiagnosticSink sink = nullptr) {
    URK::Unity::Inspect::DumpMethods(type, sink);
}
inline void DumpProperties(TypeRef type, DiagnosticSink sink = nullptr) {
    URK::Unity::Inspect::DumpProperties(type, sink);
}
}
}

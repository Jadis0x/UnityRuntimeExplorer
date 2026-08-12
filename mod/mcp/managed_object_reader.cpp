// Copyright (c) 2026 Jadis0x. All rights reserved.
#include "managed_object_reader.h"

#include <algorithm>
#include <cctype>
#include <limits>

namespace Explorer::Mcp::ManagedRead {
namespace {

std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

bool matches(std::string_view query, std::string_view name, std::string_view type,
             std::string_view declaring_type) {
    if (query.empty())
        return true;
    const std::string haystack = lowercase(std::string(name) + " " + std::string(type) + " " +
                                           std::string(declaring_type));
    return haystack.find(lowercase(std::string(query))) != std::string::npos;
}

std::string assembly_name(const URK::Unity::Inspect::TypeInfo& type) {
    if (!type.handle)
        return {};
    const char* value = URK::managed::class_get_assemblyname(type.handle);
    return value ? value : "";
}

} // namespace

ObjectPage read_object(URK::Unity::Object object, std::string_view member_query,
                       std::size_t member_limit, bool include_properties) {
    ObjectPage page{};
    if (!object || member_limit == 0)
        return page;
    page.type = URK::Unity::Inspect::TypeOf(object);
    page.assembly = assembly_name(page.type);
    if (!page.type.handle)
        return page;

    const auto fields = URK::Unity::Inspect::fields_from_class(
        static_cast<const URK::managed::Class*>(page.type.handle), true);
    for (const URK::Unity::Inspect::FieldInfo& field : fields) {
        if (!matches(member_query, field.name, field.type_name, field.declaring_type.full_name))
            continue;
        ++page.matching_members;
        if (page.fields.size() + page.properties.size() >= member_limit) {
            page.truncated = true;
            continue;
        }
        MemberValue result{};
        result.name = field.name;
        result.type_name = field.type_name;
        result.declaring_type = field.declaring_type.full_name;
        result.is_static = field.is_static;
        result.runtime_safe = !field.type_is_opaque;
        if (field.type_is_opaque) {
            result.capability_reason =
                "Runtime-specific type; metadata is available but generic reading is unsafe.";
            result.value = URK::Unity::Inspect::unavailable_value(field.type_name,
                                                                   result.capability_reason);
        } else {
            result.value = URK::Unity::Inspect::ReadField(object, field);
        }
        page.fields.push_back(std::move(result));
    }

    if (!include_properties)
        return page;
    const auto properties = URK::Unity::Inspect::properties_from_class(
        static_cast<const URK::managed::Class*>(page.type.handle), true);
    for (const URK::Unity::Inspect::PropertyInfo& property : properties) {
        if (!matches(member_query, property.name, property.type_name,
                     property.declaring_type.full_name))
            continue;
        ++page.matching_members;
        if (page.fields.size() + page.properties.size() >= member_limit) {
            page.truncated = true;
            continue;
        }
        MemberValue result{};
        result.name = property.name;
        result.type_name = property.type_name;
        result.declaring_type = property.declaring_type.full_name;
        result.property = true;
        result.is_static = property.is_static;
        result.runtime_safe = property.can_read && !property.type_is_opaque;
        if (!property.can_read) {
            result.capability_reason = "Property does not expose a getter.";
            result.value = URK::Unity::Inspect::unavailable_value(property.type_name,
                                                                   result.capability_reason);
        } else if (property.type_is_opaque) {
            result.capability_reason =
                "Runtime-specific type; metadata is available but generic reading is unsafe.";
            result.value = URK::Unity::Inspect::unavailable_value(property.type_name,
                                                                   result.capability_reason);
        } else {
            result.value = URK::Unity::Inspect::ReadProperty(object, property);
        }
        page.properties.push_back(std::move(result));
    }
    return page;
}

ArrayPage read_array(URK::Unity::Object object, std::size_t offset, std::size_t limit) {
    ArrayPage page{};
    if (!object || limit == 0)
        return page;
    page.type = URK::Unity::Inspect::TypeOf(object);
    const std::string type_name = page.type.full_name;
    if (!URK::Unity::Inspect::type_name_looks_array(type_name))
        return page;
    page.element_type = URK::Unity::Inspect::array_element_type_name(type_name);
    URK::Unity::Inspect::ValueInfo array{};
    array.kind = URK::Unity::Inspect::ValueKind::ArrayReference;
    array.type_name = type_name;
    array.object = object.handle();
    array.readable = true;
    page.length = URK::Unity::Inspect::ArrayLength(array);
    page.offset = std::min(offset, page.length);
    const std::size_t count = std::min(limit, page.length - page.offset);
    page.values.reserve(count);
    for (std::size_t index = 0; index < count; ++index)
        page.values.push_back(URK::Unity::Inspect::ReadArrayElement(array, page.offset + index));
    page.has_more = page.offset + count < page.length;
    return page;
}

ByteSnapshot read_bytes(URK::Unity::Object object, std::size_t maximum_bytes) {
    ByteSnapshot snapshot{};
    if (!object || maximum_bytes == 0) {
        snapshot.error = "A live byte array and a positive byte limit are required.";
        return snapshot;
    }
    const URK::Unity::Inspect::TypeInfo type = URK::Unity::Inspect::TypeOf(object);
    const std::string element = URK::Unity::Inspect::array_element_type_name(type.full_name);
    const std::string normalized = lowercase(element);
    if (!URK::Unity::Inspect::type_name_looks_array(type.full_name) ||
        (normalized != "system.byte" && normalized != "byte")) {
        snapshot.error = "The managed reference is not a System.Byte array.";
        return snapshot;
    }
    URK::Unity::Inspect::ValueInfo array{};
    array.kind = URK::Unity::Inspect::ValueKind::ArrayReference;
    array.type_name = type.full_name;
    array.object = object.handle();
    array.readable = true;
    snapshot.total_length = URK::Unity::Inspect::ArrayLength(array);
    const std::size_t count = std::min(snapshot.total_length, maximum_bytes);
    snapshot.truncated = count < snapshot.total_length;
    snapshot.bytes.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        const URK::Unity::Inspect::ValueInfo value =
            URK::Unity::Inspect::ReadArrayElement(array, index);
        if (!value.readable ||
            (value.kind != URK::Unity::Inspect::ValueKind::UnsignedInteger &&
             value.kind != URK::Unity::Inspect::ValueKind::SignedInteger)) {
            snapshot.error = value.display.empty() ? "A byte array element could not be read."
                                                    : value.display;
            snapshot.bytes.clear();
            return snapshot;
        }
        const std::uint64_t raw = value.kind == URK::Unity::Inspect::ValueKind::SignedInteger
            ? static_cast<std::uint64_t>(value.signed_value)
            : value.unsigned_value;
        if (raw > std::numeric_limits<std::uint8_t>::max()) {
            snapshot.error = "A managed byte array element was outside the byte range.";
            snapshot.bytes.clear();
            return snapshot;
        }
        snapshot.bytes.push_back(static_cast<std::uint8_t>(raw));
    }
    snapshot.decoded = ByteData::decode(snapshot.bytes);
    return snapshot;
}

} // namespace Explorer::Mcp::ManagedRead

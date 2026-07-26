// Copyright (c) 2026 Jadis0x. All rights reserved.
#include "managed_reference_store.h"

#include <algorithm>
#include <cstdio>

namespace Explorer {
namespace {

std::string pointer_text(void* pointer) {
    char text[2 + sizeof(std::uintptr_t) * 2 + 1]{};
    std::snprintf(text, sizeof(text), "0x%0*llX", static_cast<int>(sizeof(std::uintptr_t) * 2),
                  static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(pointer)));
    return text;
}

} // namespace

bool ManagedReferenceStore::capture(URK::Unity::Object object, std::string_view source, std::string& error,
                                    std::uint64_t& token) {
    token = 0;
    error.clear();
    if (!object) {
        error = "The reference is null or no longer available";
        return false;
    }
    const URK::Unity::Inspect::ObjectRefInfo described = URK::Unity::Inspect::DescribeObject(object);
    if (!described.handle || !described.type.handle) {
        const char* detail = URK::Unity::last_error();
        error = detail && detail[0] ? detail : "The runtime could not identify the reference type";
        return false;
    }
    URK::Unity::Inspect::ObjectHandle handle = URK::Unity::Inspect::PinObject(object);
    if (!handle.handle) {
        const char* detail = URK::Unity::last_error();
        error = detail && detail[0] ? detail : "The runtime refused to create a GC handle";
        return false;
    }
    std::uint64_t candidate = 0;
    do {
        candidate = 0xd000000000000000ull | (next_token_++ & 0x0fffffffffffffffull);
    } while (entries_.contains(candidate));
    ManagedReferenceInfo info{};
    info.token = candidate;
    info.type_name = described.type.full_name.empty() ? "<unknown>" : described.type.full_name;
    info.display = described.display.empty() ? info.type_name : described.display;
    info.pointer_text = pointer_text(described.handle);
    info.source = std::string(source);
    entries_.emplace(candidate, Entry{handle, std::move(info)});
    token = candidate;
    return true;
}

bool ManagedReferenceStore::resolve(std::uint64_t token, const void* destination_type,
                                    std::string_view destination_name, URK::Unity::Inspect::ValueInfo& value,
                                    std::string& error) const {
    value = {};
    error.clear();
    const auto found = entries_.find(token);
    if (found == entries_.end()) {
        error = "The pinned reference was removed";
        return false;
    }
    const URK::Unity::Object object = URK::Unity::Inspect::ResolveObjectHandle(found->second.handle);
    if (!object) {
        const char* detail = URK::Unity::last_error();
        error = detail && detail[0] ? detail : "The pinned reference is no longer available";
        return false;
    }
    const URK::Unity::Inspect::TypeInfo target = URK::Unity::Inspect::DescribeType(destination_type);
    const bool value_type = destination_type && target.is_value_type;
    const bool compatible = !destination_type ||
        (value_type ? URK::Unity::Inspect::IsBoxedValueOfType(object, destination_type)
                    : URK::Unity::Inspect::IsAssignableTo(object, destination_type));
    if (!compatible) {
        const URK::Unity::Inspect::ObjectRefInfo actual = URK::Unity::Inspect::DescribeObject(object);
        error = "Reference type mismatch: expected " + std::string(destination_name) + ", pinned reference is " +
            (actual.type.full_name.empty() ? std::string("<unknown>") : actual.type.full_name);
        return false;
    }
    value.kind = value_type ? URK::Unity::Inspect::ValueKind::ValueType : URK::Unity::Inspect::ValueKind::ObjectReference;
    value.type_name = std::string(destination_name);
    value.object = object.handle();
    value.display = found->second.info.display + " [pinned]";
    value.readable = true;
    return true;
}

URK::Unity::Object ManagedReferenceStore::object(std::uint64_t token, std::string& error) const {
    error.clear();
    const auto found = entries_.find(token);
    if (found == entries_.end()) {
        error = "The saved reference was removed";
        return {};
    }
    URK::Unity::Object result = URK::Unity::Inspect::ResolveObjectHandle(found->second.handle);
    if (result)
        return result;
    const char* detail = URK::Unity::last_error();
    error = detail && detail[0] ? detail : "The saved reference is no longer available";
    return {};
}

bool ManagedReferenceStore::erase(std::uint64_t token) {
    const auto found = entries_.find(token);
    if (found == entries_.end())
        return false;
    URK::Unity::Inspect::FreeObjectHandle(found->second.handle);
    entries_.erase(found);
    return true;
}

void ManagedReferenceStore::clear() {
    for (auto& [_, entry] : entries_)
        URK::Unity::Inspect::FreeObjectHandle(entry.handle);
    entries_.clear();
}

void ManagedReferenceStore::abandon_after_native_fault() {
    entries_.clear();
}

std::vector<ManagedReferenceInfo> ManagedReferenceStore::snapshot() const {
    std::vector<ManagedReferenceInfo> result;
    result.reserve(entries_.size());
    for (const auto& [_, entry] : entries_)
        result.push_back(entry.info);
    std::sort(result.begin(), result.end(), [](const ManagedReferenceInfo& left, const ManagedReferenceInfo& right) {
        return left.token < right.token;
    });
    return result;
}

} // namespace Explorer

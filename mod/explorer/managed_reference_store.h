// Copyright (c) 2026 Jadis0x. All rights reserved.
#pragma once

#include "explorer_types.h"
#include "sdk/unity/unity_inspect.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Explorer {

// References held by the argument picker.
class ManagedReferenceStore {
  public:
    bool capture(URK::Unity::Object object, std::string_view source, std::string& error,
                 std::uint64_t& token);
    bool resolve(std::uint64_t token, const void* destination_type, std::string_view destination_name,
                 URK::Unity::Inspect::ValueInfo& value, std::string& error) const;
    URK::Unity::Object object(std::uint64_t token, std::string& error) const;
    bool erase(std::uint64_t token);
    void clear();
    // Fault recovery cannot safely release these handles.
    void abandon_after_native_fault();
    std::vector<ManagedReferenceInfo> snapshot() const;
    std::size_t size() const { return entries_.size(); }

  private:
    struct Entry {
        URK::Unity::Inspect::ObjectHandle handle;
        ManagedReferenceInfo info;
    };

    std::unordered_map<std::uint64_t, Entry> entries_;
    std::uint64_t next_token_ = 1;
};

} // namespace Explorer

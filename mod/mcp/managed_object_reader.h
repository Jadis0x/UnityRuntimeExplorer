// Copyright (c) 2026 Jadis0x. All rights reserved.
#pragma once

#include "explorer/byte_data_decoder.h"
#include "sdk/unity/unity.h"
#include "sdk/unity/unity_inspect.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Explorer::Mcp::ManagedRead {

struct MemberValue {
    std::string name;
    std::string type_name;
    std::string declaring_type;
    bool property = false;
    bool is_static = false;
    bool runtime_safe = true;
    std::string capability_reason;
    URK::Unity::Inspect::ValueInfo value;
};

struct ObjectPage {
    URK::Unity::Inspect::TypeInfo type;
    std::string assembly;
    std::vector<MemberValue> fields;
    std::vector<MemberValue> properties;
    std::size_t matching_members = 0;
    bool truncated = false;
};

struct ArrayPage {
    URK::Unity::Inspect::TypeInfo type;
    std::string element_type;
    std::size_t length = 0;
    std::size_t offset = 0;
    std::vector<URK::Unity::Inspect::ValueInfo> values;
    bool has_more = false;
};

struct ByteSnapshot {
    std::vector<std::uint8_t> bytes;
    ByteData::DecodeResult decoded;
    std::size_t total_length = 0;
    bool truncated = false;
    std::string error;
};

ObjectPage read_object(URK::Unity::Object object, std::string_view member_query,
                       std::size_t member_limit, bool include_properties);
ArrayPage read_array(URK::Unity::Object object, std::size_t offset, std::size_t limit);
ByteSnapshot read_bytes(URK::Unity::Object object, std::size_t maximum_bytes);

} // namespace Explorer::Mcp::ManagedRead

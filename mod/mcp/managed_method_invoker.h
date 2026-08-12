// Copyright (c) 2026 Jadis0x. All rights reserved.
#pragma once

#include "sdk/unity/unity.h"
#include "sdk/unity/unity_inspect.h"

#include <nlohmann/json.hpp>

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace Explorer::Mcp::ManagedInvoke {

using ReferenceResolver =
    std::function<URK::Unity::Object(std::string_view reference, std::string& error,
                                    URK::Unity::Inspect::ObjectHandle& lifetime_root)>;

struct Result {
    URK::Unity::Inspect::ValueInfo value;
    std::string error;
};

struct PreparedValue {
    URK::Unity::Inspect::ValueInfo value;
    std::vector<URK::Unity::Inspect::ObjectHandle> roots;
    std::string error;

    PreparedValue() = default;
    PreparedValue(const PreparedValue&) = delete;
    PreparedValue& operator=(const PreparedValue&) = delete;
    PreparedValue(PreparedValue&& other) noexcept;
    PreparedValue& operator=(PreparedValue&& other) noexcept;
    ~PreparedValue();
};

PreparedValue prepare_value(std::string_view name, std::string_view type_name,
                            const void* type, bool is_value_type, bool is_enum,
                            const nlohmann::json& input,
                            const ReferenceResolver& resolve_reference);

Result invoke(URK::Unity::Object target, const URK::Unity::Inspect::MethodInfo& method,
              const nlohmann::json& arguments, const ReferenceResolver& resolve_reference);

} // namespace Explorer::Mcp::ManagedInvoke

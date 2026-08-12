// Copyright (c) 2026 Jadis0x. All rights reserved.
#pragma once

#include "sdk/unity/unity_inspect.h"

#include <cstdint>
#include <optional>
#include <string_view>

namespace Explorer::WatchAnalysis {

enum class AlarmCondition : std::uint8_t {
    Disabled = 0,
    GreaterThan,
    GreaterOrEqual,
    LessThan,
    LessOrEqual,
    Equal,
    NotEqual,
};

std::optional<double> numeric_value(const URK::Unity::Inspect::ValueInfo& value);
bool evaluate(AlarmCondition condition, double value, double threshold);
std::string_view label(AlarmCondition condition);

} // namespace Explorer::WatchAnalysis

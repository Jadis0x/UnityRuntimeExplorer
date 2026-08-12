// Copyright (c) 2026 Jadis0x. All rights reserved.
#include "watch_analysis.h"

#include <cmath>

namespace Explorer::WatchAnalysis {

std::optional<double> numeric_value(const URK::Unity::Inspect::ValueInfo& value) {
    using URK::Unity::Inspect::ValueKind;
    if (!value.readable)
        return std::nullopt;
    switch (value.kind) {
    case ValueKind::Boolean:
        return value.bool_value ? 1.0 : 0.0;
    case ValueKind::SignedInteger:
    case ValueKind::Enum:
        return static_cast<double>(value.signed_value);
    case ValueKind::UnsignedInteger:
        return static_cast<double>(value.unsigned_value);
    case ValueKind::FloatingPoint:
        return value.floating_value;
    default:
        return std::nullopt;
    }
}

bool evaluate(AlarmCondition condition, double value, double threshold) {
    const double tolerance = std::max(1e-9, std::abs(threshold) * 1e-9);
    switch (condition) {
    case AlarmCondition::Disabled: return false;
    case AlarmCondition::GreaterThan: return value > threshold;
    case AlarmCondition::GreaterOrEqual: return value >= threshold;
    case AlarmCondition::LessThan: return value < threshold;
    case AlarmCondition::LessOrEqual: return value <= threshold;
    case AlarmCondition::Equal: return std::abs(value - threshold) <= tolerance;
    case AlarmCondition::NotEqual: return std::abs(value - threshold) > tolerance;
    }
    return false;
}

std::string_view label(AlarmCondition condition) {
    switch (condition) {
    case AlarmCondition::Disabled: return "Disabled";
    case AlarmCondition::GreaterThan: return ">";
    case AlarmCondition::GreaterOrEqual: return ">=";
    case AlarmCondition::LessThan: return "<";
    case AlarmCondition::LessOrEqual: return "<=";
    case AlarmCondition::Equal: return "==";
    case AlarmCondition::NotEqual: return "!=";
    }
    return "Disabled";
}

} // namespace Explorer::WatchAnalysis

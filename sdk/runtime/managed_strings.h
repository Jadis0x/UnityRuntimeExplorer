#pragma once

#include "managed_runtime.h"

#include <cstdint>
#include <string>
#include <vector>

namespace URK::managed_strings {

inline std::string to_utf8(URK::managed::String* value,
                           const char* fallback = "") {
    if (!value)
        return fallback ? fallback : "";
    constexpr std::int32_t max_units = 1024 * 1024;
    const std::int32_t length = URK::managed::string_length(value);
    if (length < 0 || length > max_units)
        return fallback ? fallback : "";
    std::vector<char> buffer(static_cast<std::size_t>(length) * 4u + 1u, '\0');
    if (!URK::managed::string_to_utf8(value, buffer.data(), buffer.size()))
        return fallback ? fallback : "";
    return std::string(buffer.data());
}

} // namespace URK::managed_strings

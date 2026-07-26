// Copyright (c) 2026 Jadis0x. All rights reserved.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace Explorer::ByteData {

enum class Format {
    Empty,
    MessagePack,
    JsonText,
    Utf8Text,
    Gzip,
    Zlib,
    Unknown,
};

struct DecodeResult {
    Format format = Format::Unknown;
    std::string summary;
    std::string document;
    std::string diagnostic;
    std::size_t consumed_bytes = 0;
    bool complete = false;
};

// Decodes a copied byte buffer only. The parser never reads managed memory and
// deliberately bounds nesting, nodes, and rendered text.
DecodeResult decode(std::span<const std::uint8_t> bytes);

std::string_view format_name(Format format);
std::string hex_dump(std::span<const std::uint8_t> bytes, std::size_t maximum_bytes = 4096,
                     std::size_t bytes_per_line = 16);

} // namespace Explorer::ByteData

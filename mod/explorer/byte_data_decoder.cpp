// Copyright (c) 2026 Jadis0x. All rights reserved.
#include "byte_data_decoder.h"

#include <algorithm>
#include <bit>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <string_view>

namespace Explorer::ByteData {
namespace {

constexpr std::size_t kMaxDepth = 64;
constexpr std::size_t kMaxNodes = 4096;
constexpr std::size_t kMaxRenderedCharacters = 128 * 1024;
constexpr std::size_t kBinaryPreviewBytes = 32;

char hex_digit(std::uint8_t value) {
    return static_cast<char>(value < 10 ? '0' + value : 'A' + (value - 10));
}

void append_hex_byte(std::string& output, std::uint8_t value) {
    output.push_back(hex_digit(static_cast<std::uint8_t>(value >> 4)));
    output.push_back(hex_digit(static_cast<std::uint8_t>(value & 0x0F)));
}

void append_escaped_byte(std::string& output, std::uint8_t value) {
    switch (value) {
    case '\\': output += "\\\\"; break;
    case '\"': output += "\\\""; break;
    case '\n': output += "\\n"; break;
    case '\r': output += "\\r"; break;
    case '\t': output += "\\t"; break;
    default:
        if (value >= 0x20 && value <= 0x7E)
            output.push_back(static_cast<char>(value));
        else {
            output += "\\x";
            append_hex_byte(output, value);
        }
        break;
    }
}

std::string quoted_bytes(std::span<const std::uint8_t> bytes) {
    std::string output;
    output.reserve(bytes.size() + 2);
    output.push_back('\"');
    for (const std::uint8_t value : bytes)
        append_escaped_byte(output, value);
    output.push_back('\"');
    return output;
}

bool looks_like_text(std::span<const std::uint8_t> bytes) {
    if (bytes.empty())
        return false;
    std::size_t printable = 0;
    for (const std::uint8_t value : bytes) {
        if ((value >= 0x20 && value <= 0x7E) || value == '\n' || value == '\r' || value == '\t')
            ++printable;
    }
    return printable * 100 >= bytes.size() * 90;
}

bool looks_like_json(std::span<const std::uint8_t> bytes) {
    if (!looks_like_text(bytes))
        return false;
    const auto first = std::find_if(bytes.begin(), bytes.end(), [](std::uint8_t value) {
        return !std::isspace(static_cast<unsigned char>(value));
    });
    return first != bytes.end() && (*first == '{' || *first == '[');
}

class MessagePackParser {
  public:
    explicit MessagePackParser(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}

    DecodeResult parse() {
        DecodeResult result{};
        if (!parse_value(0)) {
            result.diagnostic = diagnostic_.empty() ? "Invalid or unsupported MessagePack payload." : diagnostic_;
            result.consumed_bytes = position_;
            return result;
        }
        if (position_ != bytes_.size()) {
            result.diagnostic = "MessagePack value ends before the byte array; trailing bytes remain at offset " +
                std::to_string(position_) + ".";
            result.consumed_bytes = position_;
            return result;
        }
        result.format = Format::MessagePack;
        result.summary = "Valid MessagePack document";
        result.document = std::move(output_);
        result.consumed_bytes = position_;
        result.complete = true;
        return result;
    }

  private:
    bool fail(std::string message) {
        if (diagnostic_.empty())
            diagnostic_ = std::move(message) + " (offset " + std::to_string(position_) + ").";
        return false;
    }

    bool append(std::string_view text) {
        if (output_.size() + text.size() > kMaxRenderedCharacters)
            return fail("MessagePack rendering limit reached");
        output_.append(text);
        return true;
    }

    bool append_char(char value) {
        if (output_.size() + 1 > kMaxRenderedCharacters)
            return fail("MessagePack rendering limit reached");
        output_.push_back(value);
        return true;
    }

    bool append_indent(std::size_t depth) {
        return append(std::string(depth * 2, ' '));
    }

    bool read_u8(std::uint8_t& value) {
        if (position_ >= bytes_.size())
            return fail("Unexpected end of MessagePack data");
        value = bytes_[position_++];
        return true;
    }

    bool read_u16(std::uint16_t& value) {
        if (bytes_.size() - position_ < 2)
            return fail("Unexpected end of MessagePack data");
        value = static_cast<std::uint16_t>((static_cast<std::uint16_t>(bytes_[position_]) << 8) |
                                           bytes_[position_ + 1]);
        position_ += 2;
        return true;
    }

    bool read_u32(std::uint32_t& value) {
        if (bytes_.size() - position_ < 4)
            return fail("Unexpected end of MessagePack data");
        value = (static_cast<std::uint32_t>(bytes_[position_]) << 24) |
                (static_cast<std::uint32_t>(bytes_[position_ + 1]) << 16) |
                (static_cast<std::uint32_t>(bytes_[position_ + 2]) << 8) |
                static_cast<std::uint32_t>(bytes_[position_ + 3]);
        position_ += 4;
        return true;
    }

    bool read_u64(std::uint64_t& value) {
        if (bytes_.size() - position_ < 8)
            return fail("Unexpected end of MessagePack data");
        value = 0;
        for (int index = 0; index < 8; ++index)
            value = (value << 8) | bytes_[position_++];
        return true;
    }

    bool read_span(std::size_t count, std::span<const std::uint8_t>& value) {
        if (count > bytes_.size() - position_)
            return fail("Unexpected end of MessagePack data");
        value = bytes_.subspan(position_, count);
        position_ += count;
        return true;
    }

    bool parse_string(std::size_t count) {
        std::span<const std::uint8_t> value;
        return read_span(count, value) && append(quoted_bytes(value));
    }

    bool parse_binary(std::size_t count) {
        std::span<const std::uint8_t> value;
        if (!read_span(count, value))
            return false;
        if (!append("binary[" + std::to_string(count) + "] "))
            return false;
        const std::size_t preview = std::min(count, kBinaryPreviewBytes);
        for (std::size_t index = 0; index < preview; ++index) {
            if (index && !append_char(' '))
                return false;
            std::string hex;
            hex.reserve(2);
            append_hex_byte(hex, value[index]);
            if (!append(hex))
                return false;
        }
        return preview == count || append(" …");
    }

    bool parse_array(std::size_t count, std::size_t depth) {
        if (!append("array(" + std::to_string(count) + ")["))
            return false;
        if (count == 0)
            return append_char(']');
        if (!append_char('\n'))
            return false;
        for (std::size_t index = 0; index < count; ++index) {
            if (!append_indent(depth + 1) || !parse_value(depth + 1))
                return false;
            if (!append(index + 1 == count ? "\n" : ",\n"))
                return false;
        }
        return append_indent(depth) && append_char(']');
    }

    bool parse_map(std::size_t count, std::size_t depth) {
        if (!append("map(" + std::to_string(count) + "){"))
            return false;
        if (count == 0)
            return append_char('}');
        if (!append_char('\n'))
            return false;
        for (std::size_t index = 0; index < count; ++index) {
            if (!append_indent(depth + 1) || !parse_value(depth + 1) || !append(": ") || !parse_value(depth + 1))
                return false;
            if (!append(index + 1 == count ? "\n" : ",\n"))
                return false;
        }
        return append_indent(depth) && append_char('}');
    }

    bool parse_extension(std::size_t count) {
        std::uint8_t type = 0;
        std::span<const std::uint8_t> value;
        if (!read_u8(type) || !read_span(count, value))
            return false;
        return append("extension(type=" + std::to_string(static_cast<std::int8_t>(type)) + ", bytes=" +
                      std::to_string(count) + ")");
    }

    bool parse_value(std::size_t depth) {
        if (depth > kMaxDepth)
            return fail("MessagePack nesting limit reached");
        if (++nodes_ > kMaxNodes)
            return fail("MessagePack node limit reached");
        std::uint8_t marker = 0;
        if (!read_u8(marker))
            return false;

        if (marker <= 0x7F)
            return append(std::to_string(marker));
        if (marker >= 0xE0)
            return append(std::to_string(static_cast<std::int8_t>(marker)));
        if ((marker & 0xF0) == 0x80)
            return parse_map(marker & 0x0F, depth);
        if ((marker & 0xF0) == 0x90)
            return parse_array(marker & 0x0F, depth);
        if ((marker & 0xE0) == 0xA0)
            return parse_string(marker & 0x1F);

        std::uint8_t u8 = 0;
        std::uint16_t u16 = 0;
        std::uint32_t u32 = 0;
        std::uint64_t u64 = 0;
        switch (marker) {
        case 0xC0: return append("null");
        case 0xC2: return append("false");
        case 0xC3: return append("true");
        case 0xC4: return read_u8(u8) && parse_binary(u8);
        case 0xC5: return read_u16(u16) && parse_binary(u16);
        case 0xC6: return read_u32(u32) && parse_binary(u32);
        case 0xC7: return read_u8(u8) && parse_extension(u8);
        case 0xC8: return read_u16(u16) && parse_extension(u16);
        case 0xC9: return read_u32(u32) && parse_extension(u32);
        case 0xCA:
            if (!read_u32(u32)) return false;
            return append(std::to_string(std::bit_cast<float>(u32)));
        case 0xCB:
            if (!read_u64(u64)) return false;
            return append(std::to_string(std::bit_cast<double>(u64)));
        case 0xCC: return read_u8(u8) && append(std::to_string(u8));
        case 0xCD: return read_u16(u16) && append(std::to_string(u16));
        case 0xCE: return read_u32(u32) && append(std::to_string(u32));
        case 0xCF: return read_u64(u64) && append(std::to_string(u64));
        case 0xD0:
            if (!read_u8(u8)) return false;
            return append(std::to_string(static_cast<std::int8_t>(u8)));
        case 0xD1:
            if (!read_u16(u16)) return false;
            return append(std::to_string(static_cast<std::int16_t>(u16)));
        case 0xD2:
            if (!read_u32(u32)) return false;
            return append(std::to_string(static_cast<std::int32_t>(u32)));
        case 0xD3:
            if (!read_u64(u64)) return false;
            return append(std::to_string(static_cast<std::int64_t>(u64)));
        case 0xD4: return parse_extension(1);
        case 0xD5: return parse_extension(2);
        case 0xD6: return parse_extension(4);
        case 0xD7: return parse_extension(8);
        case 0xD8: return parse_extension(16);
        case 0xD9: return read_u8(u8) && parse_string(u8);
        case 0xDA: return read_u16(u16) && parse_string(u16);
        case 0xDB: return read_u32(u32) && parse_string(u32);
        case 0xDC: return read_u16(u16) && parse_array(u16, depth);
        case 0xDD: return read_u32(u32) && parse_array(u32, depth);
        case 0xDE: return read_u16(u16) && parse_map(u16, depth);
        case 0xDF: return read_u32(u32) && parse_map(u32, depth);
        case 0xC1: return fail("Reserved MessagePack marker 0xC1");
        default: return fail("Unsupported MessagePack marker");
        }
    }

    std::span<const std::uint8_t> bytes_;
    std::size_t position_ = 0;
    std::size_t nodes_ = 0;
    std::string output_;
    std::string diagnostic_;
};

} // namespace

std::string_view format_name(Format format) {
    switch (format) {
    case Format::Empty: return "Empty";
    case Format::MessagePack: return "MessagePack";
    case Format::JsonText: return "JSON text";
    case Format::Utf8Text: return "Text";
    case Format::Gzip: return "GZip";
    case Format::Zlib: return "Zlib";
    case Format::Unknown: return "Unknown binary";
    }
    return "Unknown binary";
}

DecodeResult decode(std::span<const std::uint8_t> bytes) {
    if (bytes.empty())
        return { Format::Empty, "Empty byte array", {}, {}, 0, true };
    if (bytes.size() >= 2 && bytes[0] == 0x1F && bytes[1] == 0x8B)
        return { Format::Gzip, "GZip-compressed data", {}, "Compressed payload; decompression is not available in the runtime explorer.", 2, false };
    if (bytes.size() >= 2 && bytes[0] == 0x78 && (bytes[1] == 0x01 || bytes[1] == 0x5E || bytes[1] == 0x9C || bytes[1] == 0xDA))
        return { Format::Zlib, "Zlib-compressed data", {}, "Compressed payload; decompression is not available in the runtime explorer.", 2, false };

    MessagePackParser message_pack(bytes);
    DecodeResult result = message_pack.parse();
    if (result.complete)
        return result;
    if (looks_like_json(bytes))
        return { Format::JsonText, "JSON text candidate", quoted_bytes(bytes), "JSON syntax is shown as text; it is not rewritten or executed.", bytes.size(), true };
    if (looks_like_text(bytes))
        return { Format::Utf8Text, "Printable text", quoted_bytes(bytes), {}, bytes.size(), true };
    result.summary = "Unknown binary data";
    return result;
}

std::string hex_dump(std::span<const std::uint8_t> bytes, std::size_t maximum_bytes, std::size_t bytes_per_line) {
    if (bytes_per_line == 0)
        bytes_per_line = 16;
    const std::size_t count = std::min(bytes.size(), maximum_bytes);
    std::string output;
    output.reserve(count * 4 + (count / bytes_per_line + 1) * 16);
    for (std::size_t offset = 0; offset < count; offset += bytes_per_line) {
        const std::size_t line_count = std::min(bytes_per_line, count - offset);
        std::ostringstream address;
        address << std::hex << std::uppercase << std::setw(8) << std::setfill('0') << offset;
        output += address.str();
        output += "  ";
        for (std::size_t index = 0; index < bytes_per_line; ++index) {
            if (index < line_count)
                append_hex_byte(output, bytes[offset + index]);
            else
                output += "  ";
            output.push_back(' ');
        }
        output.push_back(' ');
        for (std::size_t index = 0; index < line_count; ++index) {
            const std::uint8_t value = bytes[offset + index];
            output.push_back(value >= 0x20 && value <= 0x7E ? static_cast<char>(value) : '.');
        }
        output.push_back('\n');
    }
    if (count < bytes.size())
        output += "… hex preview truncated after " + std::to_string(count) + " bytes.\n";
    return output;
}

} // namespace Explorer::ByteData

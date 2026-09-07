// Copyright (c) 2026 Jadis0x. All rights reserved.
#include "png_writer.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <vector>

namespace Explorer::PngWriter {
namespace {

std::uint32_t crc32_table(std::size_t index) {
    static const std::array<std::uint32_t, 256> table = [] {
        std::array<std::uint32_t, 256> t{};
        for (std::uint32_t n = 0; n < 256; ++n) {
            std::uint32_t c = n;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            t[n] = c;
        }
        return t;
    }();
    return table[index];
}

std::uint32_t crc32(const std::uint8_t* data, std::size_t size) {
    std::uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < size; ++i)
        crc = crc32_table((crc ^ data[i]) & 0xFFu) ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

std::uint32_t adler32(const std::uint8_t* data, std::size_t size) {
    std::uint32_t a = 1, b = 0;
    constexpr std::uint32_t kModAdler = 65521u;
    std::size_t offset = 0;
    // Reduce inside the loop periodically so a and b never overflow uint32
    // during accumulation, without paying a modulo per byte.
    while (offset < size) {
        const std::size_t chunk = std::min<std::size_t>(size - offset, 5552);
        for (std::size_t i = 0; i < chunk; ++i) {
            a += data[offset + i];
            b += a;
        }
        a %= kModAdler;
        b %= kModAdler;
        offset += chunk;
    }
    return (b << 16) | a;
}

void append_u32_be(std::vector<std::uint8_t>& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value >> 24));
    out.push_back(static_cast<std::uint8_t>(value >> 16));
    out.push_back(static_cast<std::uint8_t>(value >> 8));
    out.push_back(static_cast<std::uint8_t>(value));
}

void append_chunk(std::vector<std::uint8_t>& out, const char type[4], const std::vector<std::uint8_t>& data) {
    append_u32_be(out, static_cast<std::uint32_t>(data.size()));
    const std::size_t type_offset = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), data.begin(), data.end());
    const std::uint32_t crc = crc32(out.data() + type_offset, 4 + data.size());
    append_u32_be(out, crc);
}

// A zlib stream wrapping the payload in DEFLATE "stored" (uncompressed)
// blocks - larger than real compression, but needs no compressor at all.
std::vector<std::uint8_t> zlib_store(const std::vector<std::uint8_t>& payload) {
    std::vector<std::uint8_t> out;
    out.reserve(payload.size() + payload.size() / 65535 * 5 + 8);
    out.push_back(0x78);
    out.push_back(0x01);
    constexpr std::size_t kMaxBlock = 65535;
    std::size_t offset = 0;
    if (payload.empty()) {
        out.push_back(0x01);
        out.push_back(0x00);
        out.push_back(0x00);
        out.push_back(0xFF);
        out.push_back(0xFF);
    }
    while (offset < payload.size()) {
        const std::size_t remaining = payload.size() - offset;
        const std::size_t block_size = std::min(remaining, kMaxBlock);
        const bool final_block = (offset + block_size) >= payload.size();
        out.push_back(final_block ? 0x01 : 0x00);
        const std::uint16_t len = static_cast<std::uint16_t>(block_size);
        const std::uint16_t nlen = static_cast<std::uint16_t>(~len);
        out.push_back(static_cast<std::uint8_t>(len & 0xFF));
        out.push_back(static_cast<std::uint8_t>(len >> 8));
        out.push_back(static_cast<std::uint8_t>(nlen & 0xFF));
        out.push_back(static_cast<std::uint8_t>(nlen >> 8));
        out.insert(out.end(), payload.begin() + static_cast<std::ptrdiff_t>(offset),
                  payload.begin() + static_cast<std::ptrdiff_t>(offset + block_size));
        offset += block_size;
    }
    append_u32_be(out, adler32(payload.data(), payload.size()));
    return out;
}

} // namespace

bool write_rgba(const std::string& path, const std::uint8_t* pixels, int width, int height, std::string& error) {
    if (!pixels || width <= 0 || height <= 0) {
        error = "invalid pixel buffer or dimensions";
        return false;
    }
    std::vector<std::uint8_t> out;
    static constexpr std::uint8_t kSignature[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    out.insert(out.end(), kSignature, kSignature + 8);

    std::vector<std::uint8_t> ihdr;
    append_u32_be(ihdr, static_cast<std::uint32_t>(width));
    append_u32_be(ihdr, static_cast<std::uint32_t>(height));
    ihdr.push_back(8); // bit depth
    ihdr.push_back(6); // color type: RGBA
    ihdr.push_back(0); // compression method
    ihdr.push_back(0); // filter method
    ihdr.push_back(0); // interlace method
    append_chunk(out, "IHDR", ihdr);

    const std::size_t stride = static_cast<std::size_t>(width) * 4;
    std::vector<std::uint8_t> raw;
    raw.reserve((stride + 1) * static_cast<std::size_t>(height));
    for (int y = 0; y < height; ++y) {
        raw.push_back(0); // filter type: None
        const std::uint8_t* row = pixels + static_cast<std::size_t>(y) * stride;
        raw.insert(raw.end(), row, row + stride);
    }
    append_chunk(out, "IDAT", zlib_store(raw));
    append_chunk(out, "IEND", {});

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        error = "could not open file for writing";
        return false;
    }
    file.write(reinterpret_cast<const char*>(out.data()), static_cast<std::streamsize>(out.size()));
    if (!file) {
        error = "write did not complete";
        return false;
    }
    return true;
}

} // namespace Explorer::PngWriter

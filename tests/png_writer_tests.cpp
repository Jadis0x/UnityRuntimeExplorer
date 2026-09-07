// Copyright (c) 2026 Jadis0x. All rights reserved.
// Verifies the hand-rolled PNG encoder byte-for-byte: chunk framing, CRC32,
// and that the "stored" (uncompressed) DEFLATE blocks really do reconstruct
// the original scanline bytes exactly.
#include "mod/explorer/png_writer.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

bool require(bool condition, const char* message) {
    if (condition)
        return true;
    std::fprintf(stderr, "FAILED: %s\n", message);
    return false;
}

std::uint32_t crc32_of(const std::uint8_t* data, std::size_t size) {
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
    std::uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < size; ++i)
        crc = table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

std::uint32_t read_u32_be(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    return (static_cast<std::uint32_t>(bytes[offset]) << 24) | (static_cast<std::uint32_t>(bytes[offset + 1]) << 16) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 8) | static_cast<std::uint32_t>(bytes[offset + 3]);
}

// Inverts the encoder's "stored block" zlib stream back into raw bytes,
// independently of the encoder, so a bug in one can't hide behind the other.
std::vector<std::uint8_t> inflate_stored(const std::vector<std::uint8_t>& zlib_stream, bool& ok) {
    ok = false;
    std::vector<std::uint8_t> out;
    if (zlib_stream.size() < 6)
        return out;
    std::size_t pos = 2; // skip the 2-byte zlib header
    for (;;) {
        if (pos + 5 > zlib_stream.size() - 4) // 4 trailing adler32 bytes
            return out;
        const std::uint8_t block_header = zlib_stream[pos++];
        const bool final_block = (block_header & 1) != 0;
        const std::uint16_t len = static_cast<std::uint16_t>(zlib_stream[pos] | (zlib_stream[pos + 1] << 8));
        const std::uint16_t nlen = static_cast<std::uint16_t>(zlib_stream[pos + 2] | (zlib_stream[pos + 3] << 8));
        pos += 4;
        if (static_cast<std::uint16_t>(~len) != nlen)
            return out;
        if (pos + len > zlib_stream.size() - 4)
            return out;
        out.insert(out.end(), zlib_stream.begin() + static_cast<std::ptrdiff_t>(pos),
                  zlib_stream.begin() + static_cast<std::ptrdiff_t>(pos + len));
        pos += len;
        if (final_block) {
            ok = pos == zlib_stream.size() - 4;
            return out;
        }
    }
}

} // namespace

int main() {
    bool ok = true;

    // A tiny, non-uniform 3x2 RGBA image - enough to catch a row/stride bug
    // without needing a >64KB buffer to exercise the multi-block path too.
    constexpr int width = 3;
    constexpr int height = 2;
    const std::vector<std::uint8_t> pixels = {
        255, 0, 0, 255,    0, 255, 0, 255,    0, 0, 255, 255,
        10, 20, 30, 40,    50, 60, 70, 80,    90, 100, 110, 120,
    };

    const std::filesystem::path path = std::filesystem::temp_directory_path() / "urk_png_writer_test.png";
    std::string error;
    ok &= require(Explorer::PngWriter::write_rgba(path.string(), pixels.data(), width, height, error),
                  "write_rgba should succeed for a small valid buffer");

    std::ifstream file(path, std::ios::binary);
    ok &= require(static_cast<bool>(file), "the written file should be readable back");
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();
    std::filesystem::remove(path);

    static constexpr std::uint8_t kSignature[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    ok &= require(bytes.size() > 8 && std::equal(bytes.begin(), bytes.begin() + 8, kSignature),
                  "file must start with the PNG signature");

    // IHDR chunk: length(4) + "IHDR"(4) + 13 bytes of data + crc(4).
    std::size_t offset = 8;
    ok &= require(offset + 8 <= bytes.size(), "IHDR chunk header must be present");
    const std::uint32_t ihdr_length = read_u32_be(bytes, offset);
    ok &= require(ihdr_length == 13, "IHDR data length must be exactly 13 bytes");
    ok &= require(std::string(bytes.begin() + static_cast<std::ptrdiff_t>(offset + 4),
                              bytes.begin() + static_cast<std::ptrdiff_t>(offset + 8)) == "IHDR",
                  "second chunk must be IHDR");
    const std::size_t ihdr_data_offset = offset + 8;
    const std::uint32_t ihdr_width = read_u32_be(bytes, ihdr_data_offset);
    const std::uint32_t ihdr_height = read_u32_be(bytes, ihdr_data_offset + 4);
    ok &= require(ihdr_width == static_cast<std::uint32_t>(width), "IHDR width must match the source image");
    ok &= require(ihdr_height == static_cast<std::uint32_t>(height), "IHDR height must match the source image");
    ok &= require(bytes[ihdr_data_offset + 8] == 8, "bit depth must be 8");
    ok &= require(bytes[ihdr_data_offset + 9] == 6, "color type must be 6 (RGBA)");
    const std::uint32_t ihdr_crc = read_u32_be(bytes, ihdr_data_offset + 13);
    ok &= require(ihdr_crc == crc32_of(bytes.data() + offset + 4, 4 + 13), "IHDR CRC32 must match its type+data");

    offset = ihdr_data_offset + 13 + 4;
    ok &= require(offset + 8 <= bytes.size(), "IDAT chunk header must be present");
    const std::uint32_t idat_length = read_u32_be(bytes, offset);
    ok &= require(std::string(bytes.begin() + static_cast<std::ptrdiff_t>(offset + 4),
                              bytes.begin() + static_cast<std::ptrdiff_t>(offset + 8)) == "IDAT",
                  "third chunk must be IDAT");
    const std::size_t idat_data_offset = offset + 8;
    ok &= require(idat_data_offset + idat_length + 4 <= bytes.size(), "IDAT chunk must fit in the file");
    const std::vector<std::uint8_t> zlib_stream(bytes.begin() + static_cast<std::ptrdiff_t>(idat_data_offset),
                                                bytes.begin() + static_cast<std::ptrdiff_t>(idat_data_offset + idat_length));
    const std::uint32_t idat_crc = read_u32_be(bytes, idat_data_offset + idat_length);
    ok &= require(idat_crc == crc32_of(bytes.data() + offset + 4, 4 + idat_length),
                  "IDAT CRC32 must match its type+data");

    bool inflate_ok = false;
    const std::vector<std::uint8_t> scanlines = inflate_stored(zlib_stream, inflate_ok);
    ok &= require(inflate_ok, "the stored-block zlib stream must be well-formed");
    const std::size_t stride = static_cast<std::size_t>(width) * 4;
    ok &= require(scanlines.size() == (stride + 1) * static_cast<std::size_t>(height),
                  "reconstructed scanlines must total (stride + filter byte) * height");
    bool pixels_match = true;
    for (int y = 0; y < height && pixels_match; ++y) {
        const std::size_t row_offset = static_cast<std::size_t>(y) * (stride + 1);
        pixels_match = pixels_match && scanlines[row_offset] == 0; // filter type: None
        for (std::size_t x = 0; x < stride; ++x)
            pixels_match = pixels_match && scanlines[row_offset + 1 + x] == pixels[static_cast<std::size_t>(y) * stride + x];
    }
    ok &= require(pixels_match, "decompressed scanlines must reproduce the original pixels exactly");

    if (!ok)
        return 1;
    std::printf("png writer contract passed\n");
    return 0;
}

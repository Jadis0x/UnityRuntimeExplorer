// Copyright (c) 2026 Jadis0x. All rights reserved.
#pragma once

// Minimal, dependency-free PNG encoder for RGBA8 pixel buffers. Writes a
// standards-compliant PNG using "stored" (uncompressed) DEFLATE blocks, so it
// never needs zlib - only a CRC32 and an Adler32 checksum, both trivial. Files
// are larger than a compressed PNG would be, but they open in any viewer.

#include <cstdint>
#include <string>

namespace Explorer::PngWriter {

// `pixels` must be `width * height * 4` bytes, row-major, top-to-bottom, RGBA8.
bool write_rgba(const std::string& path, const std::uint8_t* pixels, int width, int height, std::string& error);

} // namespace Explorer::PngWriter

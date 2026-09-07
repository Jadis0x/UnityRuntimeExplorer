// Copyright (c) 2026 Jadis0x. All rights reserved.
#include "wav_writer.h"

#include <cstring>
#include <fstream>
#include <vector>

namespace Explorer::WavWriter {
namespace {

void append_bytes(std::vector<std::uint8_t>& out, const char* text, std::size_t count) {
    out.insert(out.end(), reinterpret_cast<const std::uint8_t*>(text), reinterpret_cast<const std::uint8_t*>(text) + count);
}

void append_u32_le(std::vector<std::uint8_t>& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value));
    out.push_back(static_cast<std::uint8_t>(value >> 8));
    out.push_back(static_cast<std::uint8_t>(value >> 16));
    out.push_back(static_cast<std::uint8_t>(value >> 24));
}

void append_u16_le(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value));
    out.push_back(static_cast<std::uint8_t>(value >> 8));
}

} // namespace

bool write_float32(const std::string& path, const float* samples, std::size_t sample_count, int channels,
                   int sample_rate, std::string& error) {
    if (!samples || channels <= 0 || sample_rate <= 0 || sample_count % static_cast<std::size_t>(channels) != 0) {
        error = "invalid sample buffer, channel count, or sample rate";
        return false;
    }
    constexpr std::uint32_t kBitsPerSample = 32;
    const std::uint32_t byte_rate = static_cast<std::uint32_t>(sample_rate) * static_cast<std::uint32_t>(channels) * 4u;
    const std::uint16_t block_align = static_cast<std::uint16_t>(channels * 4);
    const std::uint32_t data_size = static_cast<std::uint32_t>(sample_count * sizeof(float));
    const std::uint32_t frames_per_channel = static_cast<std::uint32_t>(sample_count / static_cast<std::size_t>(channels));

    std::vector<std::uint8_t> out;
    out.reserve(44 + data_size);
    append_bytes(out, "RIFF", 4);
    // 4 (WAVE) + fmt chunk (8 + 16) + fact chunk (8 + 4) + data chunk header (8) + data
    append_u32_le(out, 4 + (8 + 16) + (8 + 4) + 8 + data_size);
    append_bytes(out, "WAVE", 4);

    append_bytes(out, "fmt ", 4);
    append_u32_le(out, 16);
    append_u16_le(out, 3); // WAVE_FORMAT_IEEE_FLOAT
    append_u16_le(out, static_cast<std::uint16_t>(channels));
    append_u32_le(out, static_cast<std::uint32_t>(sample_rate));
    append_u32_le(out, byte_rate);
    append_u16_le(out, block_align);
    append_u16_le(out, static_cast<std::uint16_t>(kBitsPerSample));

    append_bytes(out, "fact", 4);
    append_u32_le(out, 4);
    append_u32_le(out, frames_per_channel);

    append_bytes(out, "data", 4);
    append_u32_le(out, data_size);
    const auto* sample_bytes = reinterpret_cast<const std::uint8_t*>(samples);
    out.insert(out.end(), sample_bytes, sample_bytes + data_size);

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

} // namespace Explorer::WavWriter

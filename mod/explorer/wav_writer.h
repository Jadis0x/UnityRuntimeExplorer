// Copyright (c) 2026 Jadis0x. All rights reserved.
#pragma once

// Minimal WAV (RIFF/WAVE) writer for interleaved 32-bit float PCM, matching
// what AudioClip.GetData() already hands back - no resampling or conversion
// needed, so the exported file is bit-for-bit what Unity decoded.

#include <cstdint>
#include <string>

namespace Explorer::WavWriter {

// `samples` is interleaved per-channel float data, `samples.size()` must be a
// multiple of `channels`.
bool write_float32(const std::string& path, const float* samples, std::size_t sample_count, int channels,
                   int sample_rate, std::string& error);

} // namespace Explorer::WavWriter

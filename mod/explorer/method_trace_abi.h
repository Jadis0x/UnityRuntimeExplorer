// Copyright (c) 2026 Jadis0x. All rights reserved.
#pragma once

#include "sdk/unity/unity_inspect.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace Explorer::MethodTraceAbi {

// Layout captured by the entry stub after it has saved seven general-purpose
// registers and reserved 96 bytes for XMM0..XMM5.  Win64 places the first
// stack argument after the return address and 32-byte home/shadow space.
struct RegisterFrame {
    std::array<std::array<std::uint8_t, 16>, 6> xmm;
    std::uint64_t r11;
    std::uint64_t r10;
    std::uint64_t r9;
    std::uint64_t r8;
    std::uint64_t rdx;
    std::uint64_t rcx;
    std::uint64_t rax;
    std::uint64_t return_address;
    std::array<std::uint64_t, 4> shadow_space;
    std::uint64_t stack_arguments[URK::Unity::Inspect::kMaxMethodParameters];
};

static_assert(offsetof(RegisterFrame, r11) == 96);
static_assert(offsetof(RegisterFrame, return_address) == 152);
static_assert(offsetof(RegisterFrame, shadow_space) == 160);
static_assert(offsetof(RegisterFrame, stack_arguments) == 192);

inline std::uint64_t integer_argument(const RegisterFrame& frame, std::size_t slot) {
    if (slot >= 4)
        return frame.stack_arguments[slot - 4];
    return slot == 0 ? frame.rcx : slot == 1 ? frame.rdx : slot == 2 ? frame.r8 : frame.r9;
}

inline std::uint64_t xmm_argument(const RegisterFrame& frame, std::size_t slot, std::size_t offset) {
    if (slot >= 4 || offset > 8)
        return 0;
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < sizeof(value); ++index)
        value |= static_cast<std::uint64_t>(frame.xmm[slot][offset + index]) << (index * 8u);
    return value;
}

} // namespace Explorer::MethodTraceAbi

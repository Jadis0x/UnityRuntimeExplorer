// Copyright (c) 2026 Jadis0x. All rights reserved.
// Minimal x86-64 instruction decoder.
//
// Enough of the encoding to walk a compiled function instruction by
// instruction, compare two bodies for structural equality, and know that an
// address is a real instruction boundary. Anything it does not recognise is
// reported as undecodable rather than guessed at: every caller uses the result
// to decide where to write a hook, and a wrong boundary corrupts the process.
#pragma once

#include <cstddef>
#include <cstdint>

namespace Explorer::X86 {

inline constexpr std::uint8_t no_register = 0xFF;
// Register numbers follow the hardware encoding: 0=rax 1=rcx 2=rdx 3=rbx
// 4=rsp 5=rbp 6=rsi 7=rdi 8..15=r8..r15.
inline constexpr std::uint8_t register_rcx = 1;

// Opcode map an instruction was encoded in. VEX and EVEX select the same maps
// the legacy escape bytes do.
enum class Map : std::uint8_t { one_byte = 0, two_byte = 1, three_byte_38 = 2, three_byte_3a = 3 };

struct Instruction {
    std::size_t length = 0;
    std::uintptr_t address = 0;
    Map map = Map::one_byte;
    std::uint8_t opcode = 0;
    std::uint8_t rex = 0;
    // Set for a VEX or EVEX encoding, where the register fields live in the
    // prefix instead of in REX.
    bool vector_encoding = false;
    // 0 none, 1 = 0x66, 2 = 0xF3, 3 = 0xF2. Carries the mandatory prefix of a
    // SIMD opcode however it was encoded.
    std::uint8_t simd_prefix = 0;
    // 0 = 128 bit or scalar, 1 = 256, 2 = 512.
    std::uint8_t vector_length = 0;
    // REX.W or VEX.W: the wide form of the same opcode.
    bool wide = false;
    bool operand_size_prefix = false;
    std::uint8_t repeat_prefix = 0;
    bool has_modrm = false;
    std::uint8_t modrm = 0;
    // mod != 3: the instruction reads or writes memory.
    bool memory_operand = false;
    std::uint8_t base_register = no_register;
    std::uint8_t index_register = no_register;
    std::uint8_t reg_register = no_register;
    // Register form of r/m, set only when mod == 3.
    std::uint8_t rm_register = no_register;
    bool rip_relative = false;
    std::uintptr_t rip_target = 0;
    std::int64_t displacement = 0;
    std::uint8_t displacement_size = 0;
    std::size_t displacement_offset = 0;
    std::int64_t immediate = 0;
    std::uint8_t immediate_size = 0;
    bool is_relative_branch = false;
    bool is_call = false;
    bool is_unconditional_jump = false;
    bool is_return = false;
    std::uintptr_t branch_target = 0;
};

// address is the runtime address the bytes are loaded at; it only affects
// rip_target and branch_target.
bool decode(const std::uint8_t *code, std::size_t available, std::uintptr_t address, Instruction &out);

// True for `mov reg, reg`, the shuffling a compiler inserts or drops freely
// when it inlines a body, so structural comparison skips it.
bool is_register_move(const Instruction &instruction);

// Structural equality: same operation, same memory form, same constants, same
// absolute rip target. Register numbers are ignored because inlining
// reallocates them, and branch displacements are ignored because the inlined
// copy branches elsewhere.
bool same_shape(const Instruction &left, const Instruction &right);

} // namespace Explorer::X86

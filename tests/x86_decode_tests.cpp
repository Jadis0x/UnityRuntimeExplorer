// Copyright (c) 2026 Jadis0x. All rights reserved.
// Instruction lengths and the structural comparison the inline-site resolver
// relies on. A wrong length here puts a hook in the middle of an instruction,
// so every encoding form the decoder claims to handle is pinned down.
#include "mod/explorer/x86_decode.h"

#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <iostream>
#include <vector>

namespace {

using Explorer::X86::Instruction;

void require(bool condition, const char *message) {
    if (condition)
        return;
    std::cerr << "FAILED: " << message << '\n';
    std::exit(1);
}

Instruction decode(std::initializer_list<std::uint8_t> bytes, std::uintptr_t address = 0x140001000ull) {
    const std::vector<std::uint8_t> code(bytes);
    Instruction out{};
    require(Explorer::X86::decode(code.data(), code.size(), address, out), "instruction must decode");
    return out;
}

bool decodes(std::initializer_list<std::uint8_t> bytes) {
    const std::vector<std::uint8_t> code(bytes);
    Instruction out{};
    return Explorer::X86::decode(code.data(), code.size(), 0x140001000ull, out);
}

void expect_length(std::initializer_list<std::uint8_t> bytes, std::size_t length, const char *message) {
    require(decode(bytes).length == length, message);
}

} // namespace

int main() {
    // Legacy integer forms.
    expect_length({0x48, 0x8B, 0xD9}, 3, "mov rbx, rcx");
    expect_length({0x83, 0x41, 0x20, 0xFB}, 4, "add dword [rcx+0x20], -5");
    expect_length({0x80, 0x79, 0x2C, 0x00}, 4, "cmp byte [rcx+0x2c], 0");
    expect_length({0x48, 0x89, 0x5C, 0x24, 0x18}, 5, "mov [rsp+0x18], rbx");
    expect_length({0x48, 0xB8, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88}, 10, "mov rax, imm64");
    expect_length({0xB8, 0x2A, 0x00, 0x00, 0x00}, 5, "mov eax, imm32");
    expect_length({0x66, 0xB8, 0x2A, 0x00}, 4, "mov ax, imm16 under the operand size prefix");
    expect_length({0xC7, 0x43, 0x20, 0x00, 0x00, 0x00, 0x00}, 7, "mov dword [rbx+0x20], imm32");
    expect_length({0xF7, 0xD8}, 2, "neg eax (group 3 without an immediate)");
    expect_length({0xF7, 0xC1, 0x01, 0x00, 0x00, 0x00}, 6, "test ecx, imm32 (group 3 with one)");
    expect_length({0xC8, 0x10, 0x00, 0x00}, 4, "enter imm16, imm8");
    expect_length({0x48, 0xA1, 0, 0, 0, 0, 0, 0, 0, 0}, 10, "mov rax, moffs64 stays 64-bit wide");
    expect_length({0x41, 0xFF, 0x50, 0x18}, 4, "call qword [r8+0x18]");
    expect_length({0x0F, 0x1F, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00}, 8, "multi-byte nop");

    // SIB and displacement forms.
    expect_length({0x48, 0x8B, 0x04, 0xC8}, 4, "mov rax, [rax+rcx*8]");
    expect_length({0x48, 0x8B, 0x84, 0xC8, 0x20, 0x00, 0x00, 0x00}, 8, "mov rax, [rax+rcx*8+disp32]");
    expect_length({0x48, 0x8B, 0x04, 0x25, 0x00, 0x10, 0x00, 0x00}, 8, "absolute SIB form with no base");

    // Branches and their targets.
    const Instruction call = decode({0xE8, 0x00, 0x01, 0x00, 0x00});
    require(call.length == 5 && call.is_call && call.is_relative_branch, "call rel32");
    require(call.branch_target == 0x140001105ull, "a relative call resolves against the next instruction");
    const Instruction jump = decode({0x0F, 0x85, 0x10, 0x00, 0x00, 0x00});
    require(jump.length == 6 && jump.is_relative_branch && !jump.is_call, "jcc rel32");
    require(decode({0xEB, 0x10}).is_unconditional_jump, "jmp rel8");
    require(decode({0xC3}).is_return, "ret");
    require(decode({0xFF, 0x25, 0x00, 0x00, 0x00, 0x00}).is_unconditional_jump, "jmp qword [rip+disp32]");

    // Rip-relative addressing: the resolver's fingerprints are built from these.
    const Instruction rip = decode({0x80, 0x3D, 0x10, 0x00, 0x00, 0x00, 0x00});
    require(rip.length == 7 && rip.rip_relative, "cmp byte [rip+disp32], 0");
    require(rip.rip_target == 0x140001017ull, "a rip target counts from the end of the instruction");
    require(rip.displacement_offset == 2, "the displacement field is where the scan says it is");
    const Instruction lea = decode({0x48, 0x8D, 0x0D, 0x00, 0x00, 0x00, 0x00});
    require(lea.length == 7 && lea.rip_target == 0x140001007ull, "lea rcx, [rip+disp32]");

    // SSE, then the same operations under VEX and EVEX.
    expect_length({0x0F, 0x10, 0x01}, 3, "movups xmm0, [rcx]");
    expect_length({0x66, 0x0F, 0x70, 0xC0, 0x1B}, 5, "pshufd takes an 8-bit selector");
    expect_length({0x66, 0x0F, 0x38, 0x17, 0xC1}, 5, "ptest, from the 0F38 map");
    expect_length({0x66, 0x0F, 0x3A, 0x0B, 0xC0, 0x04}, 6, "roundsd, from the 0F3A map with its selector");
    expect_length({0xC5, 0xF8, 0x10, 0x01}, 4, "vmovups xmm0, [rcx] (two-byte VEX)");
    expect_length({0xC5, 0xFC, 0x57, 0xC0}, 4, "vxorps ymm0, ymm0, ymm0");
    expect_length({0xC4, 0xE2, 0x7D, 0x18, 0x01}, 5, "vbroadcastss ymm0, [rcx] (three-byte VEX, 0F38)");
    expect_length({0xC4, 0xE3, 0x7D, 0x39, 0xC1, 0x01}, 6, "vextracti128 (three-byte VEX, 0F3A)");
    expect_length({0x62, 0xF1, 0x7C, 0x48, 0x28, 0xC1}, 6, "vmovaps zmm0, zmm1 (EVEX)");
    const Instruction vex_rip = decode({0xC5, 0xFC, 0x28, 0x05, 0x00, 0x00, 0x00, 0x00});
    require(vex_rip.length == 8 && vex_rip.rip_relative && vex_rip.rip_target == 0x140001008ull,
            "a VEX instruction still reaches data through rip");
    require(vex_rip.vector_length == 1, "the VEX payload carries the vector width");

    // Encodings that do not exist in 64-bit mode must be refused, not guessed
    // at: hitting one means the walk has run into data.
    require(!decodes({0x06}), "push es");
    require(!decodes({0x60}), "pushad");
    require(!decodes({0x9A, 0, 0, 0, 0, 0, 0}), "far call");
    require(!decodes({0xEA, 0, 0, 0, 0, 0, 0}), "far jump");
    require(!decodes({0x67, 0x8B, 0x01}), "the address size prefix changes rules the decoder does not model");
    require(!decodes({0x48, 0x8B}), "a truncated instruction is not a decode");

    // Structural comparison: registers are free to move, everything else is not.
    const Instruction from_body = decode({0x80, 0x79, 0x2C, 0x00});
    const Instruction from_copy = decode({0x80, 0x7B, 0x2C, 0x00});
    require(Explorer::X86::same_shape(from_body, from_copy), "the same access through another base register matches");
    require(!Explorer::X86::same_shape(from_body, decode({0x80, 0x7B, 0x30, 0x00})),
            "a different field offset is a different instruction");
    require(!Explorer::X86::same_shape(decode({0x83, 0x41, 0x20, 0xFB}), decode({0x83, 0x41, 0x20, 0xFA})),
            "a different constant is a different instruction");
    require(!Explorer::X86::same_shape(decode({0x83, 0x41, 0x20, 0x05}), decode({0x83, 0x69, 0x20, 0x05})),
            "add and sub share an opcode and differ in the group field");
    require(!Explorer::X86::same_shape(decode({0x8B, 0x01}), decode({0x48, 0x8B, 0x01})),
            "the wide form is a different operation");
    require(Explorer::X86::same_shape(decode({0xE8, 0x00, 0x01, 0x00, 0x00}),
                                      decode({0xE8, 0x00, 0x02, 0x00, 0x00})),
            "two calls match: an inlined copy branches elsewhere");
    require(!Explorer::X86::same_shape(decode({0xC5, 0xF8, 0x57, 0xC0}), decode({0xC5, 0xFC, 0x57, 0xC0})),
            "128-bit and 256-bit forms are different operations");
    require(!Explorer::X86::same_shape(decode({0x0F, 0x10, 0x01}), decode({0xF3, 0x0F, 0x10, 0x01})),
            "the mandatory prefix selects the operation");
    require(Explorer::X86::is_register_move(decode({0x48, 0x8B, 0xD9})), "mov rbx, rcx is a register move");
    require(!Explorer::X86::is_register_move(decode({0x48, 0x8B, 0x19})), "a load from memory is not");

    std::cout << "x86 decode contract passed\n";
    return 0;
}

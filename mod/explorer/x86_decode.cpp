// Copyright (c) 2026 Jadis0x. All rights reserved.
#include "x86_decode.h"

#include <cstring>

namespace Explorer::X86 {
namespace {

enum Form : std::uint16_t {
    form_invalid = 0,
    form_none = 1u << 0,
    form_modrm = 1u << 1,
    form_imm8 = 1u << 2,
    form_imm16 = 1u << 3,
    // 4 bytes, or 2 with the 0x66 prefix.
    form_immz = 1u << 4,
    // enter imm16, imm8.
    form_imm16_imm8 = 1u << 10,
    // A 64-bit absolute address operand, whatever REX says.
    form_moffs64 = 1u << 11,
    form_rel8 = 1u << 5,
    form_rel32 = 1u << 6,
    // mov r64, imm64 when REX.W is set.
    form_imm_moffs = 1u << 7,
    // Immediate width comes from the modrm.reg field (groups 3 and 3b).
    form_group_f6 = 1u << 8,
    form_group_f7 = 1u << 9,
};

// One-byte opcode map. Prefix bytes are consumed before the lookup, so their
// slots stay invalid.
constexpr std::uint16_t one_byte_map[256] = {
    // 0x00 add, 0x08 or, 0x10 adc, 0x18 sbb
    form_modrm, form_modrm, form_modrm, form_modrm, form_imm8, form_immz, form_invalid, form_invalid,
    form_modrm, form_modrm, form_modrm, form_modrm, form_imm8, form_immz, form_invalid, form_invalid,
    form_modrm, form_modrm, form_modrm, form_modrm, form_imm8, form_immz, form_invalid, form_invalid,
    form_modrm, form_modrm, form_modrm, form_modrm, form_imm8, form_immz, form_invalid, form_invalid,
    // 0x20 and, 0x28 sub, 0x30 xor, 0x38 cmp
    form_modrm, form_modrm, form_modrm, form_modrm, form_imm8, form_immz, form_invalid, form_invalid,
    form_modrm, form_modrm, form_modrm, form_modrm, form_imm8, form_immz, form_invalid, form_invalid,
    form_modrm, form_modrm, form_modrm, form_modrm, form_imm8, form_immz, form_invalid, form_invalid,
    form_modrm, form_modrm, form_modrm, form_modrm, form_imm8, form_immz, form_invalid, form_invalid,
    // 0x40..0x4F REX prefixes, consumed earlier
    form_invalid, form_invalid, form_invalid, form_invalid, form_invalid, form_invalid, form_invalid, form_invalid,
    form_invalid, form_invalid, form_invalid, form_invalid, form_invalid, form_invalid, form_invalid, form_invalid,
    // 0x50..0x5F push/pop r64
    form_none, form_none, form_none, form_none, form_none, form_none, form_none, form_none,
    form_none, form_none, form_none, form_none, form_none, form_none, form_none, form_none,
    // 0x60..0x6F
    form_invalid, form_invalid, form_invalid, form_modrm, form_invalid, form_invalid, form_invalid, form_invalid,
    form_immz, static_cast<std::uint16_t>(form_modrm | form_immz), form_imm8,
    static_cast<std::uint16_t>(form_modrm | form_imm8), form_none, form_none, form_none, form_none,
    // 0x70..0x7F jcc rel8
    form_rel8, form_rel8, form_rel8, form_rel8, form_rel8, form_rel8, form_rel8, form_rel8,
    form_rel8, form_rel8, form_rel8, form_rel8, form_rel8, form_rel8, form_rel8, form_rel8,
    // 0x80..0x8F
    static_cast<std::uint16_t>(form_modrm | form_imm8), static_cast<std::uint16_t>(form_modrm | form_immz),
    form_invalid, static_cast<std::uint16_t>(form_modrm | form_imm8),
    form_modrm, form_modrm, form_modrm, form_modrm,
    form_modrm, form_modrm, form_modrm, form_modrm, form_modrm, form_modrm, form_modrm, form_modrm,
    // 0x90..0x9F
    form_none, form_none, form_none, form_none, form_none, form_none, form_none, form_none,
    form_none, form_none, form_invalid, form_none, form_none, form_none, form_none, form_none,
    // 0xA0..0xAF
    form_moffs64, form_moffs64, form_moffs64, form_moffs64, form_none, form_none, form_none, form_none,
    form_imm8, form_immz, form_none, form_none, form_none, form_none, form_none, form_none,
    // 0xB0..0xB7 mov r8, imm8
    form_imm8, form_imm8, form_imm8, form_imm8, form_imm8, form_imm8, form_imm8, form_imm8,
    // 0xB8..0xBF mov r32/r64, immz/imm64
    form_imm_moffs, form_imm_moffs, form_imm_moffs, form_imm_moffs,
    form_imm_moffs, form_imm_moffs, form_imm_moffs, form_imm_moffs,
    // 0xC0..0xCF
    static_cast<std::uint16_t>(form_modrm | form_imm8), static_cast<std::uint16_t>(form_modrm | form_imm8),
    form_imm16, form_none, form_invalid, form_invalid,
    static_cast<std::uint16_t>(form_modrm | form_imm8), static_cast<std::uint16_t>(form_modrm | form_immz),
    form_imm16_imm8, form_none, form_imm16, form_none, form_none, form_imm8, form_invalid, form_none,
    // 0xD0..0xDF
    form_modrm, form_modrm, form_modrm, form_modrm, form_invalid, form_invalid, form_invalid, form_none,
    form_modrm, form_modrm, form_modrm, form_modrm, form_modrm, form_modrm, form_modrm, form_modrm,
    // 0xE0..0xEF
    form_rel8, form_rel8, form_rel8, form_rel8, form_imm8, form_imm8, form_imm8, form_imm8,
    form_rel32, form_rel32, form_invalid, form_rel8, form_none, form_none, form_none, form_none,
    // 0xF0..0xFF (0xF0/0xF2/0xF3 are prefixes, consumed earlier)
    form_invalid, form_none, form_invalid, form_invalid, form_none, form_none, form_group_f6, form_group_f7,
    form_none, form_none, form_none, form_none, form_none, form_none, form_modrm, form_modrm,
};

// 0F38 holds SSSE3/SSE4/AVX operations that take modrm and no immediate; 0F3A
// holds the ones that take an 8-bit selector.
std::uint16_t three_byte_form(Map map, std::uint8_t opcode) {
    // The far-jump-like slots in 0F38 do not exist; every defined opcode in both
    // maps uses modrm.
    (void)opcode;
    return map == Map::three_byte_3a ? static_cast<std::uint16_t>(form_modrm | form_imm8) : form_modrm;
}

std::uint16_t two_byte_form(std::uint8_t opcode) {
    switch (opcode) {
    case 0x05:
    case 0x0B:
    case 0x30:
    case 0x31:
    case 0x32:
    case 0x33:
    case 0xA0:
    case 0xA1:
    case 0xA2:
    case 0xA8:
    case 0xA9:
    case 0xC8:
    case 0xC9:
    case 0xCA:
    case 0xCB:
    case 0xCC:
    case 0xCD:
    case 0xCE:
    case 0xCF:
        return form_none;
    case 0x0F:
        return form_invalid;
    default:
        break;
    }
    if (opcode >= 0x80 && opcode <= 0x8F)
        return form_rel32;
    // pshufd/pshufhw and the shift groups, then the compare/insert/extract
    // family: modrm plus an 8-bit selector.
    if ((opcode >= 0x70 && opcode <= 0x73) || opcode == 0xA4 || opcode == 0xAC || opcode == 0xBA ||
        opcode == 0xC2 || opcode == 0xC4 || opcode == 0xC5 || opcode == 0xC6)
        return static_cast<std::uint16_t>(form_modrm | form_imm8);
    if (opcode == 0xFF)
        return form_invalid;
    return form_modrm;
}

std::int64_t read_signed(const std::uint8_t *code, std::uint8_t size) {
    switch (size) {
    case 1:
        return static_cast<std::int8_t>(code[0]);
    case 2: {
        std::int16_t value = 0;
        std::memcpy(&value, code, sizeof(value));
        return value;
    }
    case 4: {
        std::int32_t value = 0;
        std::memcpy(&value, code, sizeof(value));
        return value;
    }
    case 8: {
        std::int64_t value = 0;
        std::memcpy(&value, code, sizeof(value));
        return value;
    }
    default:
        return 0;
    }
}

} // namespace

bool decode(const std::uint8_t *code, std::size_t available, std::uintptr_t address, Instruction &out) {
    if (!code || available == 0)
        return false;
    out = Instruction{};
    out.address = address;

    std::size_t cursor = 0;
    bool address_size_prefix = false;
    for (; cursor < available; ++cursor) {
        const std::uint8_t byte = code[cursor];
        if (byte == 0x66) {
            out.operand_size_prefix = true;
            out.simd_prefix = 1;
        } else if (byte == 0x67) {
            address_size_prefix = true;
        } else if (byte == 0xF3) {
            out.repeat_prefix = byte;
            out.simd_prefix = 2;
        } else if (byte == 0xF2) {
            out.repeat_prefix = byte;
            out.simd_prefix = 3;
        } else if (byte == 0xF0 || byte == 0x2E || byte == 0x36 || byte == 0x3E || byte == 0x26 || byte == 0x64 ||
                   byte == 0x65) {
            // Lock and segment overrides carry no operand information.
        } else {
            break;
        }
    }
    // 32-bit addressing inside 64-bit code is rare and changes every
    // displacement rule; refuse rather than mis-measure the instruction.
    if (address_size_prefix || cursor >= available)
        return false;

    bool rex_w = false, rex_r = false, rex_x = false, rex_b = false;
    const std::uint8_t lead = code[cursor];
    if (lead == 0xC5 || lead == 0xC4 || lead == 0x62) {
        // VEX and EVEX carry the escape map, the mandatory SIMD prefix, the
        // operand width and the register extensions in the prefix itself. C4
        // and C5 store them inverted, which is what keeps the encoding from
        // colliding with the legacy opcodes those bytes used to mean.
        const std::size_t payload = lead == 0xC5 ? 1u : lead == 0xC4 ? 2u : 3u;
        if (cursor + payload + 1 > available)
            return false;
        const std::uint8_t *bytes = code + cursor + 1;
        std::uint8_t map_selector = 1;
        if (lead == 0xC5) {
            rex_r = (bytes[0] & 0x80) == 0;
            out.simd_prefix = static_cast<std::uint8_t>(bytes[0] & 0x03);
            out.vector_length = static_cast<std::uint8_t>((bytes[0] >> 2) & 1);
        } else if (lead == 0xC4) {
            rex_r = (bytes[0] & 0x80) == 0;
            rex_x = (bytes[0] & 0x40) == 0;
            rex_b = (bytes[0] & 0x20) == 0;
            map_selector = static_cast<std::uint8_t>(bytes[0] & 0x1F);
            rex_w = (bytes[1] & 0x80) != 0;
            out.simd_prefix = static_cast<std::uint8_t>(bytes[1] & 0x03);
            out.vector_length = static_cast<std::uint8_t>((bytes[1] >> 2) & 1);
        } else {
            rex_r = (bytes[0] & 0x80) == 0;
            rex_x = (bytes[0] & 0x40) == 0;
            rex_b = (bytes[0] & 0x20) == 0;
            map_selector = static_cast<std::uint8_t>(bytes[0] & 0x07);
            rex_w = (bytes[1] & 0x80) != 0;
            out.simd_prefix = static_cast<std::uint8_t>(bytes[1] & 0x03);
            // The two length bits are split across the payload in EVEX.
            out.vector_length = static_cast<std::uint8_t>((bytes[2] >> 5) & 0x03);
        }
        if (map_selector < 1 || map_selector > 3)
            return false;
        out.map = static_cast<Map>(map_selector);
        out.vector_encoding = true;
        cursor += payload + 1;
    } else {
        if (lead >= 0x40 && lead <= 0x4F) {
            out.rex = lead;
            rex_w = (lead & 0x08) != 0;
            rex_r = (lead & 0x04) != 0;
            rex_x = (lead & 0x02) != 0;
            rex_b = (lead & 0x01) != 0;
            ++cursor;
            if (cursor >= available)
                return false;
        }
        if (code[cursor] == 0x0F) {
            ++cursor;
            if (cursor >= available)
                return false;
            if (code[cursor] == 0x38 || code[cursor] == 0x3A) {
                out.map = code[cursor] == 0x38 ? Map::three_byte_38 : Map::three_byte_3a;
                ++cursor;
                if (cursor >= available)
                    return false;
            } else {
                out.map = Map::two_byte;
            }
        }
    }
    out.wide = rex_w;
    if (cursor >= available)
        return false;
    out.opcode = code[cursor];
    ++cursor;

    std::uint16_t form = form_invalid;
    switch (out.map) {
    case Map::one_byte:
        form = one_byte_map[out.opcode];
        break;
    case Map::two_byte:
        form = two_byte_form(out.opcode);
        break;
    default:
        form = three_byte_form(out.map, out.opcode);
        break;
    }
    // Every VEX and EVEX opcode takes a modrm byte; the only immediate any of
    // them carries is the 8-bit selector its map defines.
    if (out.vector_encoding) {
        form = out.map == Map::three_byte_3a ? static_cast<std::uint16_t>(form_modrm | form_imm8)
                                             : static_cast<std::uint16_t>(form_modrm | (form & form_imm8));
    }
    if (form == form_invalid)
        return false;

    if ((form & (form_modrm | form_group_f6 | form_group_f7)) != 0) {
        if (cursor >= available)
            return false;
        out.has_modrm = true;
        out.modrm = code[cursor];
        ++cursor;
        const std::uint8_t mod = static_cast<std::uint8_t>(out.modrm >> 6);
        const std::uint8_t reg = static_cast<std::uint8_t>((out.modrm >> 3) & 7);
        const std::uint8_t rm = static_cast<std::uint8_t>(out.modrm & 7);
        out.reg_register = static_cast<std::uint8_t>(reg | (rex_r ? 8 : 0));
        if (mod == 3) {
            out.rm_register = static_cast<std::uint8_t>(rm | (rex_b ? 8 : 0));
        } else {
            out.memory_operand = true;
            std::uint8_t displacement_size = mod == 1 ? 1 : mod == 2 ? 4 : 0;
            if (rm == 4) {
                if (cursor >= available)
                    return false;
                const std::uint8_t sib = code[cursor];
                ++cursor;
                const std::uint8_t index = static_cast<std::uint8_t>(((sib >> 3) & 7) | (rex_x ? 8 : 0));
                const std::uint8_t base = static_cast<std::uint8_t>(sib & 7);
                if (index != 4)
                    out.index_register = index;
                if (base == 5 && mod == 0)
                    displacement_size = 4;
                else
                    out.base_register = static_cast<std::uint8_t>(base | (rex_b ? 8 : 0));
            } else if (rm == 5 && mod == 0) {
                out.rip_relative = true;
                displacement_size = 4;
            } else {
                out.base_register = static_cast<std::uint8_t>(rm | (rex_b ? 8 : 0));
            }
            if (displacement_size != 0) {
                if (cursor + displacement_size > available)
                    return false;
                out.displacement_offset = cursor;
                out.displacement_size = displacement_size;
                out.displacement = read_signed(code + cursor, displacement_size);
                cursor += displacement_size;
            }
        }
        if (form == form_group_f6)
            form = static_cast<std::uint16_t>(reg <= 1 ? form_imm8 : form_none);
        else if (form == form_group_f7)
            form = static_cast<std::uint16_t>(reg <= 1 ? form_immz : form_none);
    }

    std::uint8_t immediate_size = 0;
    if ((form & form_imm8) != 0)
        immediate_size = 1;
    else if ((form & form_imm16) != 0)
        immediate_size = 2;
    else if ((form & form_immz) != 0)
        immediate_size = out.operand_size_prefix ? 2 : 4;
    else if ((form & form_moffs64) != 0)
        immediate_size = 8;
    else if ((form & form_imm_moffs) != 0)
        immediate_size = rex_w ? 8 : out.operand_size_prefix ? 2 : 4;
    else if ((form & form_imm16_imm8) != 0)
        // enter: a 16-bit frame size followed by an 8-bit nesting level.
        immediate_size = 3;

    if (immediate_size != 0) {
        if (cursor + immediate_size > available)
            return false;
        out.immediate_size = immediate_size;
        out.immediate = read_signed(code + cursor, immediate_size == 3 ? 2 : immediate_size);
        cursor += immediate_size;
    }

    if ((form & (form_rel8 | form_rel32)) != 0) {
        const std::uint8_t size = (form & form_rel8) != 0 ? 1 : 4;
        if (cursor + size > available)
            return false;
        const std::int64_t relative = read_signed(code + cursor, size);
        cursor += size;
        out.is_relative_branch = true;
        out.branch_target = static_cast<std::uintptr_t>(static_cast<std::int64_t>(address + cursor) + relative);
        out.is_call = out.map == Map::one_byte && out.opcode == 0xE8;
        out.is_unconditional_jump = out.map == Map::one_byte && (out.opcode == 0xE9 || out.opcode == 0xEB);
    }
    if (out.map == Map::one_byte && (out.opcode == 0xC3 || out.opcode == 0xC2))
        out.is_return = true;
    // An indirect jump ends a straight-line run the same way `jmp rel` does.
    if (out.map == Map::one_byte && out.opcode == 0xFF && out.has_modrm) {
        const std::uint8_t reg = static_cast<std::uint8_t>((out.modrm >> 3) & 7);
        if (reg == 4 || reg == 5)
            out.is_unconditional_jump = true;
        else if (reg == 2 || reg == 3)
            out.is_call = true;
    }

    out.length = cursor;
    if (out.rip_relative)
        out.rip_target = static_cast<std::uintptr_t>(static_cast<std::int64_t>(address + out.length) + out.displacement);
    return out.length != 0;
}

bool is_register_move(const Instruction &instruction) {
    return instruction.map == Map::one_byte && !instruction.vector_encoding &&
           (instruction.opcode == 0x89 || instruction.opcode == 0x8B) && instruction.has_modrm &&
           !instruction.memory_operand;
}

bool same_shape(const Instruction &left, const Instruction &right) {
    if (left.map != right.map || left.opcode != right.opcode)
        return false;
    // A SIMD opcode means a different operation under each mandatory prefix, and
    // the vector width is part of the operation too. Which encoding carried them
    // is not: the same work can arrive as SSE or as VEX.
    if (left.simd_prefix != right.simd_prefix || left.vector_length != right.vector_length)
        return false;
    // The wide form of an operation is a different operation; the register
    // extension bits only say which register was picked.
    if (left.wide != right.wide)
        return false;
    if (left.has_modrm != right.has_modrm)
        return false;
    if (left.has_modrm) {
        if (left.memory_operand != right.memory_operand)
            return false;
        // modrm.reg names a register the compiler is free to reallocate, except
        // in a group encoding where it selects the operation itself.
        const bool group = left.map == Map::one_byte && (left.opcode == 0x80 || left.opcode == 0x81 || left.opcode == 0x83 ||
                                             left.opcode == 0xC0 || left.opcode == 0xC1 || left.opcode == 0xD0 ||
                                             left.opcode == 0xD1 || left.opcode == 0xD2 || left.opcode == 0xD3 ||
                                             left.opcode == 0xF6 || left.opcode == 0xF7 || left.opcode == 0xFE ||
                                             left.opcode == 0xFF);
        if (group && ((left.modrm >> 3) & 7) != ((right.modrm >> 3) & 7))
            return false;
        if (left.memory_operand) {
            if (left.rip_relative != right.rip_relative)
                return false;
            if (left.rip_relative) {
                if (left.rip_target != right.rip_target)
                    return false;
            } else if (left.displacement != right.displacement) {
                return false;
            }
            if ((left.index_register == no_register) != (right.index_register == no_register))
                return false;
        }
    }
    if (left.immediate_size != right.immediate_size || left.immediate != right.immediate)
        return false;
    // Two branches of the same kind match; where they jump is the caller's
    // business, and an inlined copy always jumps somewhere else.
    return true;
}

} // namespace Explorer::X86

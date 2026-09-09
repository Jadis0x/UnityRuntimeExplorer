// Copyright (c) 2026 Jadis0x. All rights reserved.
#include "inline_sites.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <algorithm>
#include <cstring>
#include <unordered_set>

namespace Explorer::InlineSites {
namespace {

// A body longer than this is not the kind of method a compiler inlines, and
// decoding further only costs time.
constexpr std::size_t max_body_bytes = 512;
constexpr std::size_t max_body_instructions = 128;
// Functions carrying a copy are ordinary managed methods; a very long one is
// still worth decoding, but not without a bound.
constexpr std::size_t max_function_bytes = 16384;
constexpr std::size_t max_function_instructions = 4096;
// An anchor referenced from more places than this says nothing about identity.
constexpr std::size_t max_anchor_hits = 128;
constexpr std::size_t max_candidates = 512;
// The shortest run that is evidence rather than coincidence. A short body can
// only offer what it has, so the requirement is the whole body up to this many
// instructions: matching four instructions of a forty-instruction method says
// the two share a helper, not that one is a copy of the other.
constexpr std::size_t min_matched_instructions = 3;
constexpr std::size_t strong_match_instructions = 6;
// Bytes a mid-function hook overwrites.
constexpr std::size_t hook_patch_bytes = 5;

struct Section {
    std::uintptr_t start = 0;
    std::size_t size = 0;
    bool executable = false;
};

struct Function {
    std::uintptr_t start = 0;
    std::uintptr_t end = 0;
    std::uint32_t unwind = 0;
};

class Module {
public:
    bool open(const Image &image) {
        image_ = image;
        if (!image_.data || image_.size < sizeof(IMAGE_DOS_HEADER))
            return false;
        const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(image_.data);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE)
            return false;
        const auto header_offset = static_cast<std::size_t>(dos->e_lfanew);
        if (header_offset + sizeof(IMAGE_NT_HEADERS64) > image_.size)
            return false;
        const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS64 *>(image_.data + header_offset);
        if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
            return false;

        const IMAGE_SECTION_HEADER *section = IMAGE_FIRST_SECTION(nt);
        for (WORD index = 0; index < nt->FileHeader.NumberOfSections; ++index, ++section) {
            const std::size_t size = section->Misc.VirtualSize;
            if (size == 0 || static_cast<std::size_t>(section->VirtualAddress) + size > image_.size)
                continue;
            sections_.push_back(Section{image_.base + section->VirtualAddress, size,
                                        (section->Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0});
        }

        const IMAGE_DATA_DIRECTORY &exception =
            nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
        if (exception.VirtualAddress != 0 && exception.Size >= sizeof(RUNTIME_FUNCTION) &&
            static_cast<std::size_t>(exception.VirtualAddress) + exception.Size <= image_.size) {
            const auto *entries = reinterpret_cast<const RUNTIME_FUNCTION *>(image_.data + exception.VirtualAddress);
            const std::size_t count = exception.Size / sizeof(RUNTIME_FUNCTION);
            functions_.reserve(count);
            for (std::size_t index = 0; index < count; ++index) {
                const RUNTIME_FUNCTION &entry = entries[index];
                if (entry.BeginAddress == 0 || entry.EndAddress <= entry.BeginAddress)
                    continue;
                functions_.push_back(Function{image_.base + entry.BeginAddress, image_.base + entry.EndAddress,
                                              entry.UnwindInfoAddress});
            }
            std::sort(functions_.begin(), functions_.end(),
                      [](const Function &left, const Function &right) { return left.start < right.start; });
        }
        return true;
    }

    const Image &image() const { return image_; }
    const std::vector<Section> &sections() const { return sections_; }

    bool contains(std::uintptr_t address) const {
        return address >= image_.base && address < image_.base + image_.size;
    }

    const std::uint8_t *at(std::uintptr_t address) const {
        return contains(address) ? image_.data + (address - image_.base) : nullptr;
    }

    std::size_t bytes_after(std::uintptr_t address) const {
        return contains(address) ? image_.size - (address - image_.base) : 0;
    }

    // The .pdata fragment covering an address. MSVC splits a function into
    // several fragments, so this is a decodable range, not necessarily the
    // whole method.
    const Function *fragment(std::uintptr_t address) const {
        if (functions_.empty())
            return nullptr;
        auto upper = std::upper_bound(functions_.begin(), functions_.end(), address,
                                      [](std::uintptr_t value, const Function &entry) { return value < entry.start; });
        if (upper == functions_.begin())
            return nullptr;
        --upper;
        return address >= upper->start && address < upper->end ? &*upper : nullptr;
    }

    // Walks chained unwind info back to the fragment holding the entry point,
    // which is the address managed metadata knows the method by.
    std::uintptr_t primary_start(const Function &entry) const {
        const Function *current = &entry;
        Function storage{};
        for (int depth = 0; depth < 8; ++depth) {
            const std::uint8_t *unwind = at(image_.base + current->unwind);
            if (!unwind || bytes_after(image_.base + current->unwind) < 4)
                break;
            const std::uint8_t flags = static_cast<std::uint8_t>(unwind[0] >> 3);
            if ((flags & UNW_FLAG_CHAININFO) == 0)
                break;
            const std::size_t codes = unwind[2];
            const std::size_t offset = 4 + ((codes + 1) & ~static_cast<std::size_t>(1)) * 2;
            if (bytes_after(image_.base + current->unwind) < offset + sizeof(RUNTIME_FUNCTION))
                break;
            RUNTIME_FUNCTION chained{};
            std::memcpy(&chained, unwind + offset, sizeof(chained));
            if (chained.BeginAddress == 0)
                break;
            storage = Function{image_.base + chained.BeginAddress, image_.base + chained.EndAddress,
                               chained.UnwindInfoAddress};
            current = &storage;
        }
        return current->start;
    }

private:
    Image image_{};
    std::vector<Section> sections_;
    std::vector<Function> functions_;
};

std::vector<X86::Instruction> decode_run(const Module &module, std::uintptr_t start, std::uintptr_t end,
                                         std::size_t max_instructions) {
    std::vector<X86::Instruction> out;
    std::uintptr_t cursor = start;
    while (cursor < end && out.size() < max_instructions) {
        const std::uint8_t *code = module.at(cursor);
        if (!code)
            break;
        X86::Instruction instruction{};
        const std::size_t available = std::min<std::size_t>(module.bytes_after(cursor), 16);
        if (!X86::decode(code, available, cursor, instruction))
            break;
        cursor += instruction.length;
        out.push_back(instruction);
    }
    return out;
}

// The first instruction that is part of the method's work rather than its
// frame setup. An inlined copy has no frame of its own, so matching starts here.
std::size_t body_entry_index(const std::vector<X86::Instruction> &body) {
    std::size_t index = 0;
    for (; index < body.size(); ++index) {
        const X86::Instruction &instruction = body[index];
        // push r64
        if (instruction.map == X86::Map::one_byte && instruction.opcode >= 0x50 && instruction.opcode <= 0x57)
            continue;
        // sub rsp, imm
        if (instruction.map == X86::Map::one_byte && (instruction.opcode == 0x81 || instruction.opcode == 0x83) &&
            instruction.has_modrm && !instruction.memory_operand && instruction.rm_register == 4)
            continue;
        if (X86::is_register_move(instruction))
            continue;
        // mov [rsp+n], reg / movaps [rsp+n], xmm: register spills
        if (instruction.memory_operand && instruction.base_register == 4 &&
            ((instruction.map == X86::Map::one_byte && (instruction.opcode == 0x89 || instruction.opcode == 0x88)) ||
             (instruction.map == X86::Map::two_byte && (instruction.opcode == 0x29 || instruction.opcode == 0x11))))
            continue;
        break;
    }
    return index;
}

struct Anchor {
    std::uintptr_t target = 0;
    std::size_t body_index = 0;
};

// Absolute addresses the body names: rip-relative data references and the
// targets of direct calls. Inlining copies these unchanged.
std::vector<Anchor> collect_anchors(const Module &module, const std::vector<X86::Instruction> &body,
                                    std::uintptr_t body_start, std::uintptr_t body_end) {
    std::vector<Anchor> anchors;
    for (std::size_t index = 0; index < body.size(); ++index) {
        const X86::Instruction &instruction = body[index];
        std::uintptr_t target = 0;
        if (instruction.rip_relative)
            target = instruction.rip_target;
        else if (instruction.is_call && instruction.is_relative_branch)
            target = instruction.branch_target;
        if (target == 0 || !module.contains(target))
            continue;
        // A reference into the body itself travels with neither copy.
        if (target >= body_start && target < body_end)
            continue;
        anchors.push_back(Anchor{target, index});
    }
    return anchors;
}

struct Candidate {
    std::uintptr_t target = 0;
    // Offset of the 4-byte displacement that produced the reference.
    std::uintptr_t displacement_address = 0;
};

struct ScanResult {
    std::size_t direct_calls = 0;
    std::size_t address_refs = 0;
    std::vector<Candidate> candidates;
    std::vector<std::uintptr_t> call_sites;
};

// Call sites a trace will hook when the method itself is too small to host one.
constexpr std::size_t max_recorded_call_sites = 32;

// One pass over the module: how many branches reach the entry point, how often
// its address appears as data, and where each anchor is referenced from.
ScanResult scan(const Module &module, std::uintptr_t method_entry, const std::vector<Anchor> &anchors) {
    ScanResult out{};
    std::vector<std::uintptr_t> targets;
    targets.reserve(anchors.size());
    for (const Anchor &anchor : anchors)
        targets.push_back(anchor.target);
    std::sort(targets.begin(), targets.end());
    targets.erase(std::unique(targets.begin(), targets.end()), targets.end());
    std::vector<std::size_t> hits(targets.size(), 0);

    const Image &image = module.image();
    for (const Section &section : module.sections()) {
        const std::uint8_t *start = module.at(section.start);
        if (!start)
            continue;
        if (section.executable) {
            for (std::size_t offset = 0; offset + 5 <= section.size; ++offset) {
                const std::uint8_t opcode = start[offset];
                std::int32_t displacement = 0;
                if (opcode == 0xE8 || opcode == 0xE9) {
                    std::memcpy(&displacement, start + offset + 1, sizeof(displacement));
                    const std::uintptr_t next = section.start + offset + 5;
                    if (next + static_cast<std::uintptr_t>(static_cast<std::intptr_t>(displacement)) == method_entry) {
                        ++out.direct_calls;
                        if (opcode == 0xE8 && out.call_sites.size() < max_recorded_call_sites)
                            out.call_sites.push_back(section.start + offset);
                    }
                }
                if (targets.empty() || offset + 4 > section.size || out.candidates.size() >= max_candidates)
                    continue;
                // A rip-relative or rel32 field: the referenced address is the
                // displacement plus the end of the instruction, which sits 0 to
                // 4 bytes past the field depending on a trailing immediate.
                std::memcpy(&displacement, start + offset, sizeof(displacement));
                const std::uintptr_t field_end = section.start + offset + 4;
                const auto base = static_cast<std::uintptr_t>(static_cast<std::intptr_t>(field_end) + displacement);
                if (base < image.base || base >= image.base + image.size + 4)
                    continue;
                for (const std::uintptr_t trailing : {std::uintptr_t{0}, std::uintptr_t{1}, std::uintptr_t{2},
                                                      std::uintptr_t{4}}) {
                    const std::uintptr_t candidate_target = base + trailing;
                    const auto found = std::lower_bound(targets.begin(), targets.end(), candidate_target);
                    if (found == targets.end() || *found != candidate_target)
                        continue;
                    const std::size_t index = static_cast<std::size_t>(found - targets.begin());
                    if (hits[index] >= max_anchor_hits)
                        break;
                    ++hits[index];
                    out.candidates.push_back(Candidate{candidate_target, section.start + offset});
                    break;
                }
            }
        }
        for (std::size_t offset = 0; offset + 8 <= section.size; offset += 8) {
            std::uintptr_t value = 0;
            std::memcpy(&value, start + offset, sizeof(value));
            if (value == method_entry)
                ++out.address_refs;
        }
    }
    // An anchor everything references identifies nothing; drop its hits.
    std::vector<Candidate> kept;
    kept.reserve(out.candidates.size());
    for (const Candidate &candidate : out.candidates) {
        const auto found = std::lower_bound(targets.begin(), targets.end(), candidate.target);
        if (found != targets.end() && *found == candidate.target &&
            hits[static_cast<std::size_t>(found - targets.begin())] < max_anchor_hits)
            kept.push_back(candidate);
    }
    out.candidates = std::move(kept);
    return out;
}

// Aligns the caller's code against the body around a shared anchor and returns
// the run that matches, expressed as index pairs.
struct Alignment {
    bool valid = false;
    std::size_t caller_start = 0;
    std::size_t body_start = 0;
    std::size_t matched = 0;
    // Body instructions the run spans, including the register shuffles the copy
    // dropped. How much of the body was recognised, as opposed to how many
    // instructions happened to line up.
    std::size_t body_span = 0;
    // Register standing in for the body's `this`. The body reaches fields
    // through rcx, so whichever register the copy uses in the same instruction
    // holds the instance there.
    std::uint8_t instance_register = X86::no_register;
};

// Which registers hold `this` at each point in the standalone body. It arrives
// in rcx, spreads through register moves, and a call clobbers every volatile
// register holding it. Matching a field access against the copy then names the
// register the copy keeps the instance in.
std::vector<std::uint16_t> instance_alias_masks(const std::vector<X86::Instruction> &body) {
    // rbx, rbp, rsi, rdi, r12..r15 survive a call under the Win64 ABI.
    constexpr std::uint16_t callee_saved = 0b1111'0000'1110'1000;
    std::vector<std::uint16_t> masks(body.size(), 0);
    std::uint16_t mask = static_cast<std::uint16_t>(1u << X86::register_rcx);
    for (std::size_t index = 0; index < body.size(); ++index) {
        masks[index] = mask;
        const X86::Instruction &instruction = body[index];
        if (X86::is_register_move(instruction)) {
            // 8B: reg <- r/m. 89: r/m <- reg.
            const std::uint8_t source =
                instruction.opcode == 0x8B ? instruction.rm_register : instruction.reg_register;
            const std::uint8_t destination =
                instruction.opcode == 0x8B ? instruction.reg_register : instruction.rm_register;
            if (destination < 16) {
                if (source < 16 && ((mask >> source) & 1) != 0)
                    mask |= static_cast<std::uint16_t>(1u << destination);
                else
                    mask &= static_cast<std::uint16_t>(~(1u << destination));
            }
            continue;
        }
        if (instruction.is_call) {
            mask &= callee_saved;
            continue;
        }
        // Any other write to a register form drops whatever it held.
        if (instruction.has_modrm && !instruction.memory_operand && instruction.rm_register < 16 &&
            !instruction.is_relative_branch)
            mask &= static_cast<std::uint16_t>(~(1u << instruction.rm_register));
    }
    return masks;
}

void note_instance_register(const X86::Instruction &source, const X86::Instruction &copy, std::uint16_t holders,
                            std::uint8_t &out) {
    if (source.memory_operand && source.base_register < 16 && ((holders >> source.base_register) & 1) != 0 &&
        copy.base_register != X86::no_register)
        out = copy.base_register;
}

Alignment align(const std::vector<X86::Instruction> &body, std::size_t body_index,
                const std::vector<X86::Instruction> &caller, std::size_t caller_index,
                const std::vector<std::uint16_t> &holders) {
    Alignment out{};
    if (body_index >= body.size() || caller_index >= caller.size())
        return out;
    if (!X86::same_shape(body[body_index], caller[caller_index]))
        return out;
    std::size_t matched = 1;
    std::size_t body_cursor = body_index;
    std::size_t caller_cursor = caller_index;
    std::uint8_t instance = X86::no_register;
    note_instance_register(body[body_index], caller[caller_index], holders[body_index], instance);
    // Backwards: a register shuffle the standalone copy kept usually disappears
    // when the body is inlined, so step over a few. Each earlier pair overwrites
    // the instance register, which leaves the earliest one.
    int skip_budget = 3;
    while (body_cursor > 0 && caller_cursor > 0) {
        if (X86::same_shape(body[body_cursor - 1], caller[caller_cursor - 1])) {
            --body_cursor;
            --caller_cursor;
            ++matched;
            note_instance_register(body[body_cursor], caller[caller_cursor], holders[body_cursor], instance);
            continue;
        }
        if (skip_budget > 0 && X86::is_register_move(body[body_cursor - 1])) {
            --body_cursor;
            --skip_budget;
            continue;
        }
        if (skip_budget > 0 && X86::is_register_move(caller[caller_cursor - 1])) {
            --caller_cursor;
            --skip_budget;
            continue;
        }
        break;
    }
    // Forwards the two sequences drift apart: the copy is interleaved with the
    // caller's own code, and the compiler drops instructions the surrounding
    // context makes redundant. A small window on each side rejoins them, and
    // since only real matches are counted, the score stays honest. This only
    // measures confidence - the site is already fixed by the walk above.
    std::size_t forward_body = body_index;
    std::size_t forward_caller = caller_index;
    skip_budget = 6;
    for (bool advanced = true; advanced;) {
        advanced = false;
        for (std::size_t gap = 0; gap <= static_cast<std::size_t>(skip_budget) && !advanced; ++gap) {
            for (std::size_t body_skip = 0; body_skip <= gap && !advanced; ++body_skip) {
                const std::size_t caller_skip = gap - body_skip;
                const std::size_t next_body = forward_body + 1 + body_skip;
                const std::size_t next_caller = forward_caller + 1 + caller_skip;
                if (next_body >= body.size() || next_caller >= caller.size())
                    continue;
                if (!X86::same_shape(body[next_body], caller[next_caller]))
                    continue;
                forward_body = next_body;
                forward_caller = next_caller;
                skip_budget -= static_cast<int>(gap);
                ++matched;
                if (instance == X86::no_register)
                    note_instance_register(body[forward_body], caller[forward_caller], holders[forward_body], instance);
                advanced = true;
            }
        }
    }
    out.valid = true;
    out.caller_start = caller_cursor;
    out.body_start = body_cursor;
    out.matched = matched;
    out.body_span = forward_body - body_cursor + 1;
    out.instance_register = instance;
    return out;
}

// A hook overwrites the first five bytes at the site. That is safe only when no
// branch lands inside them and the instructions are wholly inside the fragment.
bool patch_window_is_safe(const Module &module, const std::vector<X86::Instruction> &caller, std::size_t index,
                          std::uintptr_t fragment_end, std::size_t &patch_bytes) {
    const std::uintptr_t site = caller[index].address;
    std::uintptr_t covered = 0;
    std::size_t cursor = index;
    while (covered < hook_patch_bytes && cursor < caller.size()) {
        covered += caller[cursor].length;
        ++cursor;
    }
    if (covered < hook_patch_bytes)
        return false;
    const std::uintptr_t window_end = site + covered;
    if (window_end > fragment_end)
        return false;
    patch_bytes = static_cast<std::size_t>(covered);
    for (const X86::Instruction &instruction : caller) {
        if (!instruction.is_relative_branch)
            continue;
        if (instruction.branch_target > site && instruction.branch_target < window_end)
            return false;
    }
    // A switch dispatches through a table of addresses, and a table entry can
    // point into the window. Only a function that jumps indirectly has one, so
    // the search for such entries stays off the common path.
    const bool dispatches_indirectly = std::any_of(caller.begin(), caller.end(), [](const X86::Instruction &entry) {
        return entry.is_unconditional_jump && !entry.is_relative_branch;
    });
    if (!dispatches_indirectly)
        return true;
    const std::uintptr_t image_base = module.image().base;
    for (const Section &section : module.sections()) {
        const std::uint8_t *start = module.at(section.start);
        if (!start || section.executable)
            continue;
        for (std::size_t offset = 0; offset + 8 <= section.size; offset += 4) {
            std::uintptr_t absolute = 0;
            std::memcpy(&absolute, start + offset, sizeof(absolute));
            if (absolute > site && absolute < window_end)
                return false;
            // MSVC stores 32-bit image offsets in its jump tables.
            std::uint32_t relative = 0;
            std::memcpy(&relative, start + offset, sizeof(relative));
            const std::uintptr_t target = image_base + relative;
            if (target > site && target < window_end)
                return false;
        }
    }
    return true;
}

} // namespace

PatchFit patch_fit(const Image &image, std::uintptr_t entry, std::size_t bytes) {
    PatchFit out{};
    Module module;
    if (!module.open(image) || !module.contains(entry))
        return out;

    // Whole instructions have to cover the branch: a hook relocates what it
    // displaces, and half an instruction cannot be relocated.
    std::uintptr_t cursor = entry;
    std::size_t covered = 0;
    bool terminated = false;
    while (covered < bytes) {
        const std::uint8_t *code = module.at(cursor);
        if (!code)
            return out;
        X86::Instruction instruction{};
        if (!X86::decode(code, std::min<std::size_t>(module.bytes_after(cursor), 16), cursor, instruction))
            return out;
        covered += instruction.length;
        cursor += instruction.length;
        if (instruction.is_return || (instruction.is_unconditional_jump && !instruction.is_call)) {
            terminated = true;
            break;
        }
    }
    out.decoded = true;
    out.patch_bytes = covered;

    // How far this method provably extends. Unwind data says so exactly; a leaf
    // small enough to have none ends at its first return, and the int3 padding
    // after it is not ours to overwrite.
    const Function *fragment = module.fragment(entry);
    if (fragment) {
        out.function_bytes = static_cast<std::size_t>(fragment->end - entry);
    } else {
        std::uintptr_t walk = entry;
        std::size_t size = 0;
        for (std::size_t steps = 0; steps < max_body_instructions; ++steps) {
            const std::uint8_t *code = module.at(walk);
            if (!code)
                break;
            if (*code == 0xCC)
                break;
            X86::Instruction instruction{};
            if (!X86::decode(code, std::min<std::size_t>(module.bytes_after(walk), 16), walk, instruction))
                break;
            size += instruction.length;
            walk += instruction.length;
            if (instruction.is_return || (instruction.is_unconditional_jump && !instruction.is_call))
                break;
        }
        out.function_bytes = size;
    }
    out.safe = out.function_bytes >= out.patch_bytes && (covered >= bytes || terminated);
    // A body that ends before the branch fits has nowhere to put it.
    if (covered < bytes)
        out.safe = false;
    return out;
}

PatchFit patch_fit_in_process(const void *entry, std::size_t bytes) {
    PatchFit out{};
    if (!entry)
        return out;
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(entry), &module) ||
        !module) {
        return out;
    }
    const auto *base = reinterpret_cast<const std::uint8_t *>(module);
    const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
        return out;
    const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS64 *>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
        return out;
    Image image{};
    image.data = base;
    image.size = nt->OptionalHeader.SizeOfImage;
    image.base = reinterpret_cast<std::uintptr_t>(base);
    return patch_fit(image, reinterpret_cast<std::uintptr_t>(entry), bytes);
}

Result resolve(const Image &image, std::uintptr_t method_entry, std::size_t max_sites) {
    Result result{};
    Module module;
    if (!module.open(image)) {
        result.diagnostic = "the module headers could not be read";
        return result;
    }
    if (!module.contains(method_entry)) {
        result.diagnostic = "the method entry point is outside the module";
        return result;
    }

    const Function *entry_fragment = module.fragment(method_entry);
    const std::uintptr_t body_end =
        entry_fragment ? std::min(entry_fragment->end, method_entry + max_body_bytes) : method_entry + max_body_bytes;
    std::vector<X86::Instruction> body = decode_run(module, method_entry, body_end, max_body_instructions);
    if (!entry_fragment) {
        // Without unwind data the body has no recorded end; stop where control
        // provably leaves it rather than decoding into the next method.
        for (std::size_t index = 0; index < body.size(); ++index) {
            if (body[index].is_return || (body[index].is_unconditional_jump && !body[index].is_call)) {
                body.resize(index + 1);
                break;
            }
        }
    }
    if (body.empty()) {
        result.diagnostic = "the method body could not be decoded";
        return result;
    }
    const std::size_t entry_index = body_entry_index(body);
    const std::vector<std::uint16_t> holders = instance_alias_masks(body);
    const std::vector<Anchor> anchors = collect_anchors(module, body, method_entry, body_end);

    const ScanResult scanned = scan(module, method_entry, anchors);
    result.scanned = true;
    result.direct_call_sites = scanned.direct_calls;
    result.address_references = scanned.address_refs;
    result.call_site_addresses = scanned.call_sites;
    if (anchors.empty()) {
        result.diagnostic = "the method body references nothing that identifies a copy of it";
        return result;
    }

    std::unordered_set<std::uintptr_t> seen_sites;
    std::vector<X86::Instruction> caller;
    // Candidates arrive in address order, so one decode serves a whole function.
    std::uintptr_t decoded_start = 0;
    for (const Candidate &candidate : scanned.candidates) {
        if (result.sites.size() >= max_sites)
            break;
        // The template itself is not a copy.
        if (candidate.displacement_address >= method_entry && candidate.displacement_address < body_end)
            continue;
        const Function *fragment = module.fragment(candidate.displacement_address);
        if (!fragment)
            continue;
        ++result.rejected.candidates;
        if (fragment->start != decoded_start) {
            caller = decode_run(module, fragment->start, std::min(fragment->end, fragment->start + max_function_bytes),
                                max_function_instructions);
            decoded_start = fragment->start;
        }
        if (caller.empty())
            continue;

        // Find the instruction that owns the displacement the scan found, which
        // also proves the byte pattern was a real instruction field.
        std::size_t caller_index = caller.size();
        for (std::size_t index = 0; index < caller.size(); ++index) {
            const X86::Instruction &instruction = caller[index];
            const bool references =
                (instruction.rip_relative && instruction.rip_target == candidate.target) ||
                (instruction.is_relative_branch && instruction.branch_target == candidate.target);
            if (!references)
                continue;
            const std::uintptr_t field = instruction.rip_relative
                                             ? instruction.address + instruction.displacement_offset
                                             : instruction.address + instruction.length - 4;
            if (field == candidate.displacement_address) {
                caller_index = index;
                break;
            }
        }
        if (caller_index == caller.size()) {
            ++result.rejected.not_an_instruction;
            continue;
        }

        // Which body instruction shares this anchor.
        std::size_t body_index = body.size();
        for (const Anchor &anchor : anchors) {
            if (anchor.target != candidate.target)
                continue;
            if (X86::same_shape(body[anchor.body_index], caller[caller_index])) {
                body_index = anchor.body_index;
                break;
            }
        }
        if (body_index == body.size()) {
            ++result.rejected.shape_mismatch;
            continue;
        }

        const Alignment alignment = align(body, body_index, caller, caller_index, holders);
        const std::size_t body_instructions = body.size() - std::min(entry_index, body.size());
        const std::size_t required =
            std::max(min_matched_instructions, std::min(body_instructions, strong_match_instructions));
        if (!alignment.valid || alignment.matched < min_matched_instructions || alignment.body_span < required) {
            ++result.rejected.too_short;
            continue;
        }
        // The run has to reach the body's first real instruction. Otherwise it
        // is a shared fragment - typically another method this one also inlines
        // - and hooking it would report calls that never happened.
        if (alignment.body_start > entry_index) {
            ++result.rejected.not_body_entry;
            continue;
        }
        const std::uintptr_t site = caller[alignment.caller_start].address;
        if (site >= method_entry && site < body_end)
            continue;
        if (!seen_sites.insert(site).second)
            continue;
        std::size_t patch_bytes = 0;
        if (!patch_window_is_safe(module, caller, alignment.caller_start, fragment->end, patch_bytes)) {
            ++result.rejected.unsafe_site;
            continue;
        }

        Site entry{};
        entry.address = site;
        entry.function_start = module.primary_start(*fragment);
        entry.matched_instructions = alignment.matched;
        entry.patch_bytes = patch_bytes;
        entry.instance_register = alignment.instance_register;
        result.sites.push_back(entry);
    }
    std::sort(result.sites.begin(), result.sites.end(), [](const Site &left, const Site &right) {
        return left.matched_instructions > right.matched_instructions;
    });
    return result;
}

Result resolve_in_process(const void *method_entry, std::size_t max_sites) {
    Result result{};
    if (!method_entry) {
        result.diagnostic = "no entry point";
        return result;
    }
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(method_entry), &module) ||
        !module) {
        result.diagnostic = "the owning module could not be identified";
        return result;
    }
    const auto *base = reinterpret_cast<const std::uint8_t *>(module);
    const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        result.diagnostic = "the owning module is not a PE image";
        return result;
    }
    const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS64 *>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
        result.diagnostic = "the owning module is not a 64-bit PE image";
        return result;
    }
    Image image{};
    image.data = base;
    image.size = nt->OptionalHeader.SizeOfImage;
    image.base = reinterpret_cast<std::uintptr_t>(base);
    return resolve(image, reinterpret_cast<std::uintptr_t>(method_entry), max_sites);
}

} // namespace Explorer::InlineSites

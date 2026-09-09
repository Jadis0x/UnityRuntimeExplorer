// Copyright (c) 2026 Jadis0x. All rights reserved.
// Finds the places an AOT compiler copied a method body into.
//
// IL2CPP compiles ahead of time and the C++ compiler inlines aggressively, so a
// small managed method usually keeps a native entry point that nothing branches
// to: the real code runs inside its callers. A hook on that entry point
// installs and never fires, which is why tracing such a method used to show
// nothing while the game called it constantly.
//
// The body that survives at the entry point is the template. Its references to
// module data and to other functions are absolute addresses that inlining
// copies verbatim, so they act as fingerprints: find code elsewhere that
// references the same address, decode the function around it, and check whether
// the instructions there have the same shape as the body. A run that covers the
// body's first instruction is an inlined copy, and its first instruction is a
// place a hook fires exactly when the method is entered.
#pragma once

#include "x86_decode.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Explorer::InlineSites {

// A PE image to search, addressed the way the loader maps it: byte `data[n]`
// lives at address `base + n`.
struct Image {
    const std::uint8_t *data = nullptr;
    std::size_t size = 0;
    std::uintptr_t base = 0;
};

struct Site {
    // Instruction boundary to hook: the inlined body's first instruction.
    std::uintptr_t address = 0;
    // Entry point of the function that carries the copy.
    std::uintptr_t function_start = 0;
    // How many instructions matched; higher is stronger evidence.
    std::size_t matched_instructions = 0;
    // Bytes a hook here displaces, so two hooks cannot overlap.
    std::size_t patch_bytes = 0;
    // Register holding `this` where the copy starts, when the match proves it.
    std::uint8_t instance_register = X86::no_register;
};

struct Result {
    bool scanned = false;
    // Branches that land on the entry point itself. Zero means the entry is
    // dead code and only the inlined copies ever run.
    std::size_t direct_call_sites = 0;
    // The entry address stored as data: metadata tables, vtable slots.
    std::size_t address_references = 0;
    std::vector<Site> sites;
    // Where the direct calls are. A method too small to host a hook of its own
    // can still be watched from the instructions that call it.
    std::vector<std::uintptr_t> call_site_addresses;
    // Why candidate references did not become sites. Reading these is how the
    // matching rules get tuned against a real binary instead of by guesswork.
    struct Rejections {
        std::size_t candidates = 0;
        std::size_t not_an_instruction = 0;
        std::size_t shape_mismatch = 0;
        std::size_t too_short = 0;
        std::size_t not_body_entry = 0;
        std::size_t unsafe_site = 0;
    };
    Rejections rejected;
    std::string diagnostic;
};

// Whether a hook can be written at an address without touching what follows it.
// IL2CPP emits property getters as three to eight bytes of code padded out to
// the next method; a five-byte branch written over one of those runs into its
// neighbour, and the neighbour crashes the next time it is called.
struct PatchFit {
    bool decoded = false;
    // Bytes the hook displaces: whole instructions covering the branch.
    std::size_t patch_bytes = 0;
    // Bytes that provably belong to this method.
    std::size_t function_bytes = 0;
    bool safe = false;
};

PatchFit patch_fit(const Image &image, std::uintptr_t entry, std::size_t patch_bytes);
PatchFit patch_fit_in_process(const void *entry, std::size_t patch_bytes);

// max_sites caps both the work and the number of hooks the caller will install.
Result resolve(const Image &image, std::uintptr_t method_entry, std::size_t max_sites);

// Same, for the module that owns `method_entry` in this process.
Result resolve_in_process(const void *method_entry, std::size_t max_sites);

} // namespace Explorer::InlineSites

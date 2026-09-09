// Copyright (c) 2026 Jadis0x. All rights reserved.
// Resolves inlined copies inside a PE image rebuilt from a real IL2CPP build.
#include "mod/explorer/inline_sites.h"
#include "tests/inline_site_fixture.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <iomanip>
#include <iostream>
#include <vector>

namespace {

void require(bool condition, const char *message) {
    if (condition)
        return;
    std::cerr << "FAILED: " << message << '\n';
    std::exit(1);
}

// Room reserved past the captured bytes for synthetic methods. The gap clears
// the last captured function, whose unwind data reaches past the bytes.
constexpr std::size_t scratch_gap = 0x200;
constexpr std::size_t scratch_bytes = scratch_gap + 128;
constexpr std::uintptr_t scratch_address = InlineSiteFixture::image_base + InlineSiteFixture::code_rva +
                                           sizeof(InlineSiteFixture::code) + scratch_gap;

struct FixtureImage {
    std::vector<std::uint8_t> storage;
    Explorer::InlineSites::Image image{};
};

void add_section(IMAGE_SECTION_HEADER &header, const char *name, std::uint32_t rva, std::uint32_t size,
                 bool executable) {
    std::memset(&header, 0, sizeof(header));
    std::memcpy(header.Name, name, std::min<std::size_t>(std::strlen(name), 8));
    header.VirtualAddress = rva;
    header.Misc.VirtualSize = size;
    header.SizeOfRawData = size;
    header.PointerToRawData = rva;
    header.Characteristics = executable ? (IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ | IMAGE_SCN_CNT_CODE)
                                        : (IMAGE_SCN_MEM_READ | IMAGE_SCN_CNT_INITIALIZED_DATA);
}

// Lays the captured bytes out the way the loader would map the module, so the
// resolver sees the same rip-relative targets and unwind data the game does.
FixtureImage build_image() {
    FixtureImage out{};
    out.storage.assign(InlineSiteFixture::image_size, 0);
    std::uint8_t *base = out.storage.data();

    auto *dos = reinterpret_cast<IMAGE_DOS_HEADER *>(base);
    dos->e_magic = IMAGE_DOS_SIGNATURE;
    dos->e_lfanew = 0x100;

    auto *nt = reinterpret_cast<IMAGE_NT_HEADERS64 *>(base + dos->e_lfanew);
    nt->Signature = IMAGE_NT_SIGNATURE;
    nt->FileHeader.Machine = IMAGE_FILE_MACHINE_AMD64;
    nt->FileHeader.NumberOfSections = 3;
    nt->FileHeader.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER64);
    nt->OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR64_MAGIC;
    nt->OptionalHeader.ImageBase = InlineSiteFixture::image_base;
    nt->OptionalHeader.SizeOfImage = static_cast<DWORD>(InlineSiteFixture::image_size);
    nt->OptionalHeader.NumberOfRvaAndSizes = IMAGE_NUMBEROF_DIRECTORY_ENTRIES;
    nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION].VirtualAddress = InlineSiteFixture::pdata_rva;
    nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION].Size =
        static_cast<DWORD>(sizeof(InlineSiteFixture::pdata));

    IMAGE_SECTION_HEADER *sections = IMAGE_FIRST_SECTION(nt);
    // Extra room after the captured code for the hand-written cases below.
    add_section(sections[0], ".text", InlineSiteFixture::code_rva,
                static_cast<std::uint32_t>(sizeof(InlineSiteFixture::code) + scratch_bytes), true);
    add_section(sections[1], ".rdata", InlineSiteFixture::unwind_rva,
                static_cast<std::uint32_t>(sizeof(InlineSiteFixture::unwind)), false);
    add_section(sections[2], ".pdata", InlineSiteFixture::pdata_rva,
                static_cast<std::uint32_t>(sizeof(InlineSiteFixture::pdata)), false);

    std::memcpy(base + InlineSiteFixture::code_rva, InlineSiteFixture::code, sizeof(InlineSiteFixture::code));
    std::memcpy(base + InlineSiteFixture::unwind_rva, InlineSiteFixture::unwind, sizeof(InlineSiteFixture::unwind));
    std::memcpy(base + InlineSiteFixture::pdata_rva, InlineSiteFixture::pdata, sizeof(InlineSiteFixture::pdata));

    out.image.data = out.storage.data();
    out.image.size = out.storage.size();
    out.image.base = InlineSiteFixture::image_base;
    return out;
}

const Explorer::InlineSites::Site *site_in(const Explorer::InlineSites::Result &result, std::uintptr_t function) {
    const auto found = std::find_if(result.sites.begin(), result.sites.end(),
                                    [function](const Explorer::InlineSites::Site &site) {
                                        return site.function_start == function;
                                    });
    return found == result.sites.end() ? nullptr : &*found;
}

void report(const char *name, const Explorer::InlineSites::Result &result) {
    std::cout << name << ": call sites=" << result.direct_call_sites << " address refs=" << result.address_references
              << " inlined copies=" << result.sites.size();
    for (const Explorer::InlineSites::Site &site : result.sites) {
        std::cout << "\n    site 0x" << std::hex << site.address << " in function 0x" << site.function_start
                  << std::dec << " (" << site.matched_instructions << " instructions, this in register "
                  << static_cast<int>(static_cast<std::int8_t>(site.instance_register)) << ")";
    }
    std::cout << '\n';
}

} // namespace

int main() {
    FixtureImage fixture = build_image();
    using Explorer::InlineSites::resolve;

    // SpawnTarget survives as a real call from Update: nothing to resolve.
    const auto spawn = resolve(fixture.image, InlineSiteFixture::spawn_target, 8);
    report("SimpleHookTest.SpawnTarget", spawn);
    require(spawn.scanned, "the fixture image must be scannable");
    require(spawn.direct_call_sites == 1, "SpawnTarget keeps the call site inside Update");

    // OnTargetMissed is called from TargetInfo.OnDestroy, which carries the
    // only copy of the body that ever runs.
    const auto missed = resolve(fixture.image, InlineSiteFixture::on_target_missed, 8);
    report("SimpleHookTest.OnTargetMissed", missed);
    require(missed.direct_call_sites == 0, "OnTargetMissed has no reachable entry point");
    const Explorer::InlineSites::Site *in_destroy = site_in(missed, InlineSiteFixture::on_destroy);
    require(in_destroy != nullptr, "OnTargetMissed must resolve to a copy inside TargetInfo.OnDestroy");
    require(in_destroy->address == 0x180240BD4ull, "the copy starts at the guard the method opens with");
    require(in_destroy->matched_instructions >= 4, "the match must cover the body, not a single instruction");
    // The standalone body reaches fields through rcx; the copy uses rbx.
    require(in_destroy->instance_register == 3, "the instance register must be recovered from the copy");

    // TriggerGameOver is inlined one level deeper, into the copy above.
    const auto game_over = resolve(fixture.image, InlineSiteFixture::trigger_game_over, 8);
    report("SimpleHookTest.TriggerGameOver", game_over);
    require(game_over.direct_call_sites == 0, "TriggerGameOver has no reachable entry point");
    const Explorer::InlineSites::Site *game_over_site = site_in(game_over, InlineSiteFixture::on_destroy);
    require(game_over_site != nullptr, "TriggerGameOver must resolve into TargetInfo.OnDestroy");
    require(game_over_site->address == 0x180240BE8ull, "the copy starts where the game-over branch begins");
    // Its own body parks `this` in rbx, and so does the copy.
    require(game_over_site->instance_register == 3, "the instance register follows the body, not the ABI");

    // RestartGame is inlined into the OnGUI button handler.
    const auto restart = resolve(fixture.image, InlineSiteFixture::restart_game, 8);
    report("SimpleHookTest.RestartGame", restart);
    require(restart.direct_call_sites == 0, "RestartGame has no reachable entry point");
    const Explorer::InlineSites::Site *in_gui = site_in(restart, InlineSiteFixture::on_gui);
    require(in_gui != nullptr, "RestartGame must resolve to a copy inside OnGUI");
    require(in_gui->address == 0x1802402CFull, "the copy starts where the body's metadata guard was placed");

    // A resolved site must never point back at the template body: hooking that
    // would record nothing, which is the bug this resolver exists to fix.
    for (const auto *result : {&missed, &game_over, &restart}) {
        for (const Explorer::InlineSites::Site &site : result->sites)
            require(site.address != site.function_start || site.function_start != InlineSiteFixture::on_target_missed,
                    "a site must not be the dead entry point itself");
    }

    // A hook writes five bytes over whole instructions. IL2CPP pads a tiny
    // method out to its neighbour, so the question is not whether there is room
    // in the padding but whether there is room in the method.
    const auto write_scratch = [&fixture](std::size_t offset, std::initializer_list<std::uint8_t> bytes) {
        std::uint8_t *code = fixture.storage.data() + InlineSiteFixture::code_rva +
                             sizeof(InlineSiteFixture::code) + scratch_gap + offset;
        std::memset(code, 0xCC, 32);
        std::size_t index = 0;
        for (const std::uint8_t value : bytes)
            code[index++] = value;
    };
    using Explorer::InlineSites::patch_fit;

    // xor eax, eax / ret: three bytes, then another method starts.
    write_scratch(0, {0x31, 0xC0, 0xC3});
    const auto tiny = patch_fit(fixture.image, scratch_address, 5);
    require(tiny.decoded, "a tiny method still decodes");
    require(tiny.function_bytes == 3, "the method ends at its return, not in the padding after it");
    require(!tiny.safe, "a three-byte method cannot host a five-byte branch");

    // movzx eax, byte [rcx+0x834] / ret: the shape of a real IL2CPP bool getter.
    write_scratch(32, {0x0F, 0xB6, 0x81, 0x34, 0x08, 0x00, 0x00, 0xC3});
    const auto getter = patch_fit(fixture.image, scratch_address + 32, 5);
    require(getter.decoded && getter.patch_bytes == 7, "the branch displaces whole instructions");
    require(getter.function_bytes == 8 && getter.safe, "seven bytes of hook fit inside an eight-byte method");

    // mov byte [rcx+0x83C], 1 / ret: the smallest setter that is still safe.
    write_scratch(64, {0xC6, 0x81, 0x3C, 0x08, 0x00, 0x00, 0x01, 0xC3});
    require(patch_fit(fixture.image, scratch_address + 64, 5).safe, "an eight-byte setter is hookable");

    // A method the fixture carries with unwind data: bounded by .pdata.
    const auto real_method = patch_fit(fixture.image, InlineSiteFixture::on_destroy, 5);
    require(real_method.decoded && real_method.safe, "an ordinary method is hookable");

    std::cout << "inline site resolution contract passed\n";
    return 0;
}

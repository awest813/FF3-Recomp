#include "psprecomp/elf32.hpp"
#include "psprecomp/program_analysis.hpp"

#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void check(bool value, const char *message) {
    if (!value) throw std::runtime_error(message);
}
void put16(std::vector<std::uint8_t> &bytes, std::size_t offset, std::uint16_t value) {
    bytes.at(offset) = static_cast<std::uint8_t>(value);
    bytes.at(offset + 1u) = static_cast<std::uint8_t>(value >> 8u);
}
void put32(std::vector<std::uint8_t> &bytes, std::size_t offset, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i)
        bytes.at(offset + i) = static_cast<std::uint8_t>(value >> (8u * i));
}
std::vector<std::uint8_t> fixture() {
    std::vector<std::uint8_t> bytes(0x280u);
    bytes[0] = 0x7f; bytes[1] = 'E'; bytes[2] = 'L'; bytes[3] = 'F';
    bytes[4] = 1; bytes[5] = 1; bytes[6] = 1;
    put16(bytes, 16, 0xFFA0); put16(bytes, 18, 8); put32(bytes, 20, 1);
    put32(bytes, 24, 0x1000); put32(bytes, 28, 52); put32(bytes, 32, 0x200);
    put16(bytes, 40, 52); put16(bytes, 42, 32); put16(bytes, 44, 1);
    put16(bytes, 46, 40); put16(bytes, 48, 3);
    // Nonzero file offset and vaddr expose file-offset / guest-address confusion.
    put32(bytes, 52, 1); put32(bytes, 56, 0x100); put32(bytes, 60, 0x1000);
    put32(bytes, 64, 0x140); put32(bytes, 68, 0x100); put32(bytes, 72, 0x100);
    put32(bytes, 76, 7); put32(bytes, 80, 4);
    put32(bytes, 0x100, 0x03e00008); // jr ra
    // A data word looks like a call to the data section. It must not become code.
    put32(bytes, 0x110, 0x0c000000u | (0x08805080u >> 2u));
    bytes[0x144] = 't'; bytes[0x145] = 'e'; bytes[0x146] = 's'; bytes[0x147] = 't';
    // Unnamed executable PROGBITS section, followed by allocated data.
    put32(bytes, 0x22c, 1); put32(bytes, 0x230, 6); put32(bytes, 0x234, 0x1000);
    put32(bytes, 0x238, 0x100); put32(bytes, 0x23c, 8);
    put32(bytes, 0x254, 1); put32(bytes, 0x258, 3); put32(bytes, 0x25c, 0x1008);
    put32(bytes, 0x260, 0x108); put32(bytes, 0x264, 0xf8);
    return bytes;
}

void module_info_and_imports() {
    for (const auto marker : {0u, 0x80000000u}) {
        auto bytes = fixture();
        put32(bytes, 64, 0x140u | marker);
        const auto elf = psprecomp::Elf32Image::from_bytes(bytes);
        psprecomp::GuestMemory memory;
        constexpr std::uint32_t base = 0x08904000u;
        elf.load_into(memory, base);
        // Simulate relocated import pointers; no commercial bytes are required.
        const auto info = base + 0x1040u;
        const auto table = base + 0x1080u;
        memory.store32(info + 44u, table); memory.store32(info + 48u, table + 20u);
        memory.store32(table, base + 0x10c0u);
        memory.store8(table + 8u, 5); memory.store16(table + 10u, 1);
        memory.store32(table + 12u, base + 0x10e0u);
        memory.store32(table + 16u, base + 0x1000u);
        memory.store8(base + 0x10c0u, 'x');
        memory.store32(base + 0x10e0u, 0x12345678u);
        const auto module = elf.find_module_info(memory, base);
        check(module && module->name == "test", "Stripped PRX module name was not found");
        check(module->address == info, "Module-info file offset was mapped incorrectly");
        const auto imports = elf.scan_imports(memory, *module);
        check(imports.size() == 1 && imports[0].library == "x" && imports[0].nid == 0x12345678u,
              "Stripped PRX imports were not resolved");
    }
    for (const auto offset : {0u, 0xffu, 0x1e0u, 0x100000u}) {
        auto bytes = fixture(); put32(bytes, 64, offset);
        const auto elf = psprecomp::Elf32Image::from_bytes(bytes);
        psprecomp::GuestMemory memory; elf.load_into(memory);
        check(!elf.find_module_info(memory), "Invalid module-info file offset accepted");
    }
    auto bytes = fixture(); put16(bytes, 16, 2);
    const auto elf = psprecomp::Elf32Image::from_bytes(bytes);
    psprecomp::GuestMemory memory;
    check(!elf.find_module_info(memory), "ET_EXEC p_paddr incorrectly treated as PSP module info");
}

void executable_sections() {
    const auto elf = psprecomp::Elf32Image::from_bytes(fixture());
    psprecomp::GuestMemory memory; elf.load_into(memory);
    const auto program = psprecomp::analyze_program(elf, memory, psprecomp::kDefaultPspUserLoadBase);
    check(program.executable_ranges.size() == 1, "Expected one code section");
    check(program.executable_ranges[0].start == 0x08805000u && program.executable_ranges[0].end == 0x08805008u,
          "RWX segment data was included in code ranges");
    check(!program.seeds.contains(0x08805080u), "Data word was decoded as a function pointer/call");
    check(program.functions.size() == 1, "Synthetic function discovery regressed");
    auto bytes = fixture(); put16(bytes, 48, 0);
    const auto stripped = psprecomp::Elf32Image::from_bytes(bytes);
    const auto fallback = psprecomp::analyze_program(stripped, memory, psprecomp::kDefaultPspUserLoadBase);
    check(fallback.executable_ranges[0].end == 0x08805100u, "Sectionless ELF must fall back to program headers");
    memory.store32(0x08805004u, 0x48000000u); // unsupported COP2 in the jr delay slot
    const auto delay = psprecomp::analyze_program(elf, memory, psprecomp::kDefaultPspUserLoadBase);
    check(delay.functions.front().unsupported_instruction_count == 1u,
          "Unsupported delay-slot instruction was omitted from analysis");
}
void rodata_jump_table() {
    auto bytes=fixture();
    put32(bytes,0x23c,16); // two isolated return blocks
    put32(bytes,0x108,0x03e00008);
    put32(bytes,0x25c,0x1010); put32(bytes,0x260,0x110); put32(bytes,0x264,0xf0);
    put32(bytes,0x258,2); // read-only data in the same RWX segment
    put32(bytes,0x110,0x08805008);
    auto elf=psprecomp::Elf32Image::from_bytes(bytes);
    psprecomp::GuestMemory memory;elf.load_into(memory);
    auto program=psprecomp::analyze_program(elf,memory,psprecomp::kDefaultPspUserLoadBase);
    check(program.seeds.contains(0x08805008),"Read-only jump table target was missed");
    check(program.covered_labels.contains(0x08805008),"Jump table target was not compiled");
    check(!program.covered_labels.contains(0x08805010),"Jump table data was decoded as code");
}
}

int main() {
    try {
        module_info_and_imports(); executable_sections(); rodata_jump_table();
        std::cout << "Stripped PRX regression tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

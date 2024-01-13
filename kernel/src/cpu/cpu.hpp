#pragma once
#include <cstdint>

extern "C" void setGdt(std::uint16_t size, std::uint32_t* base, std::uint16_t codeSegment, std::uint16_t dataSegment);

enum GdtFlags : std::uint8_t {
    UserMode = 3 << 6,
    CodeDataSegment = 1 << 4,
    Executable = 1 << 3,
};

constexpr uint64_t makeGdtEntry(GdtFlags flags) {
    return 0;
}


namespace Register {

struct CR3 {
    static std::uint64_t read() {
        std::uint64_t cr3;

        __asm__ (
            "mov %%cr3, %%rax;"
            "mov %%rax, %0"
        : "=m" (cr3)
        : // No input
        : "%rax"
        );

        return cr3;
    }

    static void flushTLBS() {
        __asm__ __volatile__ (
            "mov %%cr3, %%rax;"
            "mov %%rax, %%cr3"
        : : : "%rax"
        );
    }
};

}

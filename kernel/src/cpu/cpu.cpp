#include "cpu.hpp"

extern "C" void setGdt(std::uint16_t size, std::uint64_t* base, std::uint16_t codeSegment, std::uint16_t dataSegment);

struct GdtAccess {
    using Type = std::uint8_t;
    static constexpr auto ReadableWritable = Type(1) << 1;
    static constexpr auto Executable = Type(1) << 3;
    static constexpr auto CodeDataSegment = Type(1) << 4;
    static constexpr auto UserMode = Type(3) << 5;
    static constexpr auto Present = Type(1) << 7;
};

struct GdtFlags {
    using Type = std::uint8_t;
    static constexpr auto LongCode = Type(1) << 1;
};

    constexpr uint64_t makeGdtEntry(GdtAccess::Type access, GdtFlags::Type flags) {
    return (std::uint64_t(access) << 40) | (std::uint64_t(flags) << 52);
};

Cpu::Cpu() {
    constexpr auto DataSegmentAccess = GdtAccess::CodeDataSegment | GdtAccess::Present | GdtAccess::ReadableWritable;
    constexpr auto CodeSegmentAccess = DataSegmentAccess | GdtAccess::Executable;
    constexpr auto KernelCodeSegment = 1;
    constexpr auto KernelDataSegment = 2;
    
    gdt[0] = 0;
    // Kernel code segment
    gdt[KernelCodeSegment] = makeGdtEntry(CodeSegmentAccess, GdtFlags::LongCode);
    // Kernel data segment
    gdt[KernelDataSegment] = makeGdtEntry(DataSegmentAccess, 0);
    // User code segment
    gdt[3] = makeGdtEntry(GdtAccess::CodeDataSegment| GdtAccess::UserMode, GdtFlags::LongCode);
    // User data segment
    gdt[4] = makeGdtEntry( DataSegmentAccess | GdtAccess::UserMode, 0);

    setGdt(sizeof(gdt), gdt, KernelCodeSegment, KernelDataSegment);
};

std::uint64_t Register::CR3::read() {
    std::uint64_t cr3;

    __asm__ (
        "mov %%cr3, %%rax;"
        "mov %%rax, %0"
    : "=m" (cr3)
    : // No input
    : "%rax"
    );

    return cr3;
};

void Register::CR3::flushTLBS() {
    __asm__ __volatile__ (
        "mov %%cr3, %%rax;"
        "mov %%rax, %%cr3"
    : : : "%rax"
    );
};

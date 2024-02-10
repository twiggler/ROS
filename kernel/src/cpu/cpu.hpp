#pragma once
#include <cstdint>
#include "../memory/allocator.hpp"
#include <bit>

namespace Register {

struct CR3 {
    static std::uint64_t read();

    static void flushTLBS(); 
};

}

enum class GateType : std::uint8_t {
    Interrupt = 0xe,
    Trap = 0xf
};

struct IdtDescriptor {
    std::uint64_t low;
    std::uint64_t high;
};

struct __attribute__((packed)) TaskStateSegment {
    std::uint32_t reserved0;
    std::uint64_t rsp0;
    std::uint64_t rsp1;
    std::uint64_t rsp2;
    std::uint64_t reserved1;
    std::uint64_t ist[7];
    std::uint64_t reserved2;
    std::uint16_t reserved3;
    std::uint16_t iobp;
};

class Cpu {
public:
    static Cpu& makeCpu(Memory::Allocator& allocator);

    static Cpu& getInstance();

private:
    friend class Memory::Allocator;
    Cpu(Memory::Allocator& allocator);
    static Cpu* instance;
    
    void setupGdt(Memory::Allocator& allocator);
    void setupIdt();

    
    static constexpr auto KernelCodeSegmentIndex = std::uint16_t(1);
    static constexpr auto IstIndex = std::uint8_t(1);
    
    uint64_t gdt[7];
    IdtDescriptor idt[256];
    // From the Intel 64 Architectures manual: Volume 3A 
    // "Avoid placing a page boundary in the part of the TSS that the processor reads 
    // during a task switch (the first 104 bytes)."
    alignas(std::bit_ceil(sizeof(TaskStateSegment)))
    TaskStateSegment tss;
};

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
    
    void setupTss(Memory::Allocator& allocator);
    
    void setupGdt();

    static Cpu* instance;
    
    uint64_t gdt[7];
    uint64_t idt[64];

    // From the Intel 64 Architectures manual: Volume 3A 
    // "Avoid placing a page boundary in the part of the TSS that the processor reads 
    // during a task switch (the first 104 bytes)."
    alignas(std::bit_ceil(sizeof(TaskStateSegment))) TaskStateSegment tss;
};

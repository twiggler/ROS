#pragma once

#include "paging.hpp"
#include <libr/allocator.hpp>
#include <libr/stream.hpp>
#include "cpu.hpp"
#include <libr/elf.hpp>


struct LoadProcessResult {
    enum class Code : int {
        OK = 0,
        CannotParseElf = -1,
        CannotCreateAddressSpace = -2,
        InvalidSegmentSize = -3,
        CannotMapProcessMemory = -4,
        CannotCopySegment = -5,
        OutOfMemory = -6
    } result;

    rlib::Elf::ElfParseResult elfParseResult;
    // This is getting akward. Implement an error class with a cause, which is possibly a variant.
    rlib::StreamResult streamResult;
};

class Kernel {
public:
    // Design: Type erase allocator?
    Kernel(std::uint64_t* addressSpace, Memory::PageMapper pageMapper, Cpu& cpu, rlib::BumpAllocator allocator, rlib::InputStream<rlib::MemorySource> initrd, std::uint32_t* framebuffer);

    void run();

private:
    // TODO: Implement type erased InputStream
    LoadProcessResult loadProcess(rlib::InputStream<rlib::MemorySource>& process);    

    std::uint64_t*              addressSpace; 
    Memory::PageMapper          pageMapper;
    Cpu*                        cpu;
    rlib::BumpAllocator         allocator;
    std::uint32_t*              framebuffer;  
};



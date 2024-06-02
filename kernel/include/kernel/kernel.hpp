#pragma once

#include "paging.hpp"
#include <libr/allocator.hpp>
#include <libr/stream.hpp>
#include "cpu.hpp"
#include <libr/elf.hpp>

struct KernelErrorCategory : rlib::ErrorCategory {};
inline constexpr auto kernelErrorCategory = KernelErrorCategory{};

inline constexpr auto CannotParseElf = rlib::Error{-1, &kernelErrorCategory};
inline constexpr auto CannotCreateAddressSpace = rlib::Error{-2, &kernelErrorCategory};
inline constexpr auto InvalidSegmentSize = rlib::Error{-3, &kernelErrorCategory};
inline constexpr auto CannotMapProcessMemory = rlib::Error{-4, &kernelErrorCategory};
inline constexpr auto CannotCopySegment = rlib::Error{-5, &kernelErrorCategory};

class Kernel {
public:
    // Design: Type erase allocator?
    Kernel(Memory::TableView addressSpace, Memory::PageMapper pageMapper, Cpu& cpu, rlib::BumpAllocator allocator, rlib::InputStream<rlib::MemorySource> initrd, std::uint32_t* framebuffer);

    void run();

private:
    // TODO: Implement type erased InputStream
    std::optional<rlib::Error> loadProcess(rlib::InputStream<rlib::MemorySource>& process);    

    Memory::TableView           addressSpace; 
    Memory::PageMapper          pageMapper;
    Cpu*                        cpu;
    rlib::BumpAllocator         allocator;
    std::uint32_t*              framebuffer;  
};



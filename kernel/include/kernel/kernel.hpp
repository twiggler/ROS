#pragma once

#include "paging.hpp"
#include <libr/allocator.hpp>
#include <libr/stream.hpp>
#include "cpu.hpp"

class Kernel {
public:
    Kernel(std::uint64_t* tableLevel4, Memory::PageMapper pageMapper, Cpu& cpu, rlib::BumpAllocator allocator, rlib::InputStream<rlib::MemorySource> initrd);

    void run();

    void loadProcess(rlib::InputStream<rlib::MemorySource>& process);    

private:
    Memory::PageMapper  pageMapper;
    Cpu*                cpu;  
};



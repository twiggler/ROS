#pragma once
#include "memory/paging.hpp"
#include <stream.hpp>
#include "cpu/cpu.hpp"
#include <string>

class Kernel {
public:
    Kernel(std::uint64_t* tableLevel4, Memory::PageMapper pageMapper, Cpu& cpu, rlib::InputStream<rlib::MemorySource> initrd);

    void run();

    void loadProcess(rlib::InputStream<rlib::MemorySource>& process);    

private:
    Memory::PageMapper  pageMapper;
    Cpu*                cpu;  
};



#pragma once
#include "memory/paging.hpp"
#include "cpu/cpu.hpp"

class Kernel {
public:
    Kernel(std::uint64_t* tableLevel4, Memory::PageMapper pageMapper, Cpu& cpu);

    void run();    

private:
    Memory::PageMapper        pageMapper;
    Cpu*                      cpu;  
};


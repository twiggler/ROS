#pragma once
#include "memory/paging.hpp"
#include "cpu/cpu.hpp"

class Kernel {
public:
    Kernel(Memory::PageMapper& pageMapper, Cpu& cpu);

    void run();    

private:
    Memory::PageMapper*         pageMapper;
    Cpu*                        cpu;  
};


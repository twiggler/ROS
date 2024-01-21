#pragma once
#include "memory/memory.hpp"
#include "cpu/cpu.hpp"

class Kernel {
public:
    Kernel(Memory::PageFrameAllocator& frameAllocator, Memory::PageMapper& pageMapper, Cpu& cpu);    

private:
    Memory::PageFrameAllocator* frameAllocator;
    Memory::PageMapper*         pageMapper;
    Cpu*                        cpu;  
};


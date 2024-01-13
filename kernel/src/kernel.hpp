#pragma once
#include "memory/memory.hpp"

class Kernel {
public:
    Kernel(Memory::PageFrameAllocator frameAllocator);    

private:
    void initialize();

    Memory::PageFrameAllocator frameAllocator;
    Memory::PageMapper* pageMapper;
};

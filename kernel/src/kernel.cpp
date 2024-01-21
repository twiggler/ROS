#include "kernel.hpp"

using namespace Memory;

Kernel::Kernel(PageFrameAllocator& frameAllocator, PageMapper& pageMapper, Cpu& cpu) :
    frameAllocator(&frameAllocator),
    pageMapper(&pageMapper),
    cpu(&cpu) { }

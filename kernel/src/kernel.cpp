#include "kernel.hpp"

Kernel::Kernel(Memory::PageFrameAllocator frameAllocator) :
    frameAllocator(std::move(frameAllocator))
{
    initialize();
}

void Kernel::initialize() {

}

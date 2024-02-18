#include "kernel.hpp"

using namespace Memory;

Kernel::Kernel(PageFrameAllocator& frameAllocator, PageMapper& pageMapper, Cpu& cpu) :
    frameAllocator(&frameAllocator),
    pageMapper(&pageMapper),
    cpu(&cpu)
{
    // TODO: Expand stack to 64kb or something.
}

void Kernel::run() {
    HardwareInterrupt interruptBuffer[Cpu::InterruptBufferSize];
    
    while (true) {
        auto end = cpu->consumeInterrupts(interruptBuffer);
        if (end == interruptBuffer) {
            cpu->halt();
            continue;
        } 

        // Process Interrupts.
    }
}

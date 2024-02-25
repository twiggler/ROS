#include "kernel.hpp"

using namespace Memory;

Kernel::Kernel(PageMapper& pageMapper, Cpu& cpu) :
    pageMapper(&pageMapper),
    cpu(&cpu)
{
    cpu.growStack(64_KiB, pageMapper);
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

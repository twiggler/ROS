#include "kernel.hpp"
#include "error/error.hpp"

using namespace Memory;

Kernel::Kernel(PageMapper& pageMapper, Cpu& cpu) :
    pageMapper(&pageMapper),
    cpu(&cpu)
{
    cpu.growStack(64_KiB, pageMapper);
}

void Kernel::run() {
    HardwareInterrupt interruptBuffer[Cpu::InterruptBufferSize];
    cpu->enableInterrupts();

    while (true) {
        auto end = cpu->consumeInterrupts(interruptBuffer);
        if (end == interruptBuffer) {
            cpu->halt();
            continue;
        } 

        for (auto interrupt = interruptBuffer; interrupt != end; interrupt++) {
            if (interrupt->IRQ == 1) {
                panic("Key pressed");
            } 
        }

        // Process Interrupts.
    }
}

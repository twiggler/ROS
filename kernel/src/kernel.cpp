#include "kernel.hpp"
#include "error/error.hpp"

using namespace Memory;

Kernel::Kernel(std::uint64_t* tableLevel4, PageMapper pageMapper, Cpu& cpu) :
    pageMapper(std::move(pageMapper)),
    cpu(&cpu)
{
    cpu.growStack(tableLevel4, 64_KiB, pageMapper);
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

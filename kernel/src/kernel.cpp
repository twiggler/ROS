#include "kernel.hpp"
#include "error/error.hpp"
#include <ustar.hpp>

using namespace Memory;
using namespace rlib;

Kernel::Kernel(std::uint64_t* tableLevel4, PageMapper pageMapper, Cpu& cpu, InputStream<rlib::MemorySource> initrd) :
    pageMapper(std::move(pageMapper)),
    cpu(&cpu)
{
    cpu.growStack(tableLevel4, 64_KiB, pageMapper);
    auto [archive, result] = UStar::lookup(initrd, "kernel.x86_64.elf"_sv);
    if (result.result != UStar::LookupResult::Code::OK) {
        panic("Cannot find kernel");
    }
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

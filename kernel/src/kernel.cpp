#include "kernel/kernel.hpp"
#include <kernel/error.hpp>
#include <libr/ustar.hpp>
#include <libr/elf.hpp>

using namespace Memory;
using namespace rlib;

Kernel::Kernel(std::uint64_t* tableLevel4, PageMapper pageMapper, Cpu& cpu, BumpAllocator allocator, InputStream<rlib::MemorySource> initrd) :
    pageMapper(std::move(pageMapper)),
    cpu(&cpu)
{
    cpu.growStack(tableLevel4, 64_KiB, pageMapper);
    
    auto [elf, lookupResult] = UStar::lookup(initrd, "serial.elf"_sv);
    if (lookupResult != UStar::LookupResult::Code::OK) {
        panic("Cannot find kernel");
    }
    auto [parsedElf, parseResult] = Elf::parseElf(elf, allocator);
    if (parseResult != Elf::ElfParseResult::Code::OK) {
        panic("Cannot parse kernel");
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

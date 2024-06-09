#include "kernel/kernel.hpp"
#include <kernel/panic.hpp>
#include <libr/ustar.hpp>

using namespace Memory;
using namespace rlib;

Kernel::Kernel(TableView addressSpace, PageMapper pageMapper, Cpu& cpu, BumpAllocator allocator, InputStream<rlib::MemorySource> initrd, std::uint32_t* framebuffer) :
    addressSpace(addressSpace),
    pageMapper(std::move(pageMapper)),
    cpu(&cpu),
    allocator(std::move(allocator)),
    framebuffer(framebuffer)
{
    cpu.growStack(addressSpace, 64_KiB, pageMapper);
    
    auto elfStream = UStar::lookup(initrd, "serial.elf"_sv);
    if (!elfStream) {
        panic("Cannot find service");
    }
    loadProcess(*elfStream);
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

        // Remove this when keyboard driver is implemented.
        for (auto interrupt = interruptBuffer; interrupt != end; interrupt++) {
            if (interrupt->IRQ == 1) {
                panic("Key pressed");
            } 
        }

        // Process Interrupts.
    }
}

std::optional<rlib::Error> Kernel::loadProcess(rlib::InputStream<rlib::MemorySource>& elfStream) {
    auto parsedElf = Elf::parseElf(elfStream, allocator);
    if (!parsedElf) {
        return parsedElf.error();
    }

    auto processAddressSpace = AddressSpace::make(pageMapper, allocator);
    if (!processAddressSpace) {
        return processAddressSpace.error();
    }
    
    // Map kernel into process address space.
    processAddressSpace->shallowCopyMapping(addressSpace, VirtualAddress(0xFFFF8000'00000000), VirtualAddress(0xFFFFFFFF'FFFFFFF));

    for (const auto& segment : parsedElf->segments) {
        if (segment.type != Elf::Segment::Type::Load) {
            continue;
        }

        if (segment.memorySize < segment.fileSize) {
            return InvalidSegmentSize;
        }

        auto flags = PageFlags::Present | PageFlags::UserAccessible;
        if (!(segment.flags & Elf::Segment::Flags::Executable)) {
            flags |= PageFlags::NoExecute;
            // Allow writable access only if the segment is not executable and writable.
            if (segment.flags & Elf::Segment::Flags::Writable) {
                flags |= PageFlags::Writable;
            } 
        }
        auto region = processAddressSpace->reserve(segment.virtualAddress, segment.fileSize, flags, PageSize::_4KiB);
        if (region == nullptr) {
            return OutOfPhysicalMemory;
        }

        elfStream.seek(segment.fileOffset);
        auto segmentStreamRange = StreamRange<std::byte, MemorySource>(elfStream) | std::views::take(segment.fileSize);
        // Copy in chunks of 4_KiB so that we only need to map one page in the kernel address space at a time.
        auto pageIndex = std::size_t(0);
        for (auto chunk : segmentStreamRange | std::views::chunk(4_KiB)) {
            auto frame = pageMapper.allocate();
            if (!frame) {
                return frame.error();
            }
            auto destination = reinterpret_cast<std::byte*>(frame->ptr);
            auto copyResult = std::ranges::copy(chunk, destination);
            if (static_cast<std::size_t>(copyResult.out - destination) < segment.fileSize) {
                return CannotCopySegment;
            }
            // Map the region into the process address space.
            auto error = processAddressSpace->mapPage(**region, frame->physicalAddress, pageIndex);
            if (error) {
                return CannotMapProcessMemory;
            }
            pageIndex++;
            // TODO: Allocate more pages and initialize to zero if memorySize > fileSize.
            // Using std::views::repeat along with std::views::concat would be sweet.
            // Unfortunately, concat is not there yet (c++26)
        }
    }

    // Elf file should not put segments here. Perhaps allocate a region dynamically.
    constexpr auto stackSize = 64_KiB;
    constexpr auto stackBottom = 0x8000'0000'0000 - stackSize;
    constexpr auto stackFlags = PageFlags::Present | PageFlags::Writable | PageFlags::UserAccessible | PageFlags::NoExecute;
    auto error = processAddressSpace->allocate(stackBottom, stackSize, stackFlags, PageSize::_4KiB);
    if (error == nullptr) {
        return OutOfPhysicalMemory;
    }

    auto contextId = cpu->createContext(processAddressSpace->pageDirectory(), parsedElf->startAddress, stackBottom + stackSize, reinterpret_cast<std::uintptr_t>(framebuffer));
    cpu->switchContext(contextId);

    return {};
}

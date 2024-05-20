#include "kernel/kernel.hpp"
#include <kernel/error.hpp>
#include <libr/ustar.hpp>

using namespace Memory;
using namespace rlib;

Kernel::Kernel(std::uint64_t* addressSpace, PageMapper pageMapper, Cpu& cpu, BumpAllocator allocator, InputStream<rlib::MemorySource> initrd, std::uint32_t* framebuffer) :
    addressSpace(addressSpace),
    pageMapper(std::move(pageMapper)),
    cpu(&cpu),
    allocator(std::move(allocator)),
    framebuffer(framebuffer)
{
    cpu.growStack(addressSpace, 64_KiB, pageMapper);
    
    auto [elfStream, lookupResult] = UStar::lookup(initrd, "serial.elf"_sv);
    if (lookupResult != UStar::LookupResult::Code::OK) {
        panic("Cannot find service");
    }
    loadProcess(elfStream);
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

LoadProcessResult Kernel::loadProcess(rlib::InputStream<rlib::MemorySource>& elfStream) {
    auto [parsedElf, parseResult] = Elf::parseElf(elfStream, allocator);
    if (parseResult != Elf::ElfParseResult::Code::OK) {
        return LoadProcessResult{ LoadProcessResult::Code::CannotParseElf, parseResult};
    }

    auto processAddressSpace = pageMapper.createAddressSpace();
    if (processAddressSpace.ptr == nullptr) {
        return LoadProcessResult{ LoadProcessResult::Code::CannotCreateAddressSpace, parseResult};
    }
    
    // Map kernel into process address space.
    pageMapper.shallowCopyMapping(processAddressSpace.ptr, addressSpace, VirtualAddress(0xFFFF8000'00000000), VirtualAddress(0xFFFFFFFF'FFFFFFF));

    for (const auto& segment : parsedElf.segments) {
        if (segment.type != Elf::Segment::Type::Load) {
            continue;
        }

        if (segment.memorySize < segment.fileSize) {
            return LoadProcessResult{ LoadProcessResult::Code::InvalidSegmentSize, parseResult};
        }

        auto flags = PageFlags::Present | PageFlags::UserAccessible;
        if (!(segment.flags & Elf::Segment::Flags::Executable)) {
            flags |= PageFlags::NoExecute;
            // Allow writable access only if the segment is not executable and writable.
            if (segment.flags & Elf::Segment::Flags::Writable) {
                flags |= PageFlags::Writable;
            } 
        }
        
        elfStream.seek(segment.fileOffset);
        auto segmentStreamRange = StreamRange<std::byte, MemorySource>(elfStream) | std::views::take(segment.fileSize);
        // Copy in chunks of 4_KiB so that we only need to map one page in the kernel address space at a time.
        for (auto chunk : segmentStreamRange | std::views::chunk(4_KiB)) {
            // TODO: track allocations in the address space so we can free them on destruction. 
            auto region = pageMapper.allocate();
            if (region.ptr == nullptr) {
                return LoadProcessResult{ LoadProcessResult::Code::CannotMapProcessMemory };
            }
            auto destination = reinterpret_cast<std::byte*>(region.ptr);
            auto copyResult = std::ranges::copy(chunk, destination);
            if (static_cast<std::size_t>(copyResult.out - destination) < segment.fileSize) {
                return LoadProcessResult{ LoadProcessResult::Code::CannotCopySegment, parseResult, elfStream.lastReadResult()};
            }
            // Map the region into the process address space.
            auto mapResult = pageMapper.map(processAddressSpace.ptr, segment.virtualAddress, region.physicalAddress, PageSize::_4KiB, flags);
            if (mapResult != MapResult::OK) {
                return LoadProcessResult{ LoadProcessResult::Code::CannotMapProcessMemory };
            }
            // TODO: Allocate more pages and initialize to zero if memorySize > fileSize.
            // Using std::views::repeat along with std::views::concat would be sweet.
            // Unfortunately, concat is not there yet (c++26)
        }
    }

    // Elf file should not put segments here. Perhaps allocate a region dynamically.
    constexpr auto stackSize = 64_KiB;
    constexpr auto stackBottom = 0x8000'0000'0000 - stackSize;
    constexpr auto stackFlags = PageFlags::Present | PageFlags::Writable | PageFlags::UserAccessible | PageFlags::NoExecute;
    auto stackResult = pageMapper.allocateAndMapContiguous(processAddressSpace.ptr, stackBottom, stackFlags, stackSize / 4_KiB);
    if (stackResult != MapResult::OK) {
        return LoadProcessResult{ LoadProcessResult::Code::CannotMapProcessMemory, parseResult};
    }

    auto contextId = cpu->createContext(processAddressSpace.physicalAddress, parsedElf.startAddress, stackBottom + stackSize, reinterpret_cast<std::uintptr_t>(framebuffer));
    cpu->switchContext(contextId);

    return { LoadProcessResult::Code::OK };
}

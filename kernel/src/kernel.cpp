#include "kernel/paging.hpp"
#include "kernel/kernel.hpp"
#include <kernel/panic.hpp>
#include <libr/ustar.hpp>
#include <libr/pointer.hpp>

using namespace rlib;

std::expected<Kernel, Error> Kernel::make(Iterator<Block>& memoryMap, IdentityMapping identityMapping, void* initialHeapStorage, TableView rootPageTable, InputStream<MemorySource> initrd, std::uint32_t* framebuffer) {
    auto pageFrameAllocator = PageFrameAllocator(memoryMap, identityMapping);
    auto initialAllocator = BumpAllocator(initialHeapStorage, IntialHeapSize);
    auto pageMapper = constructRaw<PageMapper>(initialAllocator, identityMapping, std::move(pageFrameAllocator));
    if (pageMapper == nullptr) {
        return std::unexpected(OutOfPhysicalMemory);
    } 

    constexpr auto flags = PageFlags::Present | PageFlags::Writable | PageFlags::NoExecute;
    constexpr auto heapSizeInFrames = std::size_t(4);
    // TODO: Properly calculate when creating kernel address space
    auto heapStart = VirtualAddress(0xFFFF8000'00000000 + 16_GiB);
    auto error = pageMapper->allocateAndMapRange(rootPageTable, heapStart, flags, heapSizeInFrames * 4_KiB);
    if (error) {
        return std::unexpected(*error);
    }
    // Todo abstract this away
    Register::CR3::flushTLBS();
    
    auto stackAllocator = BumpAllocator(heapStart.ptr(), heapSizeInFrames * 4_KiB);
    using AllocatorType = rlib::FallbackAllocator<decltype(stackAllocator), decltype(stackAllocator)>;
    auto allocatorStorage = initialAllocator.allocate(sizeof(AllocatorType), alignof(AllocatorType));
    auto allocator = ::new (allocatorStorage) FallbackAllocator(std::move(initialAllocator), std::move(stackAllocator));
    
    // Stack size should correspond to value in linker script "link.ld".
    auto cpu = Cpu::makeCpu(*allocator, 0xffffffff'ffffffff, 16_KiB);
    if (!cpu) {
        return std::unexpected(cpu.error());
    }
    
    return std::expected<Kernel, Error>(std::in_place , rootPageTable, pageMapper, **cpu, allocator, std::move(initrd), framebuffer);
}


Kernel::Kernel(TableView addressSpace, PageMapper* pageMapper, Cpu& cpu, rlib::Allocator* allocator, InputStream<MemorySource> initrd, std::uint32_t* framebuffer) :
    addressSpace(addressSpace),
    pageMapper(std::move(pageMapper)),
    cpu(&cpu),
    allocator(std::move(allocator)),
    framebuffer(framebuffer)
{
    cpu.growStack(addressSpace, 64_KiB, *pageMapper);
    
    auto elfStream = UStar::lookup(initrd, "serial.elf"_sv);
    if (!elfStream) {
        panic("Cannot find service");
    }
    loadProcess(*elfStream);
}


void Kernel::run() {
    HardwareInterrupt interruptBuffer[Cpu::InterruptBufferSize];
    Message messageBuffer[Cpu::MessageBufferSize];
    cpu->registerObserver(*this);
    cpu->scheduleThread(*service);

    while (true) {
        auto interruptEnd = interrupts.popAll(interruptBuffer);
        // Process Interrupts.
        for (auto interrupt = interruptBuffer; interrupt != interruptEnd; interrupt++) {
             // Remove this when keyboard driver is implemented.
            if (interrupt->IRQ == 1) {
                panic("Key pressed");
            } 
        }

        auto messageEnd = mailbox.popAll(messageBuffer);
        for (auto message = messageBuffer; message != messageEnd; message++) {
            // Every message is a kill-thread message
            cpu->killThread(*allocator, *message->sender);    
        }
    }
}

std::optional<Error> Kernel::loadProcess(InputStream<MemorySource>& elfStream) {
    auto parsedElf = Elf::parseElf(elfStream, *allocator);
    if (!parsedElf) {
        return parsedElf.error();
    }

    auto processAddressSpace = AddressSpace::make(*pageMapper, *allocator);
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
            auto frame = pageMapper->allocate();
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

    auto thread = cpu->createThread(*allocator, std::move(*processAddressSpace), parsedElf->startAddress, 64_KiB, Context::Flags::Type(0));
    if (!thread) {
        panic("Cannot create service thread");
    }
    service = *thread;

    return {};
}

void Kernel::onInterrupt(std::uint8_t Irq) {
    auto result = interrupts.push(HardwareInterrupt{Irq});
    if (!result) {
        panic("Interrupt buffer overflow");
    }
}

void Kernel::onSyscall(Thread* sender) {
    auto result = mailbox.push(Message{sender});
    if (!result) {
        panic("Message buffer overflow");
    }
}

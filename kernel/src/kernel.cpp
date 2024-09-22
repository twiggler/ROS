#include "kernel/kernel.hpp"
#include "kernel/paging.hpp"
#include <kernel/panic.hpp>
#include <libr/ustar.hpp>
#include <libr/pointer.hpp>

using namespace rlib;

Thread::Thread(
    Context                                  context,
    OwningPointer<AddressSpace>              addressSpace,
    OwningPointer<mpmcBoundedQueue<Message>> mailbox,
    Region*                                  ipcBuffer,
    Region*                                  ipcBufferUserMapping
) :
    context(std::move(context)),
    addressSpace(std::move(addressSpace)),
    mailbox(std::move(mailbox)),
    ipcBuffer(ipcBuffer),
    ipcBufferUserMapping(ipcBufferUserMapping)
{}

Thread* Thread::fromContext(Context& context)
{
    static_assert(offsetof(Thread, context) == 0);

    return reinterpret_cast<Thread*>(&context);
}

std::expected<Thread*, Error> Thread::make(
    Allocator&                  allocator,
    OwningPointer<AddressSpace> addressSpace,
    AddressSpace&               kernelAddressSpace,
    std::uint64_t               entryPoint,
    std::uintptr_t              stackTop
)
{
    auto context =
        Context::make(Context::Flags::Type(0), addressSpace->rootTablePhysicalAddress(), entryPoint, stackTop);

    auto mailbox = mpmcBoundedQueue<Message>::make(MessageBufferSize, allocator);
    if (mailbox == nullptr) {
        return std::unexpected(OutOfMemoryError);
    }

    auto ipcBuffer = kernelAddressSpace.allocate(4_KiB, PageFlags::Present | PageFlags::Writable, PageSize::_4KiB);
    if (!ipcBuffer) {
        return std::unexpected(OutOfPhysicalMemory);
    }
    
    auto ipcBufferUserMapping = addressSpace->share(**ipcBuffer, PageFlags::Present | PageFlags::Writable | PageFlags::UserAccessible);
    
    auto threadPtr = constructRaw<Thread>(allocator, std::move(context), std::move(addressSpace), std::move(mailbox), ipcBuffer, ipcBufferUserMapping);
    if (threadPtr == nullptr) {
        return std::unexpected(OutOfPhysicalMemory);
    }

    return threadPtr;
}


std::expected<Kernel, Error>
Kernel::make(MemoryLayout memoryLayout, std::byte* initialHeapStorage, TableView rootPageTable)
{
    // A stable reference to initialAllocator is required for construction the kernel address space (in theory; in practice, the kernel address space is never deallocated).
    // Assume initialHeapStorage is aligned for BumpAllocator.
    auto initialAllocator = ::new (initialHeapStorage)
        BumpAllocator(initialHeapStorage + sizeof(BumpAllocator), IntialHeapSize - sizeof(BumpAllocator));
    auto pageFrameAllocator = PageFrameAllocator::make(
        *memoryLayout.freeMemoryBlocks, memoryLayout.identityMapping, 4_KiB, *initialAllocator
    );
    auto pageMapper =
        constructRaw<PageMapper>(*initialAllocator, memoryLayout.identityMapping, std::move(*pageFrameAllocator));
    if (pageMapper == nullptr) {
        return std::unexpected(OutOfPhysicalMemory);
    }

    auto kernelAddressSpace =
        AddressSpace::make(*pageMapper, *initialAllocator, StartKernelSpace, EndKernelSpace - StartKernelSpace + 1);
    if (!kernelAddressSpace) {
        return std::unexpected(kernelAddressSpace.error());
    }
    auto error = setupKernelAddressSpace(**kernelAddressSpace, rootPageTable, memoryLayout, *pageMapper);
    if (error) {
        return std::unexpected(*error);
    }
    Cpu::setRootPageTable((*kernelAddressSpace)->rootTablePhysicalAddress());

    // map kernel heap
    constexpr auto heapFlags  = PageFlags::Present | PageFlags::Writable | PageFlags::NoExecute;
    auto           heapRegion = (*kernelAddressSpace)->allocate(KernelHeapSize, heapFlags, PageSize::_4KiB);
    if (!heapRegion) {
        return std::unexpected(heapRegion.error());
    }

    // Wrap construction of allocator to infer its type
    auto makeFallbackAllocator = [=](void* storage) mutable -> auto {
        auto stackAllocator = BumpAllocator((*heapRegion)->start().ptr(), (*heapRegion)->size());
        return ::new (storage) FallbackAllocator(RefAllocator(*initialAllocator), std::move(stackAllocator));
    };
    using AllocatorType   = std::remove_pointer_t<decltype(makeFallbackAllocator(std::declval<void*>()))>;
    auto allocatorStorage = initialAllocator->allocate(sizeof(AllocatorType), alignof(AllocatorType));
    if (allocatorStorage == nullptr) {
        return std::unexpected(OutOfPhysicalMemory);
    }
    auto allocator = makeFallbackAllocator(allocatorStorage);

    auto mailbox = mpmcBoundedQueue<Message>::make(Thread::MessageBufferSize, *allocator);
    if (mailbox == nullptr) {
        return std::unexpected(OutOfMemoryError);
    }
    auto kernelThread = constructRaw<Thread>(*allocator, Context{}, std::move(*kernelAddressSpace), std::move(mailbox));
    if (kernelThread == nullptr) {
        return std::unexpected(OutOfPhysicalMemory);
    }

    auto cpu = Cpu::make(*allocator, kernelThread->context);
    if (!cpu) {
        return std::unexpected(cpu.error());
    }

    auto initrdStart  = memoryLayout.identityMapping.translate(memoryLayout.initrdPhysicalAddress);
    auto memorySource = MemorySource(initrdStart.ptr<std::byte>(), memoryLayout.initrdSize);
    auto inputStream  = InputStream(std::move(memorySource));

    // WORKAROUND: work around undefined reference for construct by upcasting allocator (compiler bug?)
    auto threadList = ThreadList::make(*static_cast<Allocator*>(allocator));
    if (!threadList) {
        return std::unexpected(threadList.error());
    }

    return std::expected<Kernel, Error>(
        std::in_place,
        kernelThread,
        pageMapper,
        **cpu,
        allocator,
        std::move(inputStream),
        std::move(*threadList),
        memoryLayout.framebufferStart
    );
}

std::optional<Error> Kernel::setupKernelAddressSpace(
    AddressSpace& addressSpace, TableView rootPageTable, MemoryLayout memoryLayout, PageMapper& pageMapper
)
{
    // identity map total physical memory
    constexpr auto identityFlags     = PageFlags::Present | PageFlags::Writable | PageFlags::NoExecute;
    auto           identityMapRegion = addressSpace.reserve(
        memoryLayout.identityMapping.translate(0), memoryLayout.totalPhysicalMemory, identityFlags, PageSize::_1GiB
    );
    if (!identityMapRegion) {
        return identityMapRegion.error();
    }
    for (auto physicalAddress = std::uint64_t(0); physicalAddress < memoryLayout.totalPhysicalMemory;
         physicalAddress += 1_GiB) {
        auto error = (*identityMapRegion)->mapPage(physicalAddress, physicalAddress / 1_GiB);
        if (error) {
            return *error;
        }
    }

    // map kernel code and read-only data
    constexpr auto kernelCodeFlags  = PageFlags::Present;
    auto           kernelCodeRegion = addressSpace.reserve(
        memoryLayout.kernelCodeStart,
        memoryLayout.kernelWritableDataStart - memoryLayout.kernelCodeStart,
        kernelCodeFlags,
        PageSize::_4KiB
    );
    if (!kernelCodeRegion) {
        return kernelCodeRegion.error();
    }
    for (auto frame = std::size_t(0); frame < (*kernelCodeRegion)->sizeInFrames(); frame++) {
        auto physicalKernelCodePageEntry = pageMapper.read(rootPageTable, memoryLayout.kernelCodeStart + frame * 4_KiB);
        if (!physicalKernelCodePageEntry) {
            return UnexpectedMemoryLayout;
        }
        (*kernelCodeRegion)->mapPage(*physicalKernelCodePageEntry, frame);
    }

    // map kernel data as non-executable
    constexpr auto kernelDataFlags  = PageFlags::Present | PageFlags::Writable | PageFlags::NoExecute;
    auto           kernelDataRegion = addressSpace.reserve(
        memoryLayout.kernelWritableDataStart,
        memoryLayout.kernelWritableDataEnd - memoryLayout.kernelWritableDataStart,
        kernelDataFlags,
        PageSize::_4KiB
    );
    if (!kernelDataRegion) {
        return kernelDataRegion.error();
    }
    for (auto frame = std::size_t(0); frame < (*kernelDataRegion)->sizeInFrames(); frame++) {
        auto physicalKernelDataPageEntry =
            pageMapper.read(rootPageTable, memoryLayout.kernelWritableDataStart + frame * 4_KiB);
        if (!physicalKernelDataPageEntry) {
            return UnexpectedMemoryLayout;
        }
        (*kernelDataRegion)->mapPage(*physicalKernelDataPageEntry, frame);
    }

    // map kernel stack
    auto kernelStackBottom = VirtualAddress(0 - KernelStackSize);
    auto kernelStackRegion = addressSpace.reserve(kernelStackBottom, KernelStackSize, kernelDataFlags, PageSize::_4KiB);
    if (!kernelStackRegion) {
        return kernelStackRegion.error();
    }

    auto frame = std::size_t(0);
    for (; frame < (KernelStackSize - memoryLayout.initialKernelStackSize) / 4_KiB; frame++) {
        auto error = (*kernelStackRegion)->allocatePage(frame);
        if (error) {
            return *error;
        }
    }
    for (; frame < (*kernelStackRegion)->sizeInFrames(); frame++) {
        auto physicalKernelStackPageEntry = pageMapper.read(rootPageTable, kernelStackBottom + frame * 4_KiB);
        if (!physicalKernelStackPageEntry) {
            return UnexpectedMemoryLayout;
        }
        (*kernelStackRegion)->mapPage(*physicalKernelStackPageEntry, frame);
    }

    // map framebuffer
    constexpr auto framebufferFlags  = PageFlags::Present | PageFlags::Writable | PageFlags::NoExecute;
    auto           framebufferRegion = addressSpace.reserve(
        memoryLayout.framebufferStart, memoryLayout.framebufferSize, framebufferFlags, PageSize::_2MiB
    );
    if (!framebufferRegion) {
        return framebufferRegion.error();
    }
    for (auto frame = std::size_t(0); frame < (*framebufferRegion)->sizeInFrames(); frame++) {
        auto physicalFramebufferPageEntry =
            pageMapper.read(rootPageTable, memoryLayout.framebufferStart + frame * 2_MiB);
        if (!physicalFramebufferPageEntry) {
            return UnexpectedMemoryLayout;
        }
        (*framebufferRegion)->mapPage(*physicalFramebufferPageEntry, frame);
    }

    return {};
}


Kernel::Kernel(
    Thread*                   kernelThread,
    PageMapper*               pageMapper,
    Cpu&                      cpu,
    Allocator*                allocator,
    InputStream<MemorySource> initrd,
    ThreadList                threads,
    std::uint32_t*            framebuffer
) :
    pageMapper(pageMapper), cpu(&cpu), allocator(allocator), threads(std::move(threads)), framebuffer(framebuffer)
{
    this->threads.pushFront(*kernelThread);

    auto elfStream = UStar::lookup(initrd, "serial.elf"_sv);
    if (!elfStream) {
        panic("Cannot find service");
    }
    auto serviceThread = loadProcess(*elfStream);
    if (!serviceThread) {
        panic("Cannot load service");
    }
    service = *serviceThread;
}


void Kernel::run()
{
    HardwareInterrupt interruptBuffer[InterruptBufferSize];
    cpu->registerObserver(*this);
    scheduleThread(*service);

    while (true) {
        auto interruptEnd = interrupts.dequeueAll(interruptBuffer);
        // Process Interrupts.
        for (auto interrupt = interruptBuffer; interrupt != interruptEnd; interrupt++) {
            // Remove this when keyboard driver is implemented.
            if (interrupt->IRQ == 1) {
                panic("Key pressed");
            }
        }

        std::optional<Message> message;
        while ((message = kernelThread()->mailbox->dequeue())) {
            // Every message is a kill-thread message
            killThread(*allocator, message->senderId);
        }
    }
}

std::expected<Thread*, Error>
Kernel::createThread(OwningPointer<AddressSpace> addressSpace, std::uint64_t entryPoint, std::uintptr_t stackTop)
{
    auto thread = Thread::make(*allocator, std::move(addressSpace), *kernelThread()->addressSpace, entryPoint, stackTop);
    if (!thread) {
        return std::unexpected(thread.error());
    }

    threads.pushFront(**thread);

    return thread;
}

void Kernel::killThread(Allocator& allocator, Thread& thread)
{
    threads.remove(thread);
    destruct(&thread, allocator);
}

void Kernel::scheduleThread(Thread& thread)
{
    cpu->scheduleContext(thread.context);
}

std::expected<Thread*, Error> Kernel::loadProcess(InputStream<MemorySource>& elfStream)
{
    auto parsedElf = Elf::parseElf(elfStream, *allocator);
    if (!parsedElf) {
        return std::unexpected(parsedElf.error());
    }

    auto processAddressSpace =
        AddressSpace::make(*pageMapper, *allocator, StartUserSpace, EndUserSpace - StartUserSpace + 1);
    if (!processAddressSpace) {
        return std::unexpected(processAddressSpace.error());
    }

    // Map kernel into process address space.
    // This leaves the kernel open to Meltdown and Spectre attacks, especially since the kernel identity maps all physical memory.
    // Implement KPTI to fix this.
    // Alternatively, mininmize the amount of kernel code and data that is mapped into the process address space.
    (*processAddressSpace)
        ->shallowCopyRootMapping(
            kernelThread()->addressSpace, VirtualAddress(0xFFFF8000'00000000), VirtualAddress(0xFFFFFFFF'FFFFFFF)
        );

    for (const auto& segment : parsedElf->segments) {
        if (segment.type != Elf::Segment::Type::Load) {
            continue;
        }

        if (segment.memorySize < segment.fileSize) {
            return std::unexpected(InvalidSegmentSize);
        }

        auto flags = PageFlags::Present | PageFlags::UserAccessible;
        if (!(segment.flags & Elf::Segment::Flags::Executable)) {
            flags |= PageFlags::NoExecute;
            // Allow writable access only if the segment is not executable and writable.
            if (segment.flags & Elf::Segment::Flags::Writable) {
                flags |= PageFlags::Writable;
            }
        }
        auto region = (*processAddressSpace)->reserve(segment.virtualAddress, segment.fileSize, flags, PageSize::_4KiB);
        if (region == nullptr) {
            return std::unexpected(OutOfPhysicalMemory);
        }

        elfStream.seek(segment.fileOffset);
        auto segmentStreamRange = StreamRange<std::byte, MemorySource>(elfStream) | std::views::take(segment.fileSize);
        // Copy in chunks of 4_KiB so that we only need to map one page in the kernel address space at a time.
        auto pageIndex = std::size_t(0);
        for (auto chunk : segmentStreamRange | std::views::chunk(4_KiB)) {
            auto frame = pageMapper->allocate();
            if (!frame) {
                return std::unexpected(frame.error());
            }
            auto destination = reinterpret_cast<std::byte*>(frame->ptr);
            auto copyResult  = std::ranges::copy(chunk, destination);
            if (static_cast<std::size_t>(copyResult.out - destination) < segment.fileSize) {
                return std::unexpected(CannotCopySegment);
            }
            // Map the region into the process address space.
            auto error = (*region)->mapPage(frame->physicalAddress, pageIndex);
            if (error) {
                return std::unexpected(CannotMapProcessMemory);
            }
            pageIndex++;
            // TODO: Allocate more pages and initialize to zero if memorySize > fileSize.
            // Using std::views::repeat along with std::views::concat would be sweet.
            // Unfortunately, concat is not there yet (c++26)
        }
    }

    auto stackFlags = PageFlags::Present | PageFlags::Writable | PageFlags::UserAccessible | PageFlags::NoExecute;
    auto stack      = (*processAddressSpace)->allocate(64_KiB, stackFlags, PageSize::_4KiB);
    if (!stack) {
        return std::unexpected(stack.error());
    }

    auto ipcBuffer = kernelThread()->addressSpace->allocate(4_KiB, stackFlags, PageSize::_4KiB);
    if (!ipcBuffer) {
        return std::unexpected(ipcBuffer.error());
    }

    auto thread = createThread(std::move(*processAddressSpace), parsedElf->startAddress, (*stack)->end());
    if (!thread) {
        return std::unexpected(thread.error());
    }

    return thread;
}

void Kernel::onInterrupt(std::uint8_t Irq)
{
    auto result = interrupts.enqueue(HardwareInterrupt{Irq});
    if (!result) {
        panic("Interrupt buffer overflow");
    }
}

Context& Kernel::onSyscall(Context& sender)
{
    auto origin = Thread::fromContext(sender);
    auto result = mailbox->enqueue(Message{origin});
    if (!result) {
        panic("Message buffer overflow");
    }

    return kernelThread()->context;
}

Thread* Kernel::kernelThread() const
{
    return threads.back();
}

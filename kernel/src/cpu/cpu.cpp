#include <kernel/cpu.hpp>
#include <kernel/panic.hpp>
#include <tuple>
#include <kernel/cpu.hpp>
#include <libr/allocator.hpp>

extern "C" void setGdt(std::uint16_t size, std::uint64_t* base, std::uint16_t codeSegmentIndex, std::uint16_t tssSegmentIndex);

extern "C" void setIdt(std::uint16_t size, IdtDescriptor* base);

extern "C" void initializePIC(std::uint8_t masterVectorOffset, std::uint8_t slaveMasterOffset);

extern "C" bool notifyEndOfInterrupt(std::uint8_t IRQ);

extern "C" void setupSyscallHandler(std::uint16_t kernelCodeSegmentIndex, std::uint16_t userCodeSegmentIndex, Core* core);

extern "C" void switchContext(Context* context);

struct GdtAccess {
    using Type = std::uint8_t;
    static constexpr auto ReadableWritable = Type(1) << 1;
    static constexpr auto Executable = Type(1) << 3;
    static constexpr auto CodeDataSegment = Type(1) << 4;
    static constexpr auto UserMode = Type(3) << 5;
    static constexpr auto Present = Type(1) << 7;
    static constexpr auto TSS = Type(0x9);
};

struct InterruptFrame {
    std::uint64_t rip;
    std::uint64_t cs;
    std::uint64_t flags;
    std::uint64_t rsp;
    std::uint64_t ss;
};

Thread::Thread(Context context, AddressSpace addressSpace) :
        context(std::move(context)), 
        addressSpace(std::move(addressSpace)) {}

Thread* Thread::fromContext(Context& context) {
    static_assert(offsetof(Thread, context) == 0);

    return reinterpret_cast<Thread*>(&context);
}

constexpr std::uint64_t makeSegmentDescriptor(GdtAccess::Type access) {
    auto entry = std::uint64_t(access) << 40; 
    if (access & GdtAccess::Executable) {
        entry |= std::uint64_t(1) << 53;   // Set long-mode code flag.
    }
    return entry;
};

constexpr std::tuple<std::uint64_t, std::uint64_t> makeTaskStateSegmentDescriptor(std::uintptr_t tssLinearAddress) {
    static_assert(sizeof(TaskStateSegment) - 1 < 0xffff);
    constexpr auto access = GdtAccess::Present | GdtAccess::TSS;
    
    // From the Intel 64 Architectures manual: Volume 1 
    // "If the I/O bit map base address is greater than or equal to the TSS segment limit, there is no I/O permission map,
    // and all I/O instructions generate exceptions when the CPL is greater than the current IOP"
    auto lowerEntry = sizeof(TaskStateSegment) - 1;
    lowerEntry |= (tssLinearAddress & 0xff'ffff) << 16;
    lowerEntry |= std::uint64_t(access) << 40;
    lowerEntry |= (tssLinearAddress & 0xff00'0000) << 32;
       
    auto higherEntry = tssLinearAddress >> 32;
    return {lowerEntry, higherEntry};
}

constexpr IdtDescriptor makeGateDescriptor(std::uintptr_t isrAddress, std::uint16_t codeSegmentIndex, GateType gateType, std::uint8_t istIndex) {
    auto codesegmentSelector = codeSegmentIndex << 3;
    
    auto low = isrAddress & 0xffff;
    low |= std::uint64_t(codesegmentSelector) << 16;
    low |= std::uint64_t(istIndex & 7) << 32;
    low |= (static_cast<std::uint64_t>(gateType) & 0xf) << 40;
    low |= std::uint64_t(1) << 47;
    low |= (isrAddress & 0xffff0000) >> 16 << 48;
    
    auto high = isrAddress >> 32;
    return { low, high};
}


 __attribute__((interrupt)) void doubleFaultHandler(InterruptFrame*, std::uint64_t errorCode) {
    // In response to a double fault we should abort.
    panic("Double fault");
};

template<std::uint8_t Irq>
__attribute__((interrupt)) void hardwareInterruptHandler(InterruptFrame*) {
    auto& cpu = Cpu::getInstance();
    if (cpu.observer != nullptr) {
        cpu.observer->onInterrupt(Irq);
    }

    auto spurious = notifyEndOfInterrupt(Irq);
    if (spurious) {
        cpu.spuriousIRQCount++;
    }
}

Cpu::Cpu(void* interruptStack, void* syscallStack, std::uintptr_t stackTop, std::size_t stackSize) : 
    stackTop(stackTop),
    stackSize(stackSize),
    gdt{ 0 },
    idt{ { 0, 0 } },
    tss{},
    spuriousIRQCount(0),
    kernelContext{},
    observer{nullptr}
{
    setupGdt(interruptStack);
    setupIdt();
    setupSyscall(syscallStack);
    initializePIC(IdtHardwareInterruptBase, IdtHardwareInterruptBase + 8);
}

rlib::OwningPointer<Cpu> Cpu::instance{};

std::expected<Cpu*, rlib::Error> Cpu::makeCpu(rlib::Allocator& allocator, std::uintptr_t stackTop, std::size_t stackSize) {
    if (Cpu::instance != nullptr) {
        return std::unexpected(AlreadyCreated);
    }

    auto interruptStack = allocator.allocate(InterruptStackSize);
    if (interruptStack == nullptr) {
        return std::unexpected(rlib::OutOfMemoryError);
    }

    auto syscallStack = allocator.allocate(SyscallStackSize);
    if (syscallStack == nullptr) {
        return std::unexpected(rlib::OutOfMemoryError);
    }

    Cpu::instance = rlib::construct<Cpu>(allocator, interruptStack, syscallStack, stackTop, stackSize);
    if (Cpu::instance == nullptr) {
        return std::unexpected(rlib::OutOfMemoryError);
    }

    return Cpu::instance.get();
}

Cpu& Cpu::getInstance() {
    return *Cpu::instance;
}

void Cpu::halt() {
    asm volatile ("hlt");
}

void Cpu::growStack(TableView addressSpace, std::size_t newSize, PageMapper& pageMapper) {
    if (stackSize >= newSize) {
        return;
    }

    auto growth = (newSize - stackSize + 4_KiB - 1) & ~(4_KiB - 1);
    constexpr auto flags = PageFlags::Present | PageFlags::Writable | PageFlags::NoExecute;
    auto error = pageMapper.allocateAndMapRange(addressSpace, stackTop - stackSize - growth, flags, growth / 4_KiB);
    if (error) {
        panic("Cannot grow stack");
    }
    
    Register::CR3::flushTLBS();
    stackSize = growth;
}

void Cpu::registerObserver(CpuObserver& observer) {
    this->observer = &observer;
    asm volatile ("sti");
}

std::expected<Thread*, rlib::Error> Cpu::createThread(rlib::Allocator& allocator, AddressSpace addressSpace, std::uint64_t entryPoint, std::size_t stackSize, Context::Flags::Type flags) {
    Context context;
    context.cr3 = addressSpace.pageDirectoryPhysicalAddress();
    context.rip = entryPoint;
    context.flags = flags;
    context.rflags = 0x202; // Enable interrupts.
    
    // Elf file should not put segments here. Perhaps allocate a region dynamically.
    auto stackBottom = 0x8000'0000'0000 - stackSize;
    auto stackFlags = PageFlags::Present | PageFlags::Writable | PageFlags::UserAccessible | PageFlags::NoExecute;
    auto stack = addressSpace.allocate(stackBottom, stackSize, stackFlags, PageSize::_4KiB);
    if (!stack) {
        return std::unexpected(stack.error());
    }
    context.rsp = stackBottom + stackSize;

    auto threadPtr = rlib::constructRaw<Thread>(allocator,  Thread{ context, std::move(addressSpace) });
    if (threadPtr == nullptr) {
        return std::unexpected(OutOfPhysicalMemory);
    }
    threads.push_front(*threadPtr);

    return threadPtr;
}

void Cpu::killThread(rlib::Allocator& allocator, Thread& thread) {
    threads.remove(thread);
    rlib::destruct(&thread, allocator);
}

void Cpu::scheduleThread(Thread& thread) {
    ::switchContext(&thread.context);
}

void Cpu::setupGdt(void* interruptStack) {
    constexpr auto DataSegmentAccess = GdtAccess::CodeDataSegment
                                       | GdtAccess::Present
                                       | GdtAccess::ReadableWritable;
    constexpr auto CodeSegmentAccess = DataSegmentAccess | GdtAccess::Executable;
    constexpr auto TssSegmentBase = 5;

    // Construct tss
    
    tss.ist[IstIndex - 1] = reinterpret_cast<std::uintptr_t>(interruptStack) + InterruptStackSize; 
    tss.iobp = sizeof(TaskStateSegment); // No IOBP

    gdt[0] = 0;
    // Kernel code segment
    gdt[KernelSegmentIndex] = makeSegmentDescriptor(CodeSegmentAccess);
    // Kernel data segment
    gdt[KernelSegmentIndex + 1] = makeSegmentDescriptor(DataSegmentAccess);
     // User data segment. Comes before code segment because of sysret.
    gdt[UserSegmentIndex] = makeSegmentDescriptor(DataSegmentAccess | GdtAccess::UserMode);
    // User code segment
    gdt[UserSegmentIndex + 1] = makeSegmentDescriptor(CodeSegmentAccess | GdtAccess::UserMode);
   
    // Tss segment
    std::tie(gdt[TssSegmentBase], gdt[TssSegmentBase + 1]) = makeTaskStateSegmentDescriptor(reinterpret_cast<uintptr_t>(&tss));

    setGdt(sizeof(gdt), gdt, KernelSegmentIndex, TssSegmentBase);
}

void Cpu::setupIdt() {
    idt[8] = makeGateDescriptor(reinterpret_cast<uintptr_t>(&doubleFaultHandler), KernelSegmentIndex, GateType::Trap, IstIndex);
    
    // Install 16 IRQ handlers 
    [this]<std::size_t ...Is>(std::index_sequence<Is...>) {
        (
            (idt[Is + IdtHardwareInterruptBase] = 
                makeGateDescriptor(reinterpret_cast<uintptr_t>(&hardwareInterruptHandler<Is>), KernelSegmentIndex, GateType::Interrupt, IstIndex)
            ),
            ...
        );  
    }(std::make_index_sequence<16>{});

    setIdt(sizeof(idt), idt);
}

void Cpu::setupSyscall(void* syscallStack) {
    core.kernelStack = reinterpret_cast<std::uintptr_t>(syscallStack) + SyscallStackSize;
    core.activeContext = &kernelContext;
    
    setupSyscallHandler(KernelSegmentIndex, UserSegmentIndex, &core);
}

std::uint64_t Register::CR3::read() {
    std::uint64_t cr3;

    asm volatile (
        "mov %%cr3, %%rax;"
        "mov %%rax, %0"
    : "=m" (cr3) : : "%rax"
    );

    return cr3;
};

void Register::CR3::flushTLBS() {
    asm volatile (
        "mov %%cr3, %%rax;"
        "mov %%rax, %%cr3"
    : : : "%rax"
    );
};

extern "C" Context* systemCallHandler() {
    auto& cpu = Cpu::getInstance();
    
    if (cpu.observer == nullptr) {
        return cpu.core.activeContext;
    }
    cpu.observer->onSyscall(Thread::fromContext(*(cpu.core.activeContext)));

    return &(cpu.kernelContext);
}

#include "cpu.hpp"
#include "../error/error.hpp"
#include <tuple>

extern "C" void setGdt(std::uint16_t size, std::uint64_t* base, std::uint16_t codeSegment, std::uint16_t dataSegment, std::uint16_t tssSegment);

extern "C" void setIdt(std::uint16_t size, IdtDescriptor* base);

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
    low |= std::uint64_t(codeSegmentIndex) << 3 << 16;
    low |= std::uint64_t(istIndex & 7) << 32;
    low |= (static_cast<std::uint64_t>(gateType) & 0xf) << 40;
    low |= std::uint64_t(1) << 47;
    low |= (isrAddress & 0xffff0000) >> 16 << 48;
    
    auto high = isrAddress >> 32;
    return { low, high};
}

__attribute__((interrupt)) void doubleFaultHandler(InterruptFrame *frame, std::uint64_t errorCode) {
    // In response to a double fault we should abort.
    panic("Double fault");
};

Cpu::Cpu(Memory::Allocator& allocator) : gdt{ 0 }, idt{ 0 }, tss{}, interruptBuffer{} {
    setupGdt(allocator);
    setupIdt();
}

Cpu* Cpu::instance = nullptr;

Cpu& Cpu::makeCpu(Memory::Allocator& allocator) {
    if (Cpu::instance != nullptr) {
        panic("CPU already constructed");
    }

    Cpu::instance = allocator.construct<Cpu>(allocator);
    if (Cpu::instance == nullptr) {
        panic("Not enough memory for CPU");
    }

    return *Cpu::instance;
}

Cpu& Cpu::getInstance() {
    return *Cpu::instance;
}

void Cpu::halt() {
    asm volatile ("hlt");
}

HardwareInterrupt* Cpu::consumeInterrupts(HardwareInterrupt *dest) {
    return interruptBuffer.popAll(dest);
}

void Cpu::setupGdt(Memory::Allocator& allocator) {
    constexpr auto DataSegmentAccess = GdtAccess::CodeDataSegment
                                      | GdtAccess::Present
                                      | GdtAccess::ReadableWritable;
    constexpr auto CodeSegmentAccess = DataSegmentAccess | GdtAccess::Executable;
    constexpr auto KernelDataSegment = 2;
    constexpr auto TssSegmentBase = 5;

    // Construct tss
    constexpr auto interruptStackSize = std::size_t(1024);
    auto interruptStack = allocator.allocate(interruptStackSize, 16);
    tss.ist[IstIndex - 1] = reinterpret_cast<std::uintptr_t>(interruptStack) + interruptStackSize; 
    tss.iobp = sizeof(TaskStateSegment); // No IOBP

    gdt[0] = 0;
    // Kernel code segment
    gdt[KernelCodeSegmentIndex] = makeSegmentDescriptor(CodeSegmentAccess);
    // Kernel data segment
    gdt[KernelDataSegment] = makeSegmentDescriptor(DataSegmentAccess);
    // User code segment
    gdt[3] = makeSegmentDescriptor(CodeSegmentAccess | GdtAccess::UserMode);
    // User data segment
    gdt[4] = makeSegmentDescriptor(DataSegmentAccess | GdtAccess::UserMode);
    // Tss segment
    std::tie(gdt[TssSegmentBase], gdt[TssSegmentBase + 1]) = makeTaskStateSegmentDescriptor(reinterpret_cast<uintptr_t>(&tss));

    setGdt(sizeof(gdt), gdt, KernelCodeSegmentIndex, KernelDataSegment, TssSegmentBase);
}

void Cpu::setupIdt() {
    idt[8] = makeGateDescriptor(reinterpret_cast<uintptr_t>(&doubleFaultHandler), KernelCodeSegmentIndex, GateType::Trap, IstIndex);

    setIdt(sizeof(idt), idt);
}

std::uint64_t Register::CR3::read() {
    std::uint64_t cr3;

    asm (
        "mov %%cr3, %%rax;"
        "mov %%rax, %0"
    : "=m" (cr3)
    : // No input
    : "%rax"
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

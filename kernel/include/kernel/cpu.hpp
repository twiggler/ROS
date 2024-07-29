#pragma once

#include <cstdint>
#include "paging.hpp"
#include <libr/allocator.hpp>
#include <libr/ringbuffer.hpp>
#include <bit>
#include <libr/pointer.hpp>
#include <libr/error.hpp>
#include <libr/intrusive/list.hpp>

namespace Register {

    struct CR3 {
        static std::uint64_t read();

        static void write(std::uint64_t rootPageTablePhysicalAddress);

        static void flushTLBS();
    };

} // namespace Register

struct CpuErrorCategory : rlib::ErrorCategory {};
inline constexpr auto cpuErrorCategory = CpuErrorCategory{};

inline constexpr auto AlreadyCreated = rlib::Error{-1, &cpuErrorCategory};


struct Context {
    struct Flags {
        using Type                       = std::uint16_t;
        static constexpr auto KernelMode = Type(1) << 0;
    };

    static Context make(
        Context::Flags::Type flags,
        std::uintptr_t       rootPageTablePhysicalAddress,
        VirtualAddress       entryPoint,
        VirtualAddress       stackTop
    );

    std::uint64_t rflags;
    std::uint64_t cr3;
    std::uint64_t rip;
    std::uint64_t rbx;
    std::uint64_t rsp;
    std::uint64_t rbp;
    std::uint64_t r12;
    std::uint64_t r13;
    std::uint64_t r14;
    std::uint64_t r15;
    Flags::Type   flags;
} __attribute__((packed));

// Assert no padding, otherwise assembler code will break.
static_assert(sizeof(Context) == 10 * sizeof(std::uint64_t) + sizeof(std::uint16_t));

enum class GateType : std::uint8_t {
    Interrupt = 0xe,
    Trap      = 0xf
};

struct IdtDescriptor {
    std::uint64_t low;
    std::uint64_t high;
};

struct __attribute__((packed)) TaskStateSegment {
    std::uint32_t reserved0;
    std::uint64_t rsp0;
    std::uint64_t rsp1;
    std::uint64_t rsp2;
    std::uint64_t reserved1;
    std::uint64_t ist[7];
    std::uint64_t reserved2;
    std::uint16_t reserved3;
    std::uint16_t iobp;
};


struct InterruptFrame;

struct Core {
    std::uintptr_t kernelStack;
    Context*       activeContext;
} __attribute__((packed));

// Assert no padding, otherwise assembler code will break.
static_assert(sizeof(Core) == 2 * sizeof(std::uint64_t));

struct CpuObserver {
    virtual void onInterrupt(std::uint8_t Irq) = 0;

    virtual Context& onSyscall(Context& sender) = 0;
};


extern "C" Context* systemCallHandler();

class Cpu {
public:
    Cpu(void* interruptStack, void* syscallStack, Context& initialContext);

    static std::expected<Cpu*, rlib::Error> make(rlib::Allocator& allocator, Context& intialContext);

    static void setRootPageTable(std::uint64_t rootPageTablePhysicalAddress);

    static Cpu& getInstance();

    static void halt();

    void registerObserver(CpuObserver& observer);

    void scheduleContext(Context& context);

private:
    template<std::uint8_t Irq>
    friend __attribute__((interrupt)) void hardwareInterruptHandler(InterruptFrame* frame);

    friend Context* systemCallHandler();

    static rlib::OwningPointer<Cpu> instance;

    void setupGdt(void* interruptStack);
    void setupIdt();
    void setupSyscall(void* syscallStack, Context& initialContext);

    static constexpr auto KernelSegmentIndex       = std::uint16_t(1);
    static constexpr auto UserSegmentIndex         = std::uint16_t(3);
    static constexpr auto IstIndex                 = std::uint8_t(1);
    static constexpr auto IdtHardwareInterruptBase = std::uint8_t(32);
    static constexpr auto InterruptStackSize       = 1_KiB;
    static constexpr auto SyscallStackSize         = 1_KiB;

    uint64_t      gdt[7];
    IdtDescriptor idt[256];
    // From the Intel 64 Architectures manual: Volume 3A
    // "Avoid placing a page boundary in the part of the TSS that the processor reads
    // during a task switch (the first 104 bytes)."
    alignas(std::bit_ceil(sizeof(TaskStateSegment))) TaskStateSegment tss;
    std::atomic<std::size_t> spuriousIRQCount;
    Core                     core; // A single core for now

    CpuObserver* observer;
};

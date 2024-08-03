#pragma once

#include "paging.hpp"
#include <libr/allocator.hpp>
#include <libr/stream.hpp>
#include <libr/intrusive/list.hpp>
#include "cpu.hpp"
#include <libr/elf.hpp>

struct KernelErrorCategory : rlib::ErrorCategory {};
inline constexpr auto kernelErrorCategory = KernelErrorCategory{};

inline constexpr auto CannotParseElf           = rlib::Error{-1, &kernelErrorCategory};
inline constexpr auto CannotCreateAddressSpace = rlib::Error{-2, &kernelErrorCategory};
inline constexpr auto InvalidSegmentSize       = rlib::Error{-3, &kernelErrorCategory};
inline constexpr auto CannotMapProcessMemory   = rlib::Error{-4, &kernelErrorCategory};
inline constexpr auto CannotCopySegment        = rlib::Error{-5, &kernelErrorCategory};
inline constexpr auto UnexpectedMemoryLayout   = rlib::Error{-6, &kernelErrorCategory};

struct Thread {
    static Thread* fromContext(Context& context);

    static std::expected<Thread*, rlib::Error>
    make(rlib::Allocator& allocator, AddressSpace addressSpace, std::uint64_t entryPoint, std::uintptr_t stackTop);

    Thread(Context context, AddressSpace addressSpace);

    Context context;

    AddressSpace addressSpace;

    rlib::intrusive::ListNode<Thread> listNode;
};

struct HardwareInterrupt {
    std::uint8_t IRQ;
};

struct Message {
    Thread* origin;
};

struct MemoryLayout {
    rlib::Iterator<Block>* freeMemoryBlocks;
    std::size_t            totalPhysicalMemory;
    IdentityMapping        identityMapping;
    VirtualAddress         kernelCodeStart;
    VirtualAddress         kernelWritableDataStart;
    VirtualAddress         kernelWritableDataEnd;
    std::size_t            initialKernelStackSize;
    std::uint32_t*         framebufferStart;
    std::size_t            framebufferSize;
    std::uintptr_t         initrdPhysicalAddress;
    std::size_t            initrdSize;
};

class Kernel : public CpuObserver {
public:
    using ThreadList = rlib::intrusive::ListWithNodeMember<Thread, &Thread::listNode>;

    static constexpr auto IntialHeapSize = std::size_t(4_KiB);

    static std::expected<Kernel, rlib::Error>
    make(MemoryLayout memoryLayout, std::byte* initialHeapStorage, TableView rootPageTable);

    Kernel(
        Thread*                                              kernelThread,
        PageMapper*                                          pageMapper,
        Cpu&                                                 cpu,
        rlib::Allocator*                                     allocator,
        rlib::InputStream<rlib::MemorySource>                initrd,
        ThreadList                                           threads,
        rlib::OwningPointer<rlib::mpmcBoundedQueue<Message>> mailbox,
        std::uint32_t*                                       framebuffer
    );

    Kernel(const Kernel&) = delete;

    Kernel& operator=(const Kernel&) = delete;

    void run();

    std::expected<Thread*, rlib::Error>
    createThread(AddressSpace addressSpace, std::uint64_t entryPoint, std::uintptr_t stackTop);

    void scheduleThread(Thread& thread);

    void killThread(rlib::Allocator& allocator, Thread& thread);

    virtual void onInterrupt(std::uint8_t Irq) final;

    virtual Context& onSyscall(Context& sender) final;

private:
    static constexpr auto KernelStackSize     = std::size_t(64_KiB);
    static constexpr auto InterruptBufferSize = std::size_t(256);
    static constexpr auto MessageBufferSize   = std::size_t(256);
    static constexpr auto KernelHeapSize      = std::size_t(1_MiB);

    static std::expected<std::tuple<AddressSpace, Region*>, rlib::Error> makeKernelAddressSpace(
        TableView rootPageTable, MemoryLayout memoryLayout, PageMapper& pageMapper, rlib::Allocator& allocator
    );

    Thread* kernelThread() const;

    // TODO: Implement type erased InputStream
    std::expected<Thread*, rlib::Error> loadProcess(rlib::InputStream<rlib::MemorySource>& process);

    PageMapper*                                                    pageMapper;
    Cpu*                                                           cpu;
    rlib::Allocator*                                               allocator;
    rlib::spscBoundedQueue<HardwareInterrupt, InterruptBufferSize> interrupts;
    ThreadList                                                     threads;
    rlib::OwningPointer<rlib::mpmcBoundedQueue<Message>>           mailbox;
    std::uint32_t*                                                 framebuffer;
    Thread*                                                        service;
};

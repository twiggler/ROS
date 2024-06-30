#pragma once

#include "paging.hpp"
#include <libr/allocator.hpp>
#include <libr/stream.hpp>
#include "cpu.hpp"
#include <libr/elf.hpp>

struct KernelErrorCategory : rlib::ErrorCategory {};
inline constexpr auto kernelErrorCategory = KernelErrorCategory{};

inline constexpr auto CannotParseElf = rlib::Error{-1, &kernelErrorCategory};
inline constexpr auto CannotCreateAddressSpace = rlib::Error{-2, &kernelErrorCategory};
inline constexpr auto InvalidSegmentSize = rlib::Error{-3, &kernelErrorCategory};
inline constexpr auto CannotMapProcessMemory = rlib::Error{-4, &kernelErrorCategory};
inline constexpr auto CannotCopySegment = rlib::Error{-5, &kernelErrorCategory};

struct HardwareInterrupt {
    std::uint8_t IRQ;
};

struct Message {
    Thread* sender;
};

class Kernel : public CpuObserver {
public:
    static constexpr auto IntialHeapSize = std::size_t(512);
    
    static std::expected<Kernel, rlib::Error> make(rlib::Iterator<Block>& memoryMap, IdentityMapping identityMapping, void* initialHeapStorage, TableView rootPageTable, rlib::InputStream<rlib::MemorySource> initrd, std::uint32_t* framebuffer);

    Kernel(TableView addressSpace, PageMapper* pageMapper, Cpu& cpu, rlib::Allocator* allocator, rlib::InputStream<rlib::MemorySource> initrd, std::uint32_t* framebuffer);

    Kernel(const Kernel&) = delete;

    Kernel& operator=(const Kernel&) = delete;

    void run();

    virtual void onInterrupt(std::uint8_t Irq) final;

    virtual void onSyscall(Thread* sender) final;

private:
    // TODO: Implement type erased InputStream
    std::optional<rlib::Error> loadProcess(rlib::InputStream<rlib::MemorySource>& process);    

    TableView                                   addressSpace; 
    PageMapper*                                 pageMapper;
    Cpu*                                        cpu;
    rlib::Allocator*                            allocator;
    std::uint32_t*                              framebuffer;  
    rlib::RingBuffer<Message, 256>              mailbox; // This supports only a single producer i.e. core
    rlib::RingBuffer<HardwareInterrupt, 256>    interrupts;
    Thread*                                     service;
};



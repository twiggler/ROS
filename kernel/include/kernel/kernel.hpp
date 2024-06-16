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
    // Design: Type erase allocator?
    Kernel(TableView addressSpace, PageMapper pageMapper, Cpu& cpu, rlib::BumpAllocator allocator, rlib::InputStream<rlib::MemorySource> initrd, std::uint32_t* framebuffer);

    void run();

    virtual void onInterrupt(std::uint8_t Irq) final;

    virtual void onSyscall(Thread* sender) final;

private:
    // TODO: Implement type erased InputStream
    std::optional<rlib::Error> loadProcess(rlib::InputStream<rlib::MemorySource>& process);    

    TableView                   addressSpace; 
    PageMapper                  pageMapper;
    Cpu*                        cpu;
    rlib::BumpAllocator         allocator;
    std::uint32_t*              framebuffer;  
    rlib::RingBuffer<Message, 256> mailbox; // This supports only a single producer i.e. core
    rlib::RingBuffer<HardwareInterrupt, 256> interrupts;
    Thread*                     service;
};



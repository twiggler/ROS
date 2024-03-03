#include <cstdint>
#include <bootboot.h>
#include <ranges>
#include <cstddef>
#include <numeric>
#include <utility>
#include "memory/paging.hpp"
#include "memory/allocator.hpp"
#include "cpu/cpu.hpp"
#include "error/error.hpp"
#include "kernel.hpp"

// BOOTBOOT imported virtual addresses, see see linker script
extern BOOTBOOT bootboot;
extern unsigned char environment[4096];
extern std::uint32_t fb;                // linear framebuffer mapped

constexpr std::size_t frameSize = 1024 * 4;

using namespace Memory;

// Statically reserve buffer to hold the page frame allocator and page mapper
alignas(PageFrameAllocator) unsigned char frameAllocatorBuffer[sizeof(PageFrameAllocator)];
alignas(PageMapper) unsigned char pageMapperBuffer[sizeof(PageMapper)];

FrameBufferInfo getFrameBufferInfo() {
    return {
        &fb,
        bootboot.fb_size,
        bootboot.fb_width,
        bootboot.fb_height,
        bootboot.fb_scanline
    };
}

// Side effect: frame allocator is constructed in-place at "frameAllocatorBuffer".
PageFrameAllocator& makePageFrameAllocator() {
    auto numberOfMemoryMapEntries = (bootboot.size - 128) / 16;
    auto memoryMap = std::span(&bootboot.mmap, numberOfMemoryMapEntries); 
    
    auto sizeAccumulator =  [](auto acc, auto block) { return acc + MMapEnt_Size(&block); };
    auto totalMemory = std::accumulate(memoryMap.begin(), memoryMap.end(), std::size_t(0), sizeAccumulator); 

    std::ranges::forward_range auto alignedBlocks = memoryMap
        | std::views::filter([](auto entry) { return MMapEnt_IsFree(&entry); })
        | std::views::transform([](auto entry) { return Block{entry.ptr, MMapEnt_Size(&entry) }; })
        | std::views::transform([](auto block) { return block.align(frameSize); });
   
    auto requiredSpace = PageFrameAllocator::requiredStorage(totalMemory / frameSize);
    auto storageBlock = std::ranges::find_if(alignedBlocks, [=](auto block) { return block.size >= requiredSpace; });
    if (storageBlock == alignedBlocks.end()) {
        panic("Not enough contiguous memory to initialize page frame allocator.");
    }

    auto justEnoughStorage = (*storageBlock).resize(requiredSpace);
    auto allocator = new(frameAllocatorBuffer) PageFrameAllocator(justEnoughStorage, totalMemory, frameSize);

    for (auto block : alignedBlocks) {
        if (block.startAddress == (*storageBlock).startAddress) {
            block.startAddress += requiredSpace;
            block.size -= requiredSpace;
            block = block.align(frameSize);
        }
        
        auto startAddress = block.startAddress;
        while (startAddress < block.startAddress + block.size) {
            allocator->dealloc(reinterpret_cast<void*>(startAddress));
            startAddress += frameSize;
        } 
    }

    return *allocator;
}

PageMapper& makePageMapper(PageFrameAllocator& frameAllocator) {
    auto level4Table = reinterpret_cast<std::uint64_t*>(Register::CR3::read());
    auto pageMapper = new (pageMapperBuffer) PageMapper(level4Table, 0 /* Bootboot identity maps first 16GB */, frameAllocator);
    return *pageMapper;
}

std::uintptr_t patchMemoryLayout(PageMapper& pageMapper, std::size_t physicalMemory) {
    // Map all physical memory to start of higher half virtual address space. 
    constexpr auto flags = PageFlags::Present | PageFlags::Writable | PageFlags::NoExecute;
    
    // 00000000 00000000 - 00007FFF FFFFFFFF (lower half)
    // 00008000 00000000 - FFFF7FFF FFFFFFFF (the hole, not valid)
    // FFFF8000 00000000 - FFFFFFFF FFFFFFFF (higher half)
    constexpr auto higherHalf = std::uintptr_t(0xffff'8000'0000'0000);
    auto virtualAddress = higherHalf;
    for (auto physicalAddress = std::uint64_t(0); physicalAddress < physicalMemory; physicalAddress += 1_GiB) {
        auto mapResult = pageMapper.map(virtualAddress, physicalAddress, PageSize::_1GiB, flags);
        if (mapResult != MapResult::OK) {
            panic("Cannot map physical memory");
        } 
        virtualAddress += 1_GiB;
    }

    // Delete identity mapping constructed by BOOTBOOT.
    pageMapper.relocate(higherHalf);
    auto physicalAddress = std::uint64_t(0);
    do {
        auto unmappedBytes = pageMapper.unmap(physicalAddress);
        if (unmappedBytes == 0) {
            panic("Unexpected memory layout");
        }
        physicalAddress += unmappedBytes;
    } while (physicalAddress < 16_GiB);

    Register::CR3::flushTLBS();
    return virtualAddress;
}

auto makeHeap(PageMapper& pageMapper, std::uintptr_t heapStart, std::size_t heapSizeInFrames) {
    constexpr auto flags = PageFlags::Present | PageFlags::Writable | PageFlags::NoExecute;    
    auto result = pageMapper.allocateAndMapContiguous(heapStart, flags, heapSizeInFrames);
    if (result != MapResult::OK) {
        panic("Cannot construct kernel heap");
    }
    Register::CR3::flushTLBS();
    
    return BumpAllocator(reinterpret_cast<void*>(heapStart), heapSizeInFrames * 4_KiB);
}

Kernel makeKernel() {
    auto& frameAllocator = makePageFrameAllocator();
    auto& pageMapper = makePageMapper(frameAllocator);
    auto heapStart = patchMemoryLayout(pageMapper, frameAllocator.physicalMemory());
    auto allocator = makeHeap(pageMapper, heapStart, 4);
    auto& cpu = Cpu::makeCpu(allocator, 0xffffffff'ffffffff, 4_KiB);
    
    return Kernel(pageMapper, cpu);
}

int main()
{
    initializePanicHandler(getFrameBufferInfo());
   
    auto kernel = makeKernel();
    kernel.run();

    return 0;
}

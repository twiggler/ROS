#include <cstdint>
#include <bootboot.h>
#include <ranges>
#include <cstddef>
#include <numeric>
#include <utility>
#include "memory/memory.hpp"
#include "cpu/cpu.hpp"
#include "error/error.hpp"

// imported virtual addresses, see linker script
extern BOOTBOOT bootboot;               // see bootboot.h
extern unsigned char environment[4096]; // configuration, UTF-8 text key=value pairs
extern std::uint32_t fb;                // linear framebuffer mapped

constexpr std::size_t frameSize = 1024 * 4;

using namespace Memory;

FrameBufferInfo getFrameBufferInfo() {
    return {
        &fb,
        bootboot.fb_size,
        bootboot.fb_width,
        bootboot.fb_height,
        bootboot.fb_scanline
    };
}

PageFrameAllocator makePageFrameAllocator() {
    auto numberOfMemoryMapEntries = (bootboot.size - 128) / 16;
    auto memoryMap = std::span(&bootboot.mmap, numberOfMemoryMapEntries); 
    
    auto sizeAccumulator =  [](auto acc, auto block) { return acc + block.size; };
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
    auto allocator = PageFrameAllocator(justEnoughStorage, totalMemory, frameSize);

    for (auto block : alignedBlocks) {
        if (block.ptr == (*storageBlock).ptr) {
            block.ptr += requiredSpace;
            block.size -= requiredSpace;
            block = block.align(frameSize);
        }
        
        auto startAddress = block.ptr;
        while (startAddress < block.ptr + block.size) {
            allocator.dealloc(reinterpret_cast<void*>(block.ptr));
            startAddress += frameSize;
        } 
    }

    return allocator;
}

void patchMemoryLayout(PageFrameAllocator& frameAllocator) {
    auto level4Table = reinterpret_cast<std::uint64_t*>(Register::CR3::read());
    auto pageMapper = PageMapper(level4Table, 0 /* Bootboot identity maps first 16GB */, frameAllocator);

    // Map all physical memory to start of higher half virtual address space. 
    constexpr auto physicalOffset = std::uintptr_t(0xffff'8000'0000'0000); // Start of higher half
    constexpr auto flags = PageFlags::Present | PageFlags::Writable | PageFlags::NoExecute;
    for (auto physicalAddress = std::uint64_t(0); physicalAddress < frameAllocator.physicalMemory(); physicalAddress += 1_GiB) {
        auto mapResult = pageMapper.map(physicalOffset + physicalAddress, physicalAddress, PageSize::_1GiB, flags);
        if (mapResult != 0) {
            panic("Cannot map physical memory");
        } 
    }

    // Delete identity mapping.
    pageMapper.relocate(physicalOffset);
    auto virtualAddress = std::uint64_t(0);
    do {
        auto unmappedBytes = pageMapper.unmap(virtualAddress);
        if (unmappedBytes == 0) {
            panic("Unexpected memory layout");
        }
        virtualAddress += unmappedBytes;
    } while (virtualAddress < 16_GiB);

    Register::CR3::flushTLBS();

}

void makeKernel() {
    auto frameAllocator = makePageFrameAllocator();
    patchMemoryLayout(frameAllocator);
    
    panic("Memory layout patched.");
}

int main()
{
    initializePanicHandler(getFrameBufferInfo());

    makeKernel();

    return 0;
}

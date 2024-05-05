#include "abi/icxxabi.h"
#include <cstdint>
#include <stream.hpp>
#include <bootboot.h>
#include <ranges>
#include <cstddef>
#include <numeric>
#include <utility>
#include "memory/paging.hpp"
#include <allocator.hpp>
#include "cpu/cpu.hpp"
#include "error/error.hpp"
#include "kernel.hpp"
#include <concepts>

// BOOTBOOT imported virtual addresses, see see linker script
extern BOOTBOOT bootboot;
extern unsigned char environment[4096];
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

template <typename R>
concept BlockRange = std::ranges::forward_range<R> && std::same_as<std::ranges::range_value_t<R>, Memory::Block>;

class OneShotAllocator : public rlib::Allocator {
    BlockRange auto availableBlockView() const {
        return memoryMap
            | std::views::filter([](auto entry) { return MMapEnt_IsFree(&entry); })
            | std::views::transform([](auto entry) { return Block{entry.ptr, MMapEnt_Size(&entry) }; })
            | std::views::transform([](auto block) { return block.align(frameSize); });
            // TODO: restrict to first 16GB
    }
public:
    explicit OneShotAllocator(std::span<const MMapEnt> memoryMap) : 
        memoryMap(std::move(memoryMap)),
        allocatedBlock(Block{0, 0}) { }


    BlockRange auto freeBlocks() const {
        return availableBlockView() | std::views::transform([this](auto block) { 
            if (block.startAddress == allocatedBlock.startAddress) {
                block.take(allocatedBlock.size).align(frameSize); 
            };

            return block;
        });
    }
private:
    virtual void* do_allocate(std::size_t bytes, std::size_t alignment = alignof(std::max_align_t)) {
        auto availableBlocks = availableBlockView();
        auto storageBlock = std::ranges::find_if(availableBlocks, [=](auto block) { return block.size >= bytes + alignment; });
        if (storageBlock == availableBlocks.end()) {
            return nullptr;
        }
        allocatedBlock = (*storageBlock).resize(bytes + alignment);

        // Prevent allocation at physical address 0x0, which is identity mapped to virtual address 0x0 
        return reinterpret_cast<void*>(allocatedBlock.startAddress + alignment);
    }

    virtual void do_deallocate( void* p, std::size_t bytes, std::size_t alignment ) { 
        // NOOP        
    }

    std::span<const MMapEnt> memoryMap;
    Block allocatedBlock;
};

PageFrameAllocator makePageFrameAllocator() {
    auto numberOfMemoryMapEntries = (bootboot.size - 128) / 16;
    auto memoryMap = std::span(&bootboot.mmap, numberOfMemoryMapEntries); 
    
    auto sizeAccumulator =  [](auto acc, auto block) { return acc + MMapEnt_Size(&block); };
    auto totalMemory = std::accumulate(memoryMap.begin(), memoryMap.end(), std::size_t(0), sizeAccumulator); 

    // Statically reserve buffer to hold the allocator for the page frame allocator.
    alignas(OneShotAllocator) static unsigned char allocatorBuffer[sizeof(OneShotAllocator)];
    auto oneShotAllocator = new (allocatorBuffer) OneShotAllocator(std::move(memoryMap));

    auto pageFrameAllocator = PageFrameAllocator::make(*oneShotAllocator, totalMemory, frameSize);
    if (!pageFrameAllocator) {
        panic("Not enough contiguous memory to initialize page frame allocator.");
    }

    for (auto block : oneShotAllocator->freeBlocks()) {
        auto startAddress = block.startAddress;
        while (startAddress < block.startAddress + block.size) {
            pageFrameAllocator->dealloc(reinterpret_cast<void*>(startAddress));
            startAddress += frameSize;
        } 
    }

    return std::move(*pageFrameAllocator);
}

std::uintptr_t patchMemoryLayout(std::uint64_t* tableLevel4, PageMapper& pageMapper, std::size_t physicalMemory) {
    // Map all physical memory to start of higher half virtual address space. 
    constexpr auto flags = PageFlags::Present | PageFlags::Writable | PageFlags::NoExecute;
    
    // 00000000 00000000 - 00007FFF FFFFFFFF (lower half)
    // 00008000 00000000 - FFFF7FFF FFFFFFFF (the hole, not valid)
    // FFFF8000 00000000 - FFFFFFFF FFFFFFFF (higher half)
    constexpr auto higherHalf = std::uintptr_t(0xffff'8000'0000'0000);
    auto virtualAddress = higherHalf;
    for (auto physicalAddress = std::uint64_t(0); physicalAddress < physicalMemory; physicalAddress += 1_GiB) {
        auto mapResult = pageMapper.map(tableLevel4, virtualAddress, physicalAddress, PageSize::_1GiB, flags);
        if (mapResult != MapResult::OK) {
            panic("Cannot map physical memory");
        } 
        virtualAddress += 1_GiB;
    }
    pageMapper.relocate(higherHalf);

    Register::CR3::flushTLBS();
    return virtualAddress;
}

auto makeHeap(std::uint64_t* tableLevel4, PageMapper& pageMapper, std::uintptr_t heapStart, std::size_t heapSizeInFrames) {
    constexpr auto flags = PageFlags::Present | PageFlags::Writable | PageFlags::NoExecute;    
    auto result = pageMapper.allocateAndMapContiguous(tableLevel4, heapStart, flags, heapSizeInFrames);
    if (result != MapResult::OK) {
        panic("Cannot construct kernel heap");
    }
    Register::CR3::flushTLBS();
    
    return rlib::BumpAllocator(reinterpret_cast<void*>(heapStart), heapSizeInFrames * 4_KiB);
}

Kernel makeKernel() {
    auto frameAllocator = makePageFrameAllocator();
    auto physicalMemory = frameAllocator.physicalMemory();

    auto tableLevel4 = reinterpret_cast<std::uint64_t*>(Register::CR3::read());
    auto pageMapper = PageMapper(0 /* Bootboot identity maps first 16GB */, std::move(frameAllocator));

    auto heapStart = patchMemoryLayout(tableLevel4, pageMapper, physicalMemory);
    auto allocator = makeHeap(tableLevel4, pageMapper, heapStart, 4);
    
    auto& cpu = Cpu::makeCpu(allocator, 0xffffffff'ffffffff, 4_KiB);

    auto memorySource = rlib::MemorySource(reinterpret_cast<std::byte*>(bootboot.initrd_ptr), bootboot.initrd_size);
    auto inputStream = rlib::InputStream(std::move(memorySource));
    
    return Kernel(tableLevel4, std::move(pageMapper), cpu, std::move(inputStream));
}

int main()
{
    initializePanicHandler(getFrameBufferInfo());
   
    auto kernel = makeKernel();
    kernel.run();

    return 0;
}

#include <libr/icxxabi.hpp>
#include <libr/stream.hpp>
#include <libr/allocator.hpp>
#include <libr/type_erasure.hpp>
#include <cstdint>
#include <bootboot.h>
#include <ranges>
#include <cstddef>
#include <numeric>
#include <utility>
#include <kernel/kernel.hpp>
#include <kernel/paging.hpp>
#include <kernel/cpu.hpp>
#include <kernel/panic.hpp>
#include <concepts>

// BOOTBOOT imported virtual addresses, see see linker script
extern BOOTBOOT bootboot;
extern unsigned char environment[4096];
extern std::uint32_t fb;                // linear framebuffer mapped

constexpr std::size_t frameSize = 1024 * 4;

FrameBufferInfo getFrameBufferInfo() {
    return {
        &fb,
        bootboot.fb_size,
        bootboot.fb_width,
        bootboot.fb_height,
        bootboot.fb_scanline
    };
}

struct MemoryMapIterator : public rlib::Iterator<Block> {
    explicit MemoryMapIterator(std::span<MMapEnt> memoryMap) : 
        memoryMap(std::move(memoryMap)),
        index(0) { }
    
    virtual std::optional<Block> next() {
        while (index < memoryMap.size()) {
            if (!MMapEnt_IsFree(&memoryMap[index])) {
                index++;
                continue;
            }
            return Block{memoryMap[index].ptr, memoryMap[index].size}.align(frameSize);
        }
    }
private:
    std::span<MMapEnt> memoryMap;
    std::size_t index;
};

PageFrameAllocator makePageFrameAllocator() {
    auto numberOfMemoryMapEntries = (bootboot.size - 128) / 16;
    auto memoryMap = std::span(&bootboot.mmap, numberOfMemoryMapEntries); 
    
    auto sizeAccumulator =  [](auto acc, auto block) { return acc + MMapEnt_Size(&block); };
    auto totalMemory = std::accumulate(memoryMap.begin(), memoryMap.end(), std::size_t(0), sizeAccumulator); 

    auto memoryMapIterator = MemoryMapIterator(std::move(memoryMap));

    auto pageFrameAllocator = PageFrameAllocator::make(memoryMapIterator, totalMemory, frameSize);
    if (!pageFrameAllocator) {
        panic("Not enough contiguous memory to initialize page frame allocator.");
    }

    return std::move(*pageFrameAllocator);
}

auto makeHeap(TableView tableLevel4, PageMapper& pageMapper, std::uintptr_t heapStart, std::size_t heapSizeInFrames) {
    constexpr auto flags = PageFlags::Present | PageFlags::Writable | PageFlags::NoExecute;    
    auto error = pageMapper.allocateAndMapRange(tableLevel4, heapStart, flags, heapSizeInFrames);
    if (error) {
        panic("Cannot construct kernel heap");
    }
    Register::CR3::flushTLBS();
    
    return rlib::BumpAllocator(reinterpret_cast<void*>(heapStart), heapSizeInFrames * 4_KiB);
}

Kernel makeKernel() {
    auto physicalAdressTableLevel4 = Register::CR3::read(); 
    auto tableLevel4 = TableView(reinterpret_cast<std::uint64_t*>(physicalAdressTableLevel4), physicalAdressTableLevel4);
    auto startKernelSpace = VirtualAddress(0xFFFF8000'00000000);
    tableLevel4.at(startKernelSpace.indexLevel4()) = tableLevel4.at(0); // Shift identity map of first 512 GB by FFFF8000 00000000
    Register::CR3::flushTLBS(); // TODO: invalidate page
    auto identityMapping = IdentityMapping(startKernelSpace); 

    auto numberOfMemoryMapEntries = (bootboot.size - 128) / 16;
    auto memoryMap = std::span(&bootboot.mmap, numberOfMemoryMapEntries);
    auto memoryMapIterator = MemoryMapIterator(std::move(memoryMap));

    auto memorySource = rlib::MemorySource(reinterpret_cast<std::byte*>(bootboot.initrd_ptr), bootboot.initrd_size);
    auto inputStream = rlib::InputStream(std::move(memorySource));

    auto kernel = Kernel::make(memoryMapIterator, identityMapping, inputStream, &fb); 
}

int main()
{
    initializePanicHandler(getFrameBufferInfo());
   
    auto kernel = makeKernel();
    kernel.run();

    return 0;
}

#include <libr/icxxabi.hpp>
#include <libr/stream.hpp>
#include <libr/allocator.hpp>
#include <libr/type_erasure.hpp>
#include <libr/memory_resource.hpp>
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
extern BOOTBOOT      bootboot;
extern unsigned char environment[4096];
extern std::uint32_t fb; // linear framebuffer mapped
extern unsigned char initstack;

// Map of sections in the kernel binary
extern unsigned char __code_start;
extern unsigned char __writable_data_start;
extern unsigned char __writable_data_end;

// Initial heap
alignas(std::max_align_t) std::byte initialHeap[Kernel::IntialHeapSize];

FrameBufferInfo getFrameBufferInfo()
{
    return {&fb, bootboot.fb_size, bootboot.fb_width, bootboot.fb_height, bootboot.fb_scanline};
}

struct MemoryMapIterator : public rlib::Iterator<Block> {
    explicit MemoryMapIterator(std::span<MMapEnt> memoryMap) : memoryMap(std::move(memoryMap)), index(0) {}

    virtual std::optional<Block> next() final
    {
        while (index < memoryMap.size()) {
            const auto& mapEntry = memoryMap[index];
            if (!MMapEnt_IsFree(&mapEntry)) {
                index++;
                continue;
            }
            index++;
            return Block{mapEntry.ptr, mapEntry.size};
        }

        return {};
    }
private:
    std::span<MMapEnt> memoryMap;
    std::size_t        index;
};

std::expected<Kernel, rlib::Error> makeKernel()
{
    auto physicalAdressTableLevel4 = Register::CR3::read();
    auto tableLevel4 =
        TableView(reinterpret_cast<std::uint64_t*>(physicalAdressTableLevel4), physicalAdressTableLevel4);
    auto startKernelSpace = VirtualAddress(0xFFFF8000'00000000);
    tableLevel4.at(startKernelSpace.indexLevel4()) =
        tableLevel4.at(0); // Shift identity map of first 512 GB by FFFF8000 00000000
    Register::CR3::flushTLBS();

    auto numberOfMemoryMapEntries = (bootboot.size - 128) / 16;
    auto memoryMap                = std::span(&bootboot.mmap, numberOfMemoryMapEntries);
    auto sizeAccumulator          = [](auto acc, auto block) { return acc + MMapEnt_Size(&block); };
    auto totalPhysicalMemory = std::accumulate(memoryMap.begin(), memoryMap.end(), std::size_t(0), sizeAccumulator);
    auto memoryMapIterator   = MemoryMapIterator(std::move(memoryMap));
    auto identityMapping     = IdentityMapping(startKernelSpace);

    auto memoryLayout = MemoryLayout{
        &memoryMapIterator,
        totalPhysicalMemory,
        identityMapping,
        reinterpret_cast<std::uintptr_t>(&__code_start),
        reinterpret_cast<std::uintptr_t>(&__writable_data_start),
        reinterpret_cast<std::uintptr_t>(&__writable_data_end),
        reinterpret_cast<std::uintptr_t>(&initstack),
        &fb,
        bootboot.fb_size,
        bootboot.initrd_ptr, // This equals the physical address because of the identity mapping provided by BOOTBOOT
        bootboot.initrd_size
    };
    return Kernel::make(memoryLayout, initialHeap, tableLevel4);
}

int main()
{
    initializePanicHandler(getFrameBufferInfo());

    auto kernel = makeKernel();
    if (!kernel) {
        panic("Cannot create kernel");
    }
    kernel->run();

    return 0;
}

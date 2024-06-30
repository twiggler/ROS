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

// Initial heap
std::byte initialHeap[Kernel::IntialHeapSize];

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
    
    virtual std::optional<Block> next() final {
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
    std::size_t index;
};


std::expected<Kernel, rlib::Error> makeKernel() {
    auto physicalAdressTableLevel4 = Register::CR3::read(); 
    auto tableLevel4 = TableView(reinterpret_cast<std::uint64_t*>(physicalAdressTableLevel4), physicalAdressTableLevel4);
    auto startKernelSpace = VirtualAddress(0xFFFF8000'00000000);
    tableLevel4.at(startKernelSpace.indexLevel4()) = tableLevel4.at(0); // Shift identity map of first 512 GB by FFFF8000 00000000
    Register::CR3::flushTLBS();

    auto numberOfMemoryMapEntries = (bootboot.size - 128) / 16;
    auto memoryMap = std::span(&bootboot.mmap, numberOfMemoryMapEntries);
    auto memoryMapIterator = MemoryMapIterator(std::move(memoryMap));

    auto memorySource = rlib::MemorySource(reinterpret_cast<std::byte*>(bootboot.initrd_ptr), bootboot.initrd_size);
    auto inputStream = rlib::InputStream(std::move(memorySource));
    auto identityMapping = IdentityMapping(startKernelSpace); 

    return Kernel::make(memoryMapIterator, identityMapping, initialHeap, tableLevel4, inputStream, &fb); 
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

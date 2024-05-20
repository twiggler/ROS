#include "kernel/paging.hpp"
#include <utility>

namespace Memory {

Block Block::align(std::size_t alignment) const {
    auto alignmentMask = alignment - 1;
    auto alignedStartAddress = (startAddress + alignmentMask) & ~alignmentMask;
    auto alignedSize = (size - (alignedStartAddress - startAddress)) & ~alignmentMask;
    return { alignedStartAddress, alignedSize <= size ? alignedSize : 0 };
}

Block Block::take(std::size_t amount) const { 
    return amount <= size ? Block{startAddress + amount, size - amount} : Block{0, 0};
}

Block Block::resize(std::size_t newSize) const { 
    return Block{startAddress, newSize };
}

std::optional<PageFrameAllocator> PageFrameAllocator::make(rlib::Allocator& allocator, std::size_t physicalMemory, std::size_t frameSize) {
    auto base = rlib::construct<std::uintptr_t[]>(allocator, physicalMemory / frameSize);
    if (base == nullptr) {
        return {};
    }
    
    return PageFrameAllocator(nullptr, physicalMemory, frameSize);
}

PageFrameAllocator::PageFrameAllocator(rlib::OwningPointer<std::uintptr_t[]> base, std::size_t physicalMemory, std::size_t frameSize) :
    base(std::move(base)),
    top(base.get()),
    _physicalMemory(physicalMemory),
    frameSize(frameSize) {}

std::size_t PageFrameAllocator::physicalMemory() const {
    return _physicalMemory;
}

Block PageFrameAllocator::alloc() { 
    if (top == base.get()) {
        return { 0, 0 }; 
    }

    return {(*(--top)), frameSize };
}

void PageFrameAllocator::dealloc(void *ptr) {
    *(top++) = reinterpret_cast<std::uintptr_t>(ptr);
}

VirtualAddress::VirtualAddress(std::uintptr_t address) :
    address(address) {}

std::uint16_t VirtualAddress::indexLevel4() const { 
    return address >> 39 & 0x1ff;
}

std::uint16_t VirtualAddress::indexLevel3() const { 
    return address >> 30 & 0x1ff;
}

std::uint16_t VirtualAddress::indexLevel2() const { 
    return address >> 21 & 0x1ff;
}

std::uint16_t VirtualAddress::indexLevel1() const { 
    return address >> 12 & 0x1ff;
}

VirtualAddress::operator std::uintptr_t() const {
    return address;
}

PageTableEntry::PageTableEntry(std::uint64_t entry) : 
    entry(entry) {}

bool PageTableEntry::isUsed() const { return entry != 0; }

std::uint64_t PageTableEntry::flags() const { 
    return entry & PageFlags::All; 
}

std::uint64_t PageTableEntry::physicalAddress() const {
    return entry & encodedPhysicalAddress(-1);
}

PageTableEntry& PageTableEntry::setFlags(PageFlags::Type flags) {
    entry = (entry & ~PageFlags::All) | (flags & PageFlags::All);
    return *this;  
}

PageTableEntry& PageTableEntry::setPhysicalAddress(std::uint64_t address) {
    entry = (entry & ~encodedPhysicalAddress(-1)) | encodedPhysicalAddress(address);
    return *this; 
}

PageTableEntry PageTableEntry::empty() {
    return PageTableEntry(0);
}

PageTableEntry::operator std::uint64_t() const {
    return entry;
}

constexpr std::uint64_t PageTableEntry::encodedPhysicalAddress(std::uint64_t address) {
    return address & std::uint64_t(0xF'FFFF'FFFF'F000); 
}

// Any new page tables have their lifetime implicitly started. 
// We need to explicitly start the lifetime of any existing page tables.
// However, "std::start_lifetime_as" is not implemented yet in gcc.
// Technically, reading and writing to page entries is UB.
PageMapper::PageMapper(std::uintptr_t offset, PageFrameAllocator allocator) :
    offset(offset), frameAllocator(std::move(allocator)) {}

MapResult PageMapper::map(std::uint64_t* addressSpace, VirtualAddress virtualAddress, std::uint64_t physicalAddress, PageSize pageSize, PageFlags::Type flags) {
    auto indexLevel4 = virtualAddress.indexLevel4();
    auto tableLevel3 = ensurePageTable(addressSpace[indexLevel4]); 
    if (tableLevel3 == nullptr) {
        return MapResult::OUT_OF_PHYSICAL_MEMORY;
    }

    auto indexLevel3 = virtualAddress.indexLevel3();
    if (pageSize == PageSize::_1GiB) {
        if (PageTableEntry(tableLevel3[indexLevel3]).isUsed()) {
            return MapResult::ALREADY_MAPPED;
        }
        
        tableLevel3[indexLevel3] = PageTableEntry()
            .setPhysicalAddress(physicalAddress)
            .setFlags(flags | PageFlags::HugePage);

        return MapResult::OK;
    }
    auto tableLevel2 = ensurePageTable(tableLevel3[indexLevel3]);
    if (tableLevel2 == nullptr) {
        return MapResult::OUT_OF_PHYSICAL_MEMORY;
    }

    auto indexLevel2 = virtualAddress.indexLevel2();
    if (pageSize == PageSize::_2MiB) {
         if (PageTableEntry(tableLevel2[indexLevel2]).isUsed()) {
            return MapResult::ALREADY_MAPPED;
        }

        tableLevel2[indexLevel2] = PageTableEntry()
            .setPhysicalAddress(physicalAddress)
            .setFlags(flags | PageFlags::HugePage);

        return MapResult::OK;
    }
    auto tableLevel1 = ensurePageTable(tableLevel2[indexLevel2]);
    if (tableLevel1 == nullptr) {
        return MapResult::OUT_OF_PHYSICAL_MEMORY;
    }

    auto indexLevel1 = virtualAddress.indexLevel1();
    if (PageTableEntry(tableLevel1[indexLevel1]).isUsed()) {
        return MapResult::ALREADY_MAPPED;
    }
    tableLevel1[indexLevel1] = PageTableEntry()
        .setPhysicalAddress(physicalAddress)
        .setFlags(flags);

    return MapResult::OK;
}

std::size_t PageMapper::unmap(std::uint64_t* addressSpace, VirtualAddress virtualAddress) {
    auto indexLevel4 = virtualAddress.indexLevel4();
    auto entryLevel4 = PageTableEntry(addressSpace[indexLevel4]);
    if (!entryLevel4.isUsed()) {
        return 0;
    }

    auto indexLevel3 = virtualAddress.indexLevel3();
    auto tableLevel3 = reinterpret_cast<std::uint64_t*>(offset + entryLevel4.physicalAddress()); 
    auto entryLevel3 = PageTableEntry(tableLevel3[indexLevel3]);
    if (!entryLevel3.isUsed()) {
        return 0;
    }
    if (entryLevel3.flags() & PageFlags::HugePage) {
        tableLevel3[indexLevel3] = PageTableEntry::empty();
        return 1_GiB;
    }

    auto indexLevel2 = virtualAddress.indexLevel2();
    auto tableLevel2 = reinterpret_cast<std::uint64_t*>(offset + entryLevel3.physicalAddress()); 
    auto entryLevel2 = PageTableEntry(tableLevel2[indexLevel2]);
    if (!entryLevel2.isUsed()) {
        return 0; 
    }
    if (entryLevel2.flags() & PageFlags::HugePage) {
        tableLevel2[indexLevel2] = PageTableEntry::empty();
        return 2_MiB;
    }

    auto indexLevel1 = virtualAddress.indexLevel1();
    auto tableLevel1 = reinterpret_cast<std::uint64_t*>(offset + entryLevel2.physicalAddress()); 
    auto entryLevel1 = PageTableEntry(tableLevel1[indexLevel1]);
    if (!entryLevel1.isUsed()) {
        return 0; 
    }

    tableLevel1[indexLevel1] = PageTableEntry::empty();
    return 4_KiB;
}

PageMapper::Table PageMapper::createAddressSpace() {
    auto table = createPageTable();

    return table;
}

MapResult PageMapper::shallowCopyMapping(std::uint64_t* destAddressSpace, std::uint64_t* sourceAddressSpace, VirtualAddress startAddress, VirtualAddress endAddress) {
    auto startIndex = startAddress.indexLevel4();
    auto endIndex = endAddress.indexLevel4();
    if (startIndex > endIndex) {
        std::swap(startIndex, endIndex);
    }
    
    for (auto i = startIndex; i <= endIndex; i++) {
        destAddressSpace[i] = sourceAddressSpace[i];
    }

    return MapResult::OK;
}

Region PageMapper::allocate() {
    auto block = frameAllocator.alloc();
    if (block.size == 0) {
        return { nullptr, 0 };
    }

    return { reinterpret_cast<void*>(offset + block.startAddress), block.startAddress };
}

MapResult PageMapper::allocateAndMap(std::uint64_t* addressSpace, VirtualAddress virtualAddress, PageFlags::Type flags) {
    auto block = frameAllocator.alloc();
    if (block.size == 0) {
        return MapResult::OUT_OF_PHYSICAL_MEMORY;
    }

    return map(addressSpace, virtualAddress, block.startAddress, PageSize::_4KiB, flags);
}

MapResult PageMapper::allocateAndMapContiguous(std::uint64_t* addressSpace, VirtualAddress virtualAddress, PageFlags::Type flags, std::size_t nFrames) {
    for (auto i = std::size_t(0); i < nFrames; i++) {
        auto result = allocateAndMap(addressSpace, virtualAddress + i * 4_KiB, flags);
        if (result != MapResult::OK) {
            return result;
        }
    }
    
    return MapResult::OK;
}

void PageMapper::relocate(std::uintptr_t newOffset) {
    offset = newOffset;
}

std::uint64_t* PageMapper::ensurePageTable(std::uint64_t& rawParentEntry) { 
    auto entry = PageTableEntry(rawParentEntry);
    
    if (entry.isUsed()) {
        return reinterpret_cast<std::uint64_t*>(offset + entry.physicalAddress());
    } 

    auto table = createPageTable();

    rawParentEntry = PageTableEntry()
        .setPhysicalAddress(table.physicalAddress)
        .setFlags(PageFlags::Present | PageFlags::Writable | PageFlags::UserAccessible);

    return table.ptr;
}

PageMapper::Table PageMapper::createPageTable() {
    auto newTableBlock = frameAllocator.alloc();
    if (newTableBlock.size == 0) {
        return { nullptr, 0 };
    }
    
    auto tableStorage = reinterpret_cast<void*>(offset + newTableBlock.startAddress);
    auto table = new (tableStorage) (std::uint64_t[512]);
    for (auto i = 0; i < 512; i++) {
        table[i] = PageTableEntry::empty(); 
    }

    return { 
        table,
        newTableBlock.startAddress
    };
}

} // namespace Memory

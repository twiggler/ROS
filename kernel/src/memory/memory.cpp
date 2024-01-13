#include "memory.hpp"

namespace Memory {

Block Block::align(std::size_t alignment) const {
    auto alignmentMask = alignment - 1;
    auto alignedStartAddress = (ptr + alignmentMask) & ~alignmentMask;
    auto alignedSize = (size - (alignedStartAddress - ptr)) & ~alignmentMask;
    return { alignedStartAddress, alignedSize <= size ? alignedSize : 0 };
}

Block Block::resize(std::size_t newSize) const { 
    return Block{ptr, newSize };
}

std::size_t PageFrameAllocator::requiredStorage(std::size_t numberOfFrames) {
    return numberOfFrames * sizeof(std::uintptr_t);
}

PageFrameAllocator::PageFrameAllocator(Block storage, std::size_t physicalMemory, std::size_t frameSize) :
    base(reinterpret_cast<uintptr_t*>(storage.ptr)),
    top(base),
    _physicalMemory(physicalMemory),
    frameSize(frameSize) {}

std::size_t PageFrameAllocator::physicalMemory() const {
    return _physicalMemory;
}

Block PageFrameAllocator::alloc() { 
    if (top == base) {
        return { 0, 0 }; 
    }

    return {(*(--top)), frameSize };
}

void PageFrameAllocator::dealloc(void *ptr) {
    *(top++) = reinterpret_cast<std::uintptr_t>(ptr);
}

void PageFrameAllocator::relocate(std::uintptr_t newOffset) {
    auto newBase = reinterpret_cast<std::uintptr_t>(base) + newOffset;
    top = reinterpret_cast<std::uintptr_t*>(newBase + (top - base));
    base = reinterpret_cast<std::uintptr_t*>(newBase);
}    

VirtualAddress::VirtualAddress(std::uintptr_t address) :
    address(address) {}

std::uint16_t VirtualAddress::indexLevel4() const { 
    return (address & 0xFF80'0000'0000) >> 39;
}

std::uint16_t VirtualAddress::indexLevel3() const { 
    return (address & 0x7F'C000'0000) >> 30;
}

std::uint16_t VirtualAddress::indexLevel2() const { 
    return (address & 0x3FE0'0000) >> 21;
}

std::uint16_t VirtualAddress::indexLevel1() const { 
    return (address & 0x1F'F000) >> 12;
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

PageMapper::PageMapper(std::uint64_t *level4Table, std::uintptr_t offset, PageFrameAllocator& allocator) :
    tableLevel4(level4Table), offset(offset), frameAllocator(&allocator) {}

int PageMapper::map(VirtualAddress virtualAddress, std::uint64_t physicalAddress, PageSize pageSize, PageFlags::Type flags) {
    auto indexLevel4 = virtualAddress.indexLevel4();
    auto tableLevel3 = ensurePageTable(tableLevel4[indexLevel4]); 
    if (tableLevel3 == nullptr) {
        return -1;
    }

    auto indexLevel3 = virtualAddress.indexLevel3();
    if (pageSize == PageSize::_1GiB) {
        tableLevel3[indexLevel3] = PageTableEntry()
            .setPhysicalAddress(physicalAddress)
            .setFlags(flags | PageFlags::HugePage);

        return 0;
    }
    auto tableLevel2 = ensurePageTable(tableLevel3[indexLevel3]);
    if (tableLevel2 == nullptr) {
        return -1;
    }

    auto indexLevel2 = virtualAddress.indexLevel2();
    if (pageSize == PageSize::_2MiB) {
        tableLevel2[indexLevel2] = PageTableEntry()
            .setPhysicalAddress(physicalAddress)
            .setFlags(flags | PageFlags::HugePage);

        return 0;
    }
    auto tableLevel1 = ensurePageTable(tableLevel2[indexLevel2]);
    if (tableLevel1 == nullptr) {
        return -1;
    }

    auto indexLevel1 = virtualAddress.indexLevel1();
    tableLevel1[indexLevel1] = PageTableEntry()
        .setPhysicalAddress(physicalAddress)
        .setFlags(flags);

    return 0;
}

std::size_t PageMapper::unmap(VirtualAddress virtualAddress) {
    auto indexLevel4 = virtualAddress.indexLevel4();
    auto entryLevel4 = PageTableEntry(tableLevel4[indexLevel4]);
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

void PageMapper::relocate(std::uintptr_t newOffset) {
    frameAllocator->relocate(newOffset);
    offset = newOffset;
    auto relocatedTableLevel4 = reinterpret_cast<std::uintptr_t>(tableLevel4) + newOffset;
    tableLevel4 = reinterpret_cast<std::uintptr_t*>(relocatedTableLevel4);
}

std::uint64_t *PageMapper::ensurePageTable(std::uint64_t& rawParentEntry) { 
    auto entry = PageTableEntry(rawParentEntry);
    
    if (entry.isUsed()) {
        return reinterpret_cast<std::uint64_t*>(offset + entry.physicalAddress());
    } 

    auto newTableBlock = frameAllocator->alloc();
    if (newTableBlock.size == 0) {
        return nullptr;
    }
    rawParentEntry = PageTableEntry()
        .setPhysicalAddress(newTableBlock.ptr)
        .setFlags(PageFlags::Present | PageFlags::Writable | PageFlags::UserAccessible);

    return reinterpret_cast<std::uint64_t*>(newTableBlock.ptr);
}

} // namespace Memory

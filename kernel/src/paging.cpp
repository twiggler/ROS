#include "kernel/paging.hpp"
#include <utility>
#include <algorithm>

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

std::expected<Block, rlib::Error> PageFrameAllocator::alloc() { 
    if (top == base.get()) {
        return std::unexpected(OutOfPhysicalMemory);
    }

    return Block{(*(--top)), frameSize };
}

void PageFrameAllocator::dealloc(std::uintptr_t physicalAddress) {
    *(top++) = physicalAddress;
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

TableEntryView::TableEntryView(std::uint64_t& entry) : 
    entry(&entry) {}

TableEntryView& TableEntryView::operator=(const TableEntryView& other) {
    *entry = *other.entry;
    return *this;
}

bool TableEntryView::isUsed() const { return *entry != 0; }

std::uint64_t TableEntryView::flags() const { 
    return *entry & PageFlags::All; 
}

std::uint64_t TableEntryView::physicalAddress() const {
    return *entry & encodedPhysicalAddress(-1);
}

TableEntryView TableEntryView::setFlags(PageFlags::Type flags) {
    *entry = (*entry & ~PageFlags::All) | (flags & PageFlags::All);
    return *this;  
}

TableEntryView TableEntryView::setPhysicalAddress(std::uint64_t address) {
    *entry = (*entry & ~encodedPhysicalAddress(-1)) | encodedPhysicalAddress(address);
    return *this; 
}

void TableEntryView::clear() {
    *entry = 0;
}

constexpr std::uint64_t TableEntryView::encodedPhysicalAddress(std::uint64_t address) {
    return address & std::uint64_t(0xF'FFFF'FFFF'F000); 
}

TableView::TableView(std::uint64_t* ptr, std::uintptr_t physicalAddress) :
    ptr(ptr), _physicalAddress(physicalAddress) {}

TableEntryView TableView::at(std::uint16_t index) {
    // TODO: Check bounds. 

    return TableEntryView(ptr[index]);
}

 std::uintptr_t TableView::physicalAddress() const {
    return _physicalAddress;
}

PageMapper::PageMapper(std::uintptr_t offset, PageFrameAllocator allocator) :
    offset(offset), frameAllocator(std::move(allocator)) {}

TableView PageMapper::mapTableView(std::uintptr_t physicalAddress) const {
    // We need to explicitly start the lifetime of any existing page tables.
    // However, "std::start_lifetime_as" is not implemented yet in gcc.
    // Technically, reading and writing to page entries is UB.

    auto virtualAddress = reinterpret_cast<std::uint64_t*>(offset + physicalAddress);
    return TableView(virtualAddress, physicalAddress);
}

std::expected<TableView, rlib::Error> PageMapper::createPageTable() {
    auto newTableBlock = frameAllocator.alloc();
    if (!newTableBlock) {
        return std::unexpected(OutOfPhysicalMemory);
    }
    
    auto tableStorage = reinterpret_cast<void*>(offset + newTableBlock->startAddress);
    auto tablePtr = new (tableStorage) (std::uint64_t[512]);
    auto table = TableView(tablePtr, newTableBlock->startAddress);

    for (auto i = 0; i < 512; i++) {
        table.at(i).clear(); 
    }

    return table;
}

std::optional<rlib::Error> PageMapper::map(TableView addressSpace, VirtualAddress virtualAddress, std::uint64_t physicalAddress, PageSize pageSize, PageFlags::Type flags) {
    auto indexLevel4 = virtualAddress.indexLevel4();
    auto tableLevel3 = ensurePageTable(addressSpace.at(indexLevel4)); 
    if (!tableLevel3) {
        return tableLevel3.error();
    }

    auto indexLevel3 = virtualAddress.indexLevel3();
    if (pageSize == PageSize::_1GiB) {
        if (tableLevel3->at(indexLevel3).isUsed()) {
            return AlreadyMapped;
        }
        
        tableLevel3->at(indexLevel3)
            .setPhysicalAddress(physicalAddress)
            .setFlags(flags | PageFlags::HugePage);

        return {};
    }
    auto tableLevel2 = ensurePageTable(tableLevel3->at(indexLevel3));
    if (!tableLevel2) {
        return tableLevel2.error();
    }

    auto indexLevel2 = virtualAddress.indexLevel2();
    if (pageSize == PageSize::_2MiB) {
         if (tableLevel2->at(indexLevel2).isUsed()) {
            return AlreadyMapped;
        }

        tableLevel2->at(indexLevel2)
            .setPhysicalAddress(physicalAddress)
            .setFlags(flags | PageFlags::HugePage);

        return {};
    }
    auto tableLevel1 = ensurePageTable(tableLevel2->at(indexLevel2));
    if (!tableLevel1) {
        return tableLevel1.error();
    }

    auto indexLevel1 = virtualAddress.indexLevel1();
    if (tableLevel1->at(indexLevel1).isUsed()) {
        return AlreadyMapped;
    }
    tableLevel1->at(indexLevel1)
        .setPhysicalAddress(physicalAddress)
        .setFlags(flags);

    return {};
}

std::optional<Block> PageMapper::unmap(TableView addressSpace, VirtualAddress virtualAddress) {
    auto indexLevel4 = virtualAddress.indexLevel4();
    auto entryLevel4 = addressSpace.at(indexLevel4);
    if (!entryLevel4.isUsed()) {
        return {};
    }

    auto indexLevel3 = virtualAddress.indexLevel3();
    auto tableLevel3 = mapTableView(entryLevel4);
    auto entryLevel3 = tableLevel3.at(indexLevel3);
    if (!entryLevel3.isUsed()) {
        return {};
    }
    if (entryLevel3.flags() & PageFlags::HugePage) {
        entryLevel3.clear();
        return Block{entryLevel3.physicalAddress(), 1_GiB};
    }

    auto indexLevel2 = virtualAddress.indexLevel2();
    auto tableLevel2 = mapTableView(entryLevel3);
    auto entryLevel2 = tableLevel2.at(indexLevel2);
    if (!entryLevel2.isUsed()) {
        return {}; 
    }
    if (entryLevel2.flags() & PageFlags::HugePage) {
        entryLevel2.clear();
        return Block{entryLevel2.physicalAddress(), 2_MiB};
    }

    auto indexLevel1 = virtualAddress.indexLevel1();
    auto tableLevel1 = mapTableView(entryLevel2);
    auto entryLevel1 = tableLevel1.at(indexLevel1);
    if (!entryLevel1.isUsed()) {
        return {}; 
    }

    entryLevel1.clear();
    return Block(entryLevel1.physicalAddress(), 4_KiB);
}

std::optional<Block> PageMapper::unmapAndDeallocate(TableView addressSpace, VirtualAddress virtualAddress) {
    auto block = unmap(addressSpace, virtualAddress);
    if (!block) {
        return {};
    }

    for (auto i = std::size_t(0); i < block->size; i += 4_KiB) {
        frameAllocator.dealloc(block->startAddress + i);
    }
    return block;
}

std::expected<PageFrame, rlib::Error> PageMapper::allocate() {
    auto block = frameAllocator.alloc();
    if (!block) {
        return std::unexpected(OutOfPhysicalMemory);
    }

    return PageFrame{reinterpret_cast<void*>(offset + block->startAddress), block->startAddress};
}

std::optional<rlib::Error> PageMapper::allocateAndMap(TableView addressSpace, VirtualAddress virtualAddress, PageFlags::Type flags) {
    auto block = frameAllocator.alloc();
    if (!block) {
        return block.error();
    }

    return map(addressSpace, virtualAddress, block->startAddress, PageSize::_4KiB, flags);
}

std::optional<rlib::Error> PageMapper::allocateAndMapRange(TableView addressSpace, VirtualAddress virtualAddress, PageFlags::Type flags, std::size_t nFrames) {
    for (auto i = std::size_t(0); i < nFrames; i++) {
        auto error = allocateAndMap(addressSpace, virtualAddress + i * 4_KiB, flags);
        if (error) {
            return error;
        }
    }
    
    return {};
}

std::size_t PageMapper::unmapAndDeallocateRange(TableView addressSpace, VirtualAddress virtualAddress, std::size_t size) {
    auto freed = std::size_t(0);

    while (freed < size) {
        auto block = unmapAndDeallocate(addressSpace, virtualAddress + freed);
        if (!block) {
            return freed;
        }
        freed += block->size;
    }

    return freed;
}

void PageMapper::relocate(std::uintptr_t newOffset) {
    offset = newOffset;
}

std::expected<TableView, rlib::Error> PageMapper::ensurePageTable(TableEntryView entry) { 
    if (entry.isUsed()) {
        return mapTableView(entry);
    } 

    auto table = createPageTable();
    if (!table) {
        return std::unexpected(table.error());
    }

    entry
        .setPhysicalAddress(table->physicalAddress())
        .setFlags(PageFlags::Present | PageFlags::Writable | PageFlags::UserAccessible);

    return table;
}

TableView PageMapper::mapTableView(TableEntryView entry) const {
    return mapTableView(entry.physicalAddress());
}

Region::Region(VirtualAddress virtualAddress, std::size_t sizeInFrames, PageFlags::Type pageFlags, PageSize pageSize) :
    _start(virtualAddress), sizeInFrames(sizeInFrames), pageFlags(pageFlags), _pageSize(pageSize) {}

bool Region::overlap(const Region& other) const {
    return _start < other.end() && end() > other._start;
}

std::optional<rlib::Error> Region::mapPage(TableView tableLevel4, PageMapper& pageMapper, std::uint64_t physicalAddress, std::size_t pageIndex) {
    if (pageIndex > sizeInFrames) {
        return OutOfBounds;
    }

    auto offset = pageIndex * pageSizeInBytes();
    return pageMapper.map(tableLevel4, _start + offset, physicalAddress, _pageSize, pageFlags);
}

std::optional<rlib::Error> Region::map(TableView tableLevel4, PageMapper& pageMapper) {
    return pageMapper.allocateAndMapRange(tableLevel4, _start, pageFlags, sizeInFrames);
}

VirtualAddress Region::start() const {
    return _start;
}

VirtualAddress Region::end() const {
    return _start + size();
}

std::size_t Region::size() const {
    return sizeInFrames * pageSizeInBytes();
}

std::size_t Region::pageSizeInBytes() const {
    return static_cast<std::uint32_t>(_pageSize);
};

std::expected<AddressSpace, rlib::Error> AddressSpace::make(PageMapper& pageMapper, rlib::Allocator& allocator) {
    auto tableLevel4 = pageMapper.createPageTable();
    if (!tableLevel4) {
        return std::unexpected(tableLevel4.error());
    }

    return AddressSpace(pageMapper, *tableLevel4, allocator);    
}

AddressSpace::AddressSpace(PageMapper& pageMapper, TableView tableLevel4, rlib::Allocator& allocator) :
    pageMapper(&pageMapper), tableLevel4(tableLevel4), allocator(&allocator) {}

std::expected<Region*, rlib::Error> AddressSpace::reserve(VirtualAddress start, std::size_t size, PageFlags::Type flags, PageSize pageSize) {
    auto pageSizeInBytes = static_cast<std::uint32_t>(pageSize); 
    auto sizeInFrames = (size + pageSizeInBytes - 1) / pageSizeInBytes;
    auto region = Region(start, sizeInFrames, flags, pageSize);

    // This has a complexity of O(n). Optimize using balanced tree or skip list.
    if (std::ranges::any_of(regions, [&region](const auto& other) { return region.overlap(other); })) {
        return std::unexpected(VirtualRangeInUse);
    }

    auto regionPtr = rlib::constructRaw<Region>(*allocator, region);
    if (regionPtr == nullptr) {
        return std::unexpected(OutOfPhysicalMemory);
    }

    regions.push_front(*regionPtr);
    return regionPtr;
}

std::expected<Region*, rlib::Error> AddressSpace::allocate(VirtualAddress start, std::size_t size, PageFlags::Type flags, PageSize pageSize) {
    auto region = reserve(start, size, flags, pageSize);
    if (region == nullptr) {
        return std::unexpected(OutOfPhysicalMemory);
    }

    auto error = region.value()->map(tableLevel4, *pageMapper);
    if (error) {
        return std::unexpected(*error);
    }

    return region;
}

std::optional<rlib::Error> AddressSpace::mapPage(Region& region, std::uint64_t physicalAddress, std::size_t offsetInFrames) {
    return region.mapPage(tableLevel4, *pageMapper, physicalAddress, offsetInFrames);
}

AddressSpace::~AddressSpace() {
    auto region = regions.pop_front();
    while (region != nullptr) {
        pageMapper->unmapAndDeallocateRange(tableLevel4, region->start(), region->size());
        destruct(region, *allocator);
        region = regions.pop_front();
    }
}

std::uintptr_t AddressSpace::pageDirectoryPhysicalAddress() const {
    return tableLevel4.physicalAddress();
}

void AddressSpace::shallowCopyMapping(TableView from, VirtualAddress startAddress, VirtualAddress endAddress) {
    auto startIndex = startAddress.indexLevel4();
    auto endIndex = endAddress.indexLevel4();
    if (startIndex > endIndex) {
        std::swap(startIndex, endIndex);
    }
    
    for (auto i = startIndex; i <= endIndex; i++) {
        tableLevel4.at(i) = from.at(i);
    }
}
